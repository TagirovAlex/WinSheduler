using System.Runtime.InteropServices;

namespace WinSheduler.UI.Services;

/// <summary>
/// Проверка Authenticode-подписи через <c>WinVerifyTrust</c>. Отдельный класс,
/// потому что для проверки MSI недостаточно <see cref="System.Security.Cryptography.X509Certificates.X509Certificate"/>:
/// нужно доверие к цепочке, а не только наличие подписи.
/// </summary>
internal static class AuthenticodeVerifier
{
    private const uint WtdUiNone = 2;
    private const uint WtdRevokeNone = 0;
    private const uint WtdChoiceFile = 1;
    private const uint WtdStateActionVerify = 0;
    private const uint WtdStateActionClose = 2;
    private const uint WtdCacheOnlyUrlRetrieval = 0x1000;

    private static readonly Guid ActionVerifyV2 =
        new("00AAC56B-CD44-11d0-8CC2-00C04FC295EE");

    /// <summary>Возвращает <c>true</c>, если подпись и цепочка доверенные.</summary>
    public static bool Verify(string filePath)
    {
        var fileInfo = new WinTrustFileInfo
        {
            cbStruct = (uint)Marshal.SizeOf<WinTrustFileInfo>(),
            pcwszFilePath = Marshal.StringToHGlobalUni(filePath),
        };

        var fileInfoPtr = Marshal.AllocHGlobal(Marshal.SizeOf<WinTrustFileInfo>());
        var dataPtr = Marshal.AllocHGlobal(Marshal.SizeOf<WinTrustData>());

        try
        {
            Marshal.StructureToPtr(fileInfo, fileInfoPtr, false);

            var data = new WinTrustData
            {
                cbStruct = (uint)Marshal.SizeOf<WinTrustData>(),
                dwUIChoice = WtdUiNone,
                fdwRevocationChecks = WtdRevokeNone,
                dwUnionChoice = WtdChoiceFile,
                pFile = fileInfoPtr,
                dwStateAction = WtdStateActionVerify,
                dwProvFlags = WtdCacheOnlyUrlRetrieval,
            };

            Marshal.StructureToPtr(data, dataPtr, false);
            var result = WinVerifyTrust(IntPtr.Zero, ActionVerifyV2, dataPtr);

            // Освобождаем состояние проверки.
            data.dwStateAction = WtdStateActionClose;
            Marshal.StructureToPtr(data, dataPtr, false);
            WinVerifyTrust(IntPtr.Zero, ActionVerifyV2, dataPtr);

            return result == 0;
        }
        finally
        {
            Marshal.FreeHGlobal(fileInfo.pcwszFilePath);
            Marshal.FreeHGlobal(fileInfoPtr);
            Marshal.FreeHGlobal(dataPtr);
        }
    }

    [DllImport("wintrust.dll", PreserveSig = true, SetLastError = false)]
    private static extern uint WinVerifyTrust(
        IntPtr hwnd,
        [MarshalAs(UnmanagedType.LPStruct)] Guid pgActionID,
        IntPtr pWVTData);

    [StructLayout(LayoutKind.Sequential)]
    private struct WinTrustFileInfo
    {
        public uint cbStruct;
        public IntPtr pcwszFilePath;
        public IntPtr hFile;
        public IntPtr pgKnownSubject;
    }

    [StructLayout(LayoutKind.Sequential)]
    private struct WinTrustData
    {
        public uint cbStruct;
        public IntPtr pPolicyCallbackData;
        public IntPtr pSIPClientData;
        public uint dwUIChoice;
        public uint fdwRevocationChecks;
        public uint dwUnionChoice;
        public IntPtr pFile;
        public uint dwStateAction;
        public IntPtr hWVTStateData;
        public IntPtr pwszURLReference;
        public uint dwProvFlags;
        public uint dwUIContext;
        public IntPtr pSignatureSettings;
    }
}
