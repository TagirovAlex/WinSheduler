using System.Buffers.Binary;

namespace WinSheduler.ContractTests;

/// <summary>
/// Эталонная реализация кадрирования IPC из ./docs/contracts/IPC.md, п. 1.
/// Нужна для проверки контракта на стороне C#: 4 байта длины (little-endian)
/// плюс JSON-тело, максимум 1 МиБ.
/// </summary>
public static class IpcFrame
{
    /// <summary>Размер заголовка: uint32 little-endian.</summary>
    public const int HeaderBytes = 4;

    /// <summary>Максимальный размер кадра — 1 МиБ (IPC.md, п. 1).</summary>
    public const int MaxFrameBytes = 1024 * 1024;

    /// <summary>Максимальная глубина JSON (IPC.md, п. 1).</summary>
    public const int MaxJsonDepth = 64;

    /// <summary>Максимальная длина строки — 64 КиБ (IPC.md, п. 1).</summary>
    public const int MaxStringBytes = 64 * 1024;

    public static byte[] Encode(ReadOnlySpan<byte> body)
    {
        if (body.Length > MaxFrameBytes)
        {
            throw new ArgumentOutOfRangeException(
                nameof(body), body.Length, "Тело кадра больше 1 МиБ.");
        }

        var frame = new byte[HeaderBytes + body.Length];
        BinaryPrimitives.WriteUInt32LittleEndian(frame, (uint)body.Length);
        body.CopyTo(frame.AsSpan(HeaderBytes));
        return frame;
    }

    public static bool TryDecode(ReadOnlySpan<byte> frame, out ReadOnlySpan<byte> body)
    {
        body = default;

        if (frame.Length < HeaderBytes)
        {
            return false;
        }

        var length = BinaryPrimitives.ReadUInt32LittleEndian(frame);
        if (length > MaxFrameBytes)
        {
            return false;
        }

        if (frame.Length - HeaderBytes < length)
        {
            return false;
        }

        body = frame.Slice(HeaderBytes, (int)length);
        return true;
    }
}
