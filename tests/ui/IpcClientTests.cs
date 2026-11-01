using System.Buffers.Binary;
using System.IO;
using System.IO.Pipes;
using System.Text;
using System.Text.Json;
using WinSheduler.Shared;
using WinSheduler.UI.Services;
using Xunit;

namespace WinSheduler.UI.Tests;

/// <summary>D-4: фрейминг, типизированные ошибки и legacy-совместимость IPC-клиента.</summary>
public class IpcClientTests
{
    [Fact]
    public async Task V2_ЧитаетКадрСДлинойИВозвращаетТипизированныйОтвет()
    {
        var pipeName = NewPipeName();
        using var server = CreateServer(pipeName, PipeTransmissionMode.Byte);

        var serverTask = Task.Run(async () =>
        {
            await server.WaitForConnectionAsync();
            var request = await ReadFrameAsync(server);

            using var document = JsonDocument.Parse(request);
            var root = document.RootElement;
            Assert.Equal(1, root.GetProperty("v").GetInt32());
            Assert.Equal("Ping", root.GetProperty("action").GetString());

            var id = root.GetProperty("id").GetString();
            await WriteFrameAsync(
                server,
                $"{{\"v\":1,\"id\":\"{id}\",\"ok\":true,\"error\":null,\"payload\":{{\"pong\":true,\"serviceVersion\":\"9.9.9\"}}}}");

            await Task.Delay(100);
        });

        await using var client = new IpcClient(new IpcClientOptions
        {
            PipeName = pipeName,
            MaxAttempts = 1,
            ResponseTimeout = TimeSpan.FromSeconds(5),
        });

        var ping = await client.CallAsync<PingResult>("Ping", null, idempotent: true, CancellationToken.None);

        Assert.True(ping.Pong);
        Assert.Equal("9.9.9", ping.ServiceVersion);
        await serverTask;
    }

    [Fact]
    public async Task V2_ОшибкаСлужбыПревращаетсяВТипизированноеИсключение()
    {
        var pipeName = NewPipeName();
        using var server = CreateServer(pipeName, PipeTransmissionMode.Byte);

        var serverTask = Task.Run(async () =>
        {
            await server.WaitForConnectionAsync();
            var request = await ReadFrameAsync(server);
            using var document = JsonDocument.Parse(request);
            var id = document.RootElement.GetProperty("id").GetString();

            await WriteFrameAsync(
                server,
                $"{{\"v\":1,\"id\":\"{id}\",\"ok\":false,\"error\":{{\"code\":\"AUTH_DENIED\",\"message\":\"Недостаточно прав\"}},\"payload\":null}}");

            await Task.Delay(100);
        });

        await using var client = new IpcClient(new IpcClientOptions
        {
            PipeName = pipeName,
            MaxAttempts = 1,
            ResponseTimeout = TimeSpan.FromSeconds(5),
        });

        var exception = await Assert.ThrowsAsync<IpcException>(
            () => client.CallAsync<PingResult>("Ping", null, idempotent: true, CancellationToken.None));

        Assert.Equal(IpcErrorCode.AuthDenied, exception.Code);
        Assert.Equal("AUTH_DENIED", exception.RawCode);
        Assert.Equal("Недостаточно прав", exception.Message);
        await serverTask;
    }

    [Fact]
    public async Task V2_ПревышениеРазмераКадраОтклоняется()
    {
        var pipeName = NewPipeName();
        using var server = CreateServer(pipeName, PipeTransmissionMode.Byte);

        var serverTask = Task.Run(async () =>
        {
            await server.WaitForConnectionAsync();
            await ReadFrameAsync(server);

            var header = new byte[4];
            BinaryPrimitives.WriteUInt32LittleEndian(header, 1024 * 1024 + 1);
            await server.WriteAsync(header);
            await server.FlushAsync();
            await Task.Delay(100);
        });

        await using var client = new IpcClient(new IpcClientOptions
        {
            PipeName = pipeName,
            MaxAttempts = 1,
            ResponseTimeout = TimeSpan.FromSeconds(5),
        });

        var exception = await Assert.ThrowsAsync<IpcException>(
            () => client.CallAsync<PingResult>("Ping", null, idempotent: true, CancellationToken.None));

        Assert.Equal(IpcErrorCode.TooLarge, exception.Code);
        await serverTask;
    }

    [Fact]
    public async Task V2_ТаймаутОтветаДаётВременнуюОшибку()
    {
        var pipeName = NewPipeName();
        using var server = CreateServer(pipeName, PipeTransmissionMode.Byte);

        var serverTask = Task.Run(async () =>
        {
            await server.WaitForConnectionAsync();
            await ReadFrameAsync(server);
            await Task.Delay(1000);
        });

        await using var client = new IpcClient(new IpcClientOptions
        {
            PipeName = pipeName,
            MaxAttempts = 1,
            ResponseTimeout = TimeSpan.FromMilliseconds(200),
        });

        var exception = await Assert.ThrowsAsync<IpcException>(
            () => client.CallAsync<PingResult>("Ping", null, idempotent: true, CancellationToken.None));

        Assert.Equal(IpcErrorCode.Timeout, exception.Code);
        Assert.True(exception.IsTransient);
        await serverTask;
    }

    [Fact]
    public async Task Legacy_КонвертСовместимСТекущимиЭкранами()
    {
        var pipeName = NewPipeName();
        using var server = CreateServer(pipeName, PipeTransmissionMode.Message);

        var serverTask = Task.Run(async () =>
        {
            await server.WaitForConnectionAsync();

            var buffer = new byte[65536];
            var read = await server.ReadAsync(buffer);
            var request = Encoding.UTF8.GetString(buffer, 0, read);
            Assert.Contains("\"Action\":\"GetStatus\"", request);

            var response = Encoding.UTF8.GetBytes(
                "{\"Success\":true,\"Error\":\"\",\"Payload\":\"{\\\"running\\\":true}\"}");
            await server.WriteAsync(response);
            await server.FlushAsync();
            await Task.Delay(100);
        });

        await using var client = new IpcClient(new IpcClientOptions { PipeName = pipeName });

        var response = await client.SendAsync(new IpcRequest { Action = "GetStatus" });

        Assert.True(response.Success);
        Assert.Contains("running", response.Payload);
        await serverTask;
    }

    [Fact]
    public void MapCode_СопоставляетКодыКонтракта()
    {
        Assert.Equal(IpcErrorCode.AuthDenied, IpcException.MapCode("AUTH_DENIED"));
        Assert.Equal(IpcErrorCode.NotFound, IpcException.MapCode("NOT_FOUND"));
        Assert.Equal(IpcErrorCode.Validation, IpcException.MapCode("VALIDATION"));
        Assert.Equal(IpcErrorCode.Conflict, IpcException.MapCode("CONFLICT"));
        Assert.Equal(IpcErrorCode.TooLarge, IpcException.MapCode("TOO_LARGE"));
        Assert.Equal(IpcErrorCode.UnsupportedVersion, IpcException.MapCode("UNSUPPORTED_VERSION"));
        Assert.Equal(IpcErrorCode.Database, IpcException.MapCode("DB"));
        Assert.Equal(IpcErrorCode.Internal, IpcException.MapCode("INTERNAL"));
        Assert.Equal(IpcErrorCode.Unknown, IpcException.MapCode("SOMETHING"));
        Assert.Equal(IpcErrorCode.Unknown, IpcException.MapCode(null));
    }

    private static string NewPipeName() => "WinShedulerTest_" + Guid.NewGuid().ToString("N");

    private static NamedPipeServerStream CreateServer(string name, PipeTransmissionMode mode)
        => new(name, PipeDirection.InOut, 1, mode, PipeOptions.Asynchronous);

    private static async Task<string> ReadFrameAsync(Stream stream)
    {
        var header = new byte[4];
        await stream.ReadExactlyAsync(header);

        var length = BinaryPrimitives.ReadUInt32LittleEndian(header);
        var body = new byte[(int)length];
        await stream.ReadExactlyAsync(body);

        return Encoding.UTF8.GetString(body);
    }

    private static async Task WriteFrameAsync(Stream stream, string json)
    {
        var body = Encoding.UTF8.GetBytes(json);
        var frame = new byte[4 + body.Length];
        BinaryPrimitives.WriteUInt32LittleEndian(frame, (uint)body.Length);
        body.CopyTo(frame, 4);

        await stream.WriteAsync(frame);
        await stream.FlushAsync();
    }
}
