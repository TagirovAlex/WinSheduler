using System.Diagnostics;
using System.Globalization;
using System.IO;
using System.Security.Cryptography;
using System.Text;
using System.Text.Json;
using Microsoft.Win32;

namespace WinSheduler.UI.Services;

/// <summary>Манифест обновления <c>latest.json</c> на шаре.</summary>
public sealed class UpdateManifest
{
    public string? Version { get; set; }
    public string? Url { get; set; }
    public string? Sha256 { get; set; }
    public long Size { get; set; }
}

/// <summary>Результат проверки обновления.</summary>
public enum UpdateCheckStatus
{
    NotConfigured,
    UpToDate,
    Available,
    Error,
}

/// <summary>Данные проверки обновления.</summary>
public sealed record UpdateCheckResult(
    UpdateCheckStatus Status,
    string? Version = null,
    string? Url = null,
    string? Sha256 = null,
    long Size = 0,
    string? Error = null);

/// <summary>
/// Проверка и установка обновлений по адресу из
/// <c>HKLM\SOFTWARE\WinSheduler\UpdateUrl</c>. Адрес задаётся при сборке MSI;
/// если он пуст, пункт меню скрыт.
/// </summary>
public sealed class UpdateService
{
    private const string RegistryKeyPath = @"SOFTWARE\WinSheduler";
    private const string RegistryValueName = "UpdateUrl";
    private const string DataDirValueName = "DataDir";

    private static readonly JsonSerializerOptions JsonOptions = new()
    {
        PropertyNameCaseInsensitive = true,
    };

    private readonly Func<string?> _readUpdateUrl;

    public UpdateService()
        : this(() => ReadRegistryValue(RegistryValueName))
    {
    }

    /// <summary>Конструктор для тестов: адрес обновлений задаётся делегатом.</summary>
    public UpdateService(Func<string?> readUpdateUrl) => _readUpdateUrl = readUpdateUrl;

    /// <summary>Текущая версия UI в формате <c>x.y.z</c>.</summary>
    public static string CurrentVersion =>
        typeof(UpdateService).Assembly.GetName().Version?.ToString(3) ?? "0.0.0";

    /// <summary>Каталог загрузки обновлений.</summary>
    public static string UpdatesRoot => Path.Combine(
        Environment.GetFolderPath(Environment.SpecialFolder.CommonApplicationData),
        "WinSheduler",
        "updates");

    /// <summary>Журнал последней установки.</summary>
    public static string InstallLogPath => Path.Combine(UpdatesRoot, "last-install.log");

    /// <summary>Код последней установки; читается при следующем запуске UI.</summary>
    public static string InstallResultPath => Path.Combine(UpdatesRoot, "last-install-result.txt");

    /// <summary>Адрес источника обновлений или <c>null</c>.</summary>
    public string? UpdateUrl
    {
        get
        {
            var value = _readUpdateUrl();
            return string.IsNullOrWhiteSpace(value) ? null : value.Trim();
        }
    }

    /// <summary>Настроен ли источник обновлений при сборке/установке.</summary>
    public bool IsConfigured => UpdateUrl is not null;

    /// <summary>Читает манифест и сравнивает его версию с текущей.</summary>
    public UpdateCheckResult Check()
    {
        var baseUrl = UpdateUrl;
        if (baseUrl is null)
        {
            return new UpdateCheckResult(UpdateCheckStatus.NotConfigured);
        }

        try
        {
            var json = File.ReadAllText(Path.Combine(baseUrl, "latest.json"));
            return Evaluate(json, CurrentVersion);
        }
        catch (Exception ex)
        {
            return new UpdateCheckResult(UpdateCheckStatus.Error, Error: ex.Message);
        }
    }

    /// <summary>Разбирает манифест, проверяет поля и сравнивает версии.</summary>
    public static UpdateCheckResult Evaluate(string json, string currentVersion)
    {
        var manifest = JsonSerializer.Deserialize<UpdateManifest>(json, JsonOptions)
            ?? throw new JsonException("Пустой манифест обновления.");

        if (!Version.TryParse(manifest.Version, out var remote) ||
            !Version.TryParse(currentVersion, out var current))
        {
            throw new JsonException("В манифесте нет корректной версии.");
        }

        if (!IsSha256(manifest.Sha256))
        {
            throw new JsonException("В манифесте нет корректного SHA256.");
        }

        if (manifest.Size <= 0 || !IsUncPath(manifest.Url))
        {
            throw new JsonException("В манифесте нет размера или корректного UNC-пути к MSI.");
        }

        var status = Normalize(remote) > Normalize(current)
            ? UpdateCheckStatus.Available
            : UpdateCheckStatus.UpToDate;

        return new UpdateCheckResult(
            status,
            manifest.Version,
            manifest.Url,
            manifest.Sha256,
            manifest.Size);
    }

    /// <summary>Форматирует размер обновления для диалога.</summary>
    public static string FormatSize(long bytes) => bytes <= 0
        ? "размер неизвестен"
        : string.Format(CultureInfo.GetCultureInfo("ru-RU"), "{0:N1} МБ", bytes / 1024d / 1024d);

    /// <summary>
    /// Копирует MSI в защищённый локальный каталог, проверяет SHA256 и
    /// Authenticode-подпись. Возвращает путь к локальному MSI.
    /// </summary>
    public string PrepareForInstall(UpdateCheckResult result)
    {
        if (result.Url is null || result.Version is null || result.Sha256 is null)
        {
            throw new InvalidOperationException("Неполные данные обновления.");
        }

        var directory = Path.Combine(UpdatesRoot, result.Version);
        Directory.CreateDirectory(directory);

        var localPath = Path.Combine(directory, $"WinSheduler-{result.Version}.msi");
        File.Copy(result.Url, localPath, overwrite: true);

        string hash;
        using (var stream = File.OpenRead(localPath))
        {
            hash = Convert.ToHexString(SHA256.HashData(stream));
        }

        if (!string.Equals(hash, result.Sha256, StringComparison.OrdinalIgnoreCase))
        {
            File.Delete(localPath);
            throw new InvalidOperationException("SHA256 скачанного MSI не совпадает с манифестом.");
        }

        if (!AuthenticodeVerifier.Verify(localPath))
        {
            File.Delete(localPath);
            throw new InvalidOperationException("Подпись MSI недействительна или не доверена.");
        }

        return localPath;
    }

    /// <summary>
    /// Копирует БД и WAL-файлы на шару перед установкой. Возвращает каталог копии.
    /// </summary>
    public static string BackupDatabase(string shareUrl, string version)
    {
        var dataDir = ReadRegistryValue(DataDirValueName)
            ?? Path.Combine(
                Environment.GetFolderPath(Environment.SpecialFolder.CommonApplicationData),
                "WinSheduler");

        var database = Path.Combine(dataDir, "data", "scheduler.db");
        if (!File.Exists(database))
        {
            throw new FileNotFoundException("База данных не найдена.", database);
        }

        var target = Path.Combine(
            shareUrl,
            "backups",
            Environment.MachineName,
            $"{DateTime.UtcNow:yyyyMMdd-HHmmss}-{version}");
        Directory.CreateDirectory(target);

        foreach (var suffix in new[] { string.Empty, "-wal", "-shm" })
        {
            var source = database + suffix;
            if (File.Exists(source))
            {
                File.Copy(source, Path.Combine(target, Path.GetFileName(source)), overwrite: true);
            }
        }

        return target;
    }

    /// <summary>
    /// Запускает helper: тихая установка MSI, запись кода результата и
    /// перезапуск UI. Текущий UI должен завершиться сразу после вызова.
    /// </summary>
    public static void StartInstall(string msiPath, string uiPath)
    {
        var directory = Path.GetDirectoryName(msiPath)
            ?? throw new InvalidOperationException("Не удалось определить каталог MSI.");
        var script = Path.Combine(directory, "install.cmd");

        File.WriteAllText(
            script,
            $"""
            @echo off
            msiexec /i "{msiPath}" /qn /norestart /l*v "{InstallLogPath}"
            > "{InstallResultPath}" echo %ERRORLEVEL%
            start "" "{uiPath}"
            """,
            Encoding.Default);

        Process.Start(new ProcessStartInfo("cmd.exe", $"/c \"\"{script}\"\"")
        {
            UseShellExecute = false,
            CreateNoWindow = true,
            WorkingDirectory = directory,
        });
    }

    /// <summary>Читает и удаляет код последней установки.</summary>
    public static int? ReadInstallResult()
    {
        try
        {
            if (!File.Exists(InstallResultPath))
            {
                return null;
            }

            var text = File.ReadAllText(InstallResultPath).Trim();
            File.Delete(InstallResultPath);
            return int.TryParse(text, out var code) ? code : null;
        }
        catch (Exception ex) when (ex is IOException or UnauthorizedAccessException)
        {
            return null;
        }
    }

    private static string? ReadRegistryValue(string name)
    {
        try
        {
            using var baseKey = RegistryKey.OpenBaseKey(RegistryHive.LocalMachine, RegistryView.Registry64);
            using var key = baseKey.OpenSubKey(RegistryKeyPath);
            return key?.GetValue(name) as string;
        }
        catch (Exception ex) when (ex is UnauthorizedAccessException or System.Security.SecurityException)
        {
            return null;
        }
    }

    private static bool IsSha256(string? value)
        => value is { Length: 64 } && value.All(Uri.IsHexDigit);

    private static bool IsUncPath(string? value)
    {
        if (string.IsNullOrWhiteSpace(value))
        {
            return false;
        }

        var normalized = value.Replace('\\', '/');
        return normalized.StartsWith("//", StringComparison.Ordinal) &&
               !normalized.Split('/').Contains("..");
    }

    /// <summary>Сравниваем только major.minor.build: ревизия в манифесте не нужна.</summary>
    private static Version Normalize(Version version)
        => new(version.Major, version.Minor, Math.Max(version.Build, 0));
}
