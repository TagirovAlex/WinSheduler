using System.Buffers.Binary;
using System.Text;
using Xunit;

namespace WinSheduler.ContractTests;

/// <summary>
/// Кадрирование IPC: 4 байта длины (little-endian) + JSON-тело (IPC.md, п. 1).
/// </summary>
public class IpcFrameTests
{
    [Fact]
    public void Кадр_КодируетсяИДекодируетсяБезПотерь()
    {
        var body = Encoding.UTF8.GetBytes("{\"v\":1,\"action\":\"Ping\"}");

        var frame = IpcFrame.Encode(body);
        Assert.Equal(IpcFrame.HeaderBytes + body.Length, frame.Length);

        Assert.True(IpcFrame.TryDecode(frame, out var decoded));
        Assert.Equal(body, decoded.ToArray());
    }

    [Fact]
    public void ДлинаКадра_ЗаписанаLittleEndian()
    {
        var body = new byte[0x0102];
        var frame = IpcFrame.Encode(body);

        Assert.Equal(0x02, frame[0]);
        Assert.Equal(0x01, frame[1]);
        Assert.Equal(0x00, frame[2]);
        Assert.Equal(0x00, frame[3]);
        Assert.Equal(0x0102u, BinaryPrimitives.ReadUInt32LittleEndian(frame));
    }

    [Fact]
    public void ПустоеТело_ДопустимоКакКадр()
    {
        var frame = IpcFrame.Encode(ReadOnlySpan<byte>.Empty);

        Assert.True(IpcFrame.TryDecode(frame, out var decoded));
        Assert.Empty(decoded.ToArray());
    }

    [Fact]
    public void КадрБольшеЛимита_ОтклоняетсяПриКодировании()
    {
        var body = new byte[IpcFrame.MaxFrameBytes + 1];

        Assert.Throws<ArgumentOutOfRangeException>(() => IpcFrame.Encode(body));
    }

    [Fact]
    public void ОбъявленнаяДлинаБольшеЛимита_ОтклоняетсяПриДекодировании()
    {
        var frame = new byte[IpcFrame.HeaderBytes];
        BinaryPrimitives.WriteUInt32LittleEndian(frame, (uint)IpcFrame.MaxFrameBytes + 1);

        Assert.False(IpcFrame.TryDecode(frame, out _));
    }

    [Fact]
    public void ОбрезанныйКадр_Отклоняется()
    {
        var body = Encoding.UTF8.GetBytes("{\"v\":1}");
        var frame = IpcFrame.Encode(body);

        Assert.False(IpcFrame.TryDecode(frame.AsSpan(0, frame.Length - 1), out _));
        Assert.False(IpcFrame.TryDecode(frame.AsSpan(0, 3), out _));
    }
}
