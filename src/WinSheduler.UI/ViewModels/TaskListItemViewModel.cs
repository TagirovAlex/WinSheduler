using WinSheduler.UI.Services;

namespace WinSheduler.UI.ViewModels;

/// <summary>
/// Строка списка задач. Обновляется на месте (<see cref="Apply"/>), чтобы
/// не сбрасывать ItemsSource и сохранять выделение (контракт UI.md, раздел 8).
/// </summary>
public sealed class TaskListItemViewModel : ViewModelBase
{
    public TaskListItemViewModel(DashboardTaskDto task)
    {
        Id = task.Id;
        Apply(task);
    }

    public Guid Id { get; }

    public string Name { get; private set; } = string.Empty;

    public string ProgramPath { get; private set; } = string.Empty;

    public bool Enabled { get; private set; }

    public bool IsOwn { get; private set; }

    public string OwnerLabel { get; private set; } = string.Empty;

    public string ScheduleSummary { get; private set; } = string.Empty;

    public DashboardLastRunDto? LastRun { get; private set; }

    public bool Running { get; private set; }

    /// <summary>Копирует изменяемые поля из ответа службы.</summary>
    public void Apply(DashboardTaskDto task)
    {
        Name = task.Name;
        ProgramPath = task.ProgramPath;
        Enabled = task.Enabled;
        IsOwn = task.IsOwn;
        OwnerLabel = task.OwnerLabel;
        ScheduleSummary = task.ScheduleSummary;
        LastRun = task.LastRun;
        Running = task.Running;

        // Одно уведомление на все поля: строка списка перерисовывается целиком.
        OnPropertiesChanged(
            nameof(Name),
            nameof(ProgramPath),
            nameof(Enabled),
            nameof(IsOwn),
            nameof(OwnerLabel),
            nameof(ScheduleSummary),
            nameof(LastRun),
            nameof(Running));
    }
}
