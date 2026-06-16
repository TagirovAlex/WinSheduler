using System.Security.Cryptography;
using System.Text;

namespace WinSheduler.Shared;

public static class CryptoHelper
{
    public static string Encrypt(string plainText)
    {
        var bytes = Encoding.UTF8.GetBytes(plainText);
        var encrypted = ProtectedData.Protect(bytes, null, DataProtectionScope.LocalMachine);
        Array.Clear(bytes);
        return Convert.ToBase64String(encrypted);
    }

    public static string Decrypt(string encryptedBase64)
    {
        var encrypted = Convert.FromBase64String(encryptedBase64);
        var bytes = ProtectedData.Unprotect(encrypted, null, DataProtectionScope.LocalMachine);
        var result = Encoding.UTF8.GetString(bytes);
        Array.Clear(bytes);
        return result;
    }
}
