using System.IO;
using System.IO.Pipes;
using System.Text;
using System.Text.Json;
using System.Text.Json.Serialization;
using WinSheduler.Shared;

namespace WinSheduler.UI;

public class IpcClient
{
    private const string PipeName = "WinSheduler";
    private const int TimeoutMs = 5000;
    private static readonly JsonSerializerOptions JsonOpts = new()
    {
        Converters = { new JsonStringEnumConverter() }
    };

    public IpcResponse Send(IpcRequest request)
    {
        try
        {
            using var pipe = new NamedPipeClientStream(".", PipeName, PipeDirection.InOut);
            pipe.Connect(TimeoutMs);
            pipe.ReadMode = PipeTransmissionMode.Message;
            var json = JsonSerializer.Serialize(request, JsonOpts);
            var bytes = Encoding.UTF8.GetBytes(json);
            pipe.Write(bytes, 0, bytes.Length);

            using var ms = new MemoryStream();
            var buffer = new byte[65536];
            int read;
            do
            {
                read = pipe.Read(buffer, 0, buffer.Length);
                if (read > 0) ms.Write(buffer, 0, read);
            }
            while (read > 0 && !pipe.IsMessageComplete);
            var responseJson = Encoding.UTF8.GetString(ms.ToArray(), 0, (int)ms.Length);
            if (string.IsNullOrEmpty(responseJson))
                return new IpcResponse { Success = false, Error = "Empty response from service" };
            return JsonSerializer.Deserialize<IpcResponse>(responseJson, JsonOpts)
                ?? new IpcResponse { Success = false, Error = "Failed to parse response" };
        }
        catch (Exception ex)
        {
            return new IpcResponse { Success = false, Error = ex.Message };
        }
    }

    public async Task<IpcResponse> SendAsync(IpcRequest request)
    {
        try
        {
            using var pipe = new NamedPipeClientStream(".", PipeName, PipeDirection.InOut, PipeOptions.Asynchronous);
            await pipe.ConnectAsync(TimeoutMs);
            pipe.ReadMode = PipeTransmissionMode.Message;
            var json = JsonSerializer.Serialize(request, JsonOpts);
            var bytes = Encoding.UTF8.GetBytes(json);
            await pipe.WriteAsync(bytes, 0, bytes.Length);

            using var ms = new MemoryStream();
            var buffer = new byte[65536];
            int read;
            do
            {
                read = await pipe.ReadAsync(buffer, 0, buffer.Length);
                if (read > 0) ms.Write(buffer, 0, read);
            }
            while (read > 0 && !pipe.IsMessageComplete);
            var responseJson = Encoding.UTF8.GetString(ms.ToArray(), 0, (int)ms.Length);
            if (string.IsNullOrEmpty(responseJson))
                return new IpcResponse { Success = false, Error = "Empty response from service" };
            return JsonSerializer.Deserialize<IpcResponse>(responseJson, JsonOpts)
                ?? new IpcResponse { Success = false, Error = "Failed to parse response" };
        }
        catch (Exception ex)
        {
            return new IpcResponse { Success = false, Error = ex.Message };
        }
    }
}
