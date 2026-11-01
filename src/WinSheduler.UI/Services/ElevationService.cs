using System.ComponentModel;
using System.Diagnostics;
using System.Security.Principal;
using WinSheduler.Shared;
using WinSheduler.UI.Localization;

namespace WinSheduler.UI.Services;

/// <summary>
/// Состояние прав пользователя и перезапуск с повышением.
///
/// Роль определяется службой (<see cref="ServiceStatus.IsAdmin"/>): неэлевированный
/// администратор имеет filtered token и обрабатывается как оператор (контракт
/// SECURITY.md, раздел 2). Локальная проверка процесса нужна только для того,
/// чтобы скрыть привилегированные действия до получения статуса.
/// </summary>
public interface IElevationService
{
    /// <summary>Служба подтвердила права администратора.</summary>
    bool IsAdmin { get; }

    /// <summary>Пользователь входит в группу операторов.</summary>
    bool IsOperator { get; }

    /// <summary>Разрешено управление системными задачами (SYSTEM/RunAs).</summary>
    bool CanManageSystemTasks { get; }

    /// <summary>Процесс запущен с повышенными правами (не filtered token).</summary>
    bool IsProcessElevated { get; }

    /// <summary>Подпись роли для статус-бара: «Администратор» или «Оператор».</summary>
    string RoleText { get; }

    /// <summary>Роль или доступность привилегированных действий изменились.</summary>
    event EventHandler? StateChanged;

    /// <summary>Обновляет состояние по ответу <c>GetStatus</c>.</summary>
    void Update(ServiceStatus? status);

    /// <summary>
    /// Перезапускает приложение с правами администратора (<c>runas</c>).
    /// При успехе текущий процесс завершается. Возвращает <c>false</c>,
    /// если пользователь отменил UAC или перезапуск невозможен.
    /// </summary>
    bool RelaunchAsAdmin();
}

/// <summary>Реализация <see cref="IElevationService"/>.</summary>
public sealed class ElevationService : IElevationService
{
    private bool _isAdmin;
    private bool _isOperator;
    private bool _canManageSystemTasks;

    public ElevationService()
    {
        IsProcessElevated = CheckProcessElevated();
    }

    /// <inheritdoc />
    public bool IsAdmin => _isAdmin;

    /// <inheritdoc />
    public bool IsOperator => _isOperator;

    /// <inheritdoc />
    public bool CanManageSystemTasks => _canManageSystemTasks;

    /// <inheritdoc />
    public bool IsProcessElevated { get; }

    /// <inheritdoc />
    public string RoleText => Strings.Get(_isAdmin ? "Status_Administrator" : "Status_Operator");

    /// <inheritdoc />
    public event EventHandler? StateChanged;

    /// <inheritdoc />
    public void Update(ServiceStatus? status)
    {
        var isAdmin = status?.IsAdmin == true;
        var isOperator = status?.IsOperator == true || isAdmin;
        var canManageSystemTasks = status?.CanManageSystemTasks == true;

        if (isAdmin == _isAdmin && isOperator == _isOperator && canManageSystemTasks == _canManageSystemTasks)
        {
            return;
        }

        _isAdmin = isAdmin;
        _isOperator = isOperator;
        _canManageSystemTasks = canManageSystemTasks;
        StateChanged?.Invoke(this, EventArgs.Empty);
    }

    /// <inheritdoc />
    public bool RelaunchAsAdmin()
    {
        var executable = Environment.ProcessPath;
        if (string.IsNullOrEmpty(executable))
        {
            return false;
        }

        try
        {
            var startInfo = new ProcessStartInfo
            {
                FileName = executable,
                UseShellExecute = true,
                Verb = "runas",
                WorkingDirectory = AppContext.BaseDirectory,
            };

            if (Process.Start(startInfo) is null)
            {
                return false;
            }
        }
        catch (Win32Exception)
        {
            // Пользователь отменил запрос UAC или повышение запрещено политикой.
            return false;
        }

        System.Windows.Application.Current?.Shutdown();
        return true;
    }

    private static bool CheckProcessElevated()
    {
        try
        {
            using var identity = WindowsIdentity.GetCurrent();
            var principal = new WindowsPrincipal(identity);
            return principal.IsInRole(WindowsBuiltInRole.Administrator);
        }
        catch (Exception ex) when (ex is UnauthorizedAccessException or PlatformNotSupportedException)
        {
            return false;
        }
    }
}
