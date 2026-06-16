namespace WinSheduler.Shared.Models;

public class RunAsCredentials
{
    public string UserName { get; set; } = "";
    public string Domain { get; set; } = "";
    public string EncryptedPassword { get; set; } = "";
}
