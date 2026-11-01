using System.Buffers.Binary;
using System.IO;
using System.IO.Pipes;
using System.Text;
using System.Text.Json;
using System.Text.Json.Serialization;
using WinSheduler.Shared;
using WinSheduler.UI.Localization;
using WinSheduler.UI.Services;

namespace WinSheduler.UI;

/// <summary>Параметры подключения к службе по именованному каналу.</summary>
public sealed class IpcClientOptions
{
    /// <summary>Имя канала (контракт IPC.md).</summary>
    public string PipeName { get; init; } = "WinSheduler";

    /// <summary>Сервер канала; локальный по умолчанию.</summary>
    public string ServerName { get; init; } = ".";

    /// <summary>Таймаут подключения.</summary>
    public TimeSpan ConnectTimeout { get; init; } = TimeSpan.FromSeconds(5);

    /// <summary>Таймаут ожидания ответа на один кадр.</summary>
    public TimeSpan ResponseTimeout { get; init; } = TimeSpan.FromSeconds(30);

    /// <summary>Максимальный размер кадра — 1 МиБ (контракт IPC.md).</summary>
    public int MaxFrameBytes { get; init; } = 1024 * 1024;

    /// <summary>Число попыток для идемпотентных запросов (1 — без повторов).</summary>
    public int MaxAttempts { get; init; } = 2;

    /// <summary>Базовая пауза между повторами; растёт линейно.</summary>
    public TimeSpan RetryDelay { get; init; } = TimeSpan.FromMilliseconds(200);
}

/// <summary>
/// Клиент IPC v2: фрейминг «4 байта длины little-endian + JSON», одно соединение,
/// последовательные запросы без конвейеризации, таймауты, отмена, типизированные
/// ошибки и повтор идемпотентных запросов.
///
/// Дополнительно сохранены legacy-методы <see cref="Send"/> и <see cref="SendAsync(IpcRequest)"/>
/// для текущих экранов: они работают по старому конверту в режиме сообщений и будут
/// удалены после перевода экранов на <see cref="IIpcService"/>.
/// </summary>
public sealed class IpcClient : IAsyncDisposable
{
    private readonly IpcClientOptions _options;
    private readonly SemaphoreSlim _gate = new(1, 1);

    private NamedPipeClientStream? _pipe;
    private bool _disposed;

    public IpcClient()
        : this(new IpcClientOptions())
    {
    }

    public IpcClient(IpcClientOptions options)
    {
        _options = options ?? throw new ArgumentNullException(nameof(options));
    }

    /// <summary>Параметры подключения.</summary>
    public IpcClientOptions Options => _options;

    /// <summary>Настройки чтения ответов: имена свойств без учёта регистра, перечисления строками.</summary>
    internal static JsonSerializerOptions ReadOptions { get; } = new()
    {
        PropertyNameCaseInsensitive = true,
        Converters = { new JsonStringEnumConverter() },
    };

    /// <summary>Настройки записи запросов: camelCase и форматы дат контракта.</summary>
    internal static JsonSerializerOptions WriteOptions { get; } = new()
    {
        PropertyNameCaseInsensitive = true,
        PropertyNamingPolicy = JsonNamingPolicy.CamelCase,
        DefaultIgnoreCondition = JsonIgnoreCondition.WhenWritingNull,
        Converters = { new JsonStringEnumConverter(), new IpcDateTimeConverter() },
    };

    /// <summary>
    /// Выполняет типизированный запрос v2. Идемпотентные запросы повторяются
    /// при временной недоступности службы. Низкоуровневый метод: прикладной код
    /// использует <see cref="IIpcService"/>.
    /// </summary>
    public async Task<T> CallAsync<T>(
        string action,
        object? payload,
        bool idempotent,
        CancellationToken cancellationToken)
    {
        var element = await CallWithRetryAsync(action, payload, idempotent, cancellationToken).ConfigureAwait(false);
        if (element is null)
        {
            throw new IpcException(IpcErrorCode.Internal, null, Strings.Get("Error_EmptyResponse"));
        }

        try
        {
            return element.Value.Deserialize<T>(ReadOptions)
                ?? throw new IpcException(IpcErrorCode.Internal, null, Strings.Get("Error_EmptyResponse"));
        }
        catch (JsonException ex)
        {
            throw new IpcException(IpcErrorCode.Internal, null, Strings.Get("Error_BadResponse"), innerException: ex);
        }
    }

    /// <summary>Выполняет запрос v2 без разбора payload.</summary>
    public async Task CallAsync(
        string action,
        object? payload,
        bool idempotent,
        CancellationToken cancellationToken)
        => await CallWithRetryAsync(action, payload, idempotent, cancellationToken).ConfigureAwait(false);

    private async Task<JsonElement?> CallWithRetryAsync(
        string action,
        object? payload,
        bool idempotent,
        CancellationToken cancellationToken)
    {
        var attempts = idempotent ? Math.Max(1, _options.MaxAttempts) : 1;

        for (var attempt = 1; ; attempt++)
        {
            try
            {
                return await CallOnceAsync(action, payload, cancellationToken).ConfigureAwait(false);
            }
            catch (IpcException ex) when (ex.IsTransient && attempt < attempts && !cancellationToken.IsCancellationRequested)
            {
                await Task.Delay(_options.RetryDelay * attempt, cancellationToken).ConfigureAwait(false);
            }
        }
    }

    private async Task<JsonElement?> CallOnceAsync(string action, object? payload, CancellationToken cancellationToken)
    {
        ObjectDisposedException.ThrowIf(_disposed, this);

        await _gate.WaitAsync(cancellationToken).ConfigureAwait(false);
        try
        {
            var pipe = await EnsureConnectedAsync(cancellationToken).ConfigureAwait(false);
            var requestId = Guid.NewGuid().ToString("D");
            var body = BuildRequest(requestId, action, payload);

            if (body.Length > _options.MaxFrameBytes)
            {
                throw new IpcException(IpcErrorCode.TooLarge, "TOO_LARGE", Strings.Get("Error_TooLarge"));
            }

            await WriteFrameAsync(pipe, body, cancellationToken).ConfigureAwait(false);
            var response = await ReadFrameAsync(pipe, cancellationToken).ConfigureAwait(false);
            return ParseResponse(requestId, response);
        }
        catch (IpcException)
        {
            throw;
        }
        catch (OperationCanceledException) when (cancellationToken.IsCancellationRequested)
        {
            ResetConnection();
            throw;
        }
        catch (Exception ex) when (ex is IOException or TimeoutException or ObjectDisposedException or UnauthorizedAccessException)
        {
            ResetConnection();
            throw new IpcUnavailableException(Strings.Get("Error_Unavailable"), ex);
        }
        finally
        {
            _gate.Release();
        }
    }

    private async Task<NamedPipeClientStream> EnsureConnectedAsync(CancellationToken cancellationToken)
    {
        if (_pipe is { IsConnected: true })
        {
            return _pipe;
        }

        ResetConnection();

        var pipe = new NamedPipeClientStream(
            _options.ServerName,
            _options.PipeName,
            PipeDirection.InOut,
            PipeOptions.Asynchronous);

        try
        {
            await pipe.ConnectAsync(
                (int)_options.ConnectTimeout.TotalMilliseconds,
                cancellationToken).ConfigureAwait(false);
        }
        catch (OperationCanceledException)
        {
            pipe.Dispose();
            throw;
        }
        catch (Exception ex) when (ex is TimeoutException or IOException or UnauthorizedAccessException)
        {
            pipe.Dispose();
            throw new IpcUnavailableException(Strings.Get("Error_Unavailable"), ex);
        }

        _pipe = pipe;
        return pipe;
    }

    private byte[] BuildRequest(string requestId, string action, object? payload)
    {
        using var buffer = new MemoryStream();

        using (var writer = new Utf8JsonWriter(buffer))
        {
            writer.WriteStartObject();
            writer.WriteNumber("v", 1);
            writer.WriteString("id", requestId);
            writer.WriteString("action", action);
            writer.WritePropertyName("payload");

            if (payload is null)
            {
                writer.WriteStartObject();
                writer.WriteEndObject();
            }
            else
            {
                JsonSerializer.Serialize(writer, payload, payload.GetType(), WriteOptions);
            }

            writer.WriteEndObject();
        }

        return buffer.ToArray();
    }

    private async Task WriteFrameAsync(NamedPipeClientStream pipe, byte[] body, CancellationToken cancellationToken)
    {
        var frame = new byte[4 + body.Length];
        BinaryPrimitives.WriteUInt32LittleEndian(frame, (uint)body.Length);
        body.CopyTo(frame, 4);

        try
        {
            await pipe.WriteAsync(frame, cancellationToken)
                .AsTask()
                .WaitAsync(_options.ResponseTimeout, cancellationToken)
                .ConfigureAwait(false);
        }
        catch (TimeoutException ex)
        {
            ResetConnection();
            throw new IpcException(IpcErrorCode.Timeout, null, Strings.Get("Error_Timeout"), isTransient: true, ex);
        }
    }

    private async Task<byte[]> ReadFrameAsync(NamedPipeClientStream pipe, CancellationToken cancellationToken)
    {
        var header = new byte[4];
        await ReadExactlyAsync(pipe, header, cancellationToken).ConfigureAwait(false);

        var length = BinaryPrimitives.ReadUInt32LittleEndian(header);
        if (length == 0 || length > (uint)_options.MaxFrameBytes)
        {
            ResetConnection();
            throw new IpcException(IpcErrorCode.TooLarge, "TOO_LARGE", Strings.Get("Error_TooLarge"));
        }

        var body = new byte[(int)length];
        await ReadExactlyAsync(pipe, body, cancellationToken).ConfigureAwait(false);
        return body;
    }

    private async Task ReadExactlyAsync(NamedPipeClientStream pipe, Memory<byte> buffer, CancellationToken cancellationToken)
    {
        using var linked = CancellationTokenSource.CreateLinkedTokenSource(cancellationToken);
        var readTask = pipe.ReadExactlyAsync(buffer, linked.Token).AsTask();
        var timeoutTask = Task.Delay(_options.ResponseTimeout, linked.Token);

        var completed = await Task.WhenAny(readTask, timeoutTask).ConfigureAwait(false);
        if (completed != readTask)
        {
            linked.Cancel();
            Observe(readTask);
            ResetConnection();

            cancellationToken.ThrowIfCancellationRequested();
            throw new IpcException(IpcErrorCode.Timeout, null, Strings.Get("Error_Timeout"), isTransient: true);
        }

        await readTask.ConfigureAwait(false);
    }

    private static JsonElement? ParseResponse(string requestId, byte[] body)
    {
        JsonDocument document;
        try
        {
            document = JsonDocument.Parse(body);
        }
        catch (JsonException ex)
        {
            throw new IpcException(IpcErrorCode.Internal, null, Strings.Get("Error_BadResponse"), innerException: ex);
        }

        using (document)
        {
            var root = document.RootElement;
            if (root.ValueKind != JsonValueKind.Object)
            {
                throw new IpcException(IpcErrorCode.Internal, null, Strings.Get("Error_BadResponse"));
            }

            var responseId = GetString(root, "id");
            if (!string.IsNullOrEmpty(responseId) &&
                !string.Equals(responseId, requestId, StringComparison.OrdinalIgnoreCase))
            {
                throw new IpcException(IpcErrorCode.Internal, null, Strings.Get("Error_BadResponse"));
            }

            if (!TryGetProperty(root, "ok", out var okElement) &&
                !TryGetProperty(root, "success", out okElement))
            {
                throw new IpcException(IpcErrorCode.Internal, null, Strings.Get("Error_BadResponse"));
            }

            if (okElement.ValueKind != JsonValueKind.True)
            {
                var rawCode = string.Empty;
                var message = string.Empty;

                if (TryGetProperty(root, "error", out var errorElement))
                {
                    if (errorElement.ValueKind == JsonValueKind.Object)
                    {
                        rawCode = GetString(errorElement, "code") ?? string.Empty;
                        message = GetString(errorElement, "message") ?? string.Empty;
                    }
                    else if (errorElement.ValueKind == JsonValueKind.String)
                    {
                        message = errorElement.GetString() ?? string.Empty;
                    }
                }

                throw new IpcException(
                    IpcException.MapCode(rawCode),
                    rawCode,
                    string.IsNullOrWhiteSpace(message) ? Strings.Get("Error_Unknown") : message);
            }

            if (!TryGetProperty(root, "payload", out var payloadElement) ||
                payloadElement.ValueKind == JsonValueKind.Null)
            {
                return null;
            }

            // Legacy-ответ содержит payload строкой с вложенным JSON.
            if (payloadElement.ValueKind == JsonValueKind.String)
            {
                var text = payloadElement.GetString();
                if (string.IsNullOrWhiteSpace(text))
                {
                    return null;
                }

                try
                {
                    using var inner = JsonDocument.Parse(text);
                    return inner.RootElement.Clone();
                }
                catch (JsonException ex)
                {
                    throw new IpcException(IpcErrorCode.Internal, null, Strings.Get("Error_BadResponse"), innerException: ex);
                }
            }

            return payloadElement.Clone();
        }
    }

    private static bool TryGetProperty(JsonElement element, string name, out JsonElement value)
    {
        if (element.TryGetProperty(name, out value))
        {
            return true;
        }

        foreach (var property in element.EnumerateObject())
        {
            if (string.Equals(property.Name, name, StringComparison.OrdinalIgnoreCase))
            {
                value = property.Value;
                return true;
            }
        }

        value = default;
        return false;
    }

    private static string? GetString(JsonElement element, string name)
        => TryGetProperty(element, name, out var value) && value.ValueKind == JsonValueKind.String
            ? value.GetString()
            : null;

    private void ResetConnection()
    {
        var pipe = _pipe;
        _pipe = null;

        try
        {
            pipe?.Dispose();
        }
        catch (IOException)
        {
            // Соединение уже разорвано.
        }
    }

    private static void Observe(Task task)
        => _ = task.ContinueWith(
            static completed => _ = completed.Exception,
            CancellationToken.None,
            TaskContinuationOptions.OnlyOnFaulted | TaskContinuationOptions.ExecuteSynchronously,
            TaskScheduler.Default);

    // ===== Временный слой совместимости с текущими экранами (legacy-конверт) =====

    /// <summary>
    /// Синхронный legacy-запрос. Оставлен для текущего кода экранов;
    /// новый код использует <see cref="IIpcService"/>.
    /// </summary>
    public IpcResponse Send(IpcRequest request) => SendAsync(request).GetAwaiter().GetResult();

    /// <summary>
    /// Legacy-запрос по старому конверту <c>{Action, Payload}</c> в режиме сообщений.
    /// Создаёт отдельное соединение и не участвует в пуле v2.
    /// </summary>
    public async Task<IpcResponse> SendAsync(IpcRequest request)
    {
        try
        {
            using var pipe = new NamedPipeClientStream(
                _options.ServerName,
                _options.PipeName,
                PipeDirection.InOut,
                PipeOptions.Asynchronous);

            await pipe.ConnectAsync((int)_options.ConnectTimeout.TotalMilliseconds)
                .WaitAsync(_options.ConnectTimeout)
                .ConfigureAwait(false);

            pipe.ReadMode = PipeTransmissionMode.Message;

            var requestJson = JsonSerializer.Serialize(request, ReadOptions);
            var requestBytes = Encoding.UTF8.GetBytes(requestJson);
            await pipe.WriteAsync(requestBytes)
                .AsTask()
                .WaitAsync(_options.ResponseTimeout)
                .ConfigureAwait(false);

            using var buffer = new MemoryStream();
            var chunk = new byte[64 * 1024];

            while (true)
            {
                var read = await pipe.ReadAsync(chunk)
                    .AsTask()
                    .WaitAsync(_options.ResponseTimeout)
                    .ConfigureAwait(false);

                if (read <= 0)
                {
                    break;
                }

                buffer.Write(chunk, 0, read);
                if (pipe.IsMessageComplete)
                {
                    break;
                }
            }

            var responseJson = Encoding.UTF8.GetString(buffer.ToArray());
            if (string.IsNullOrEmpty(responseJson))
            {
                return new IpcResponse { Success = false, Error = Strings.Get("Error_EmptyResponse") };
            }

            return JsonSerializer.Deserialize<IpcResponse>(responseJson, ReadOptions)
                ?? new IpcResponse { Success = false, Error = Strings.Get("Error_BadResponse") };
        }
        catch (Exception ex)
        {
            return new IpcResponse { Success = false, Error = ex.Message };
        }
    }

    /// <summary>Закрывает соединение и освобождает ресурсы.</summary>
    public async ValueTask DisposeAsync()
    {
        if (_disposed)
        {
            return;
        }

        _disposed = true;
        await _gate.WaitAsync().ConfigureAwait(false);
        try
        {
            ResetConnection();
        }
        finally
        {
            _gate.Release();
            _gate.Dispose();
        }
    }
}
