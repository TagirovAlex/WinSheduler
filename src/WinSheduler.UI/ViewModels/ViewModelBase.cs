using System.Collections;
using System.ComponentModel;
using System.ComponentModel.DataAnnotations;
using System.Runtime.CompilerServices;

namespace WinSheduler.UI.ViewModels;

/// <summary>
/// Базовый класс ViewModel: уведомления об изменении свойств и валидация
/// через <see cref="INotifyDataErrorInfo"/>.
///
/// Валидация поддерживает два способа:
/// 1) атрибуты <see cref="ValidationAttribute"/> на свойстве — через <see cref="ValidateProperty"/>;
/// 2) ручные ошибки — через <see cref="AddError"/> и <see cref="ClearErrors"/>.
/// </summary>
public abstract class ViewModelBase : INotifyPropertyChanged, INotifyDataErrorInfo
{
    private readonly Dictionary<string, List<string>> _errors = new(StringComparer.Ordinal);

    /// <inheritdoc />
    public event PropertyChangedEventHandler? PropertyChanged;

    /// <inheritdoc />
    public event EventHandler<DataErrorsChangedEventArgs>? ErrorsChanged;

    /// <inheritdoc />
    public bool HasErrors => _errors.Count > 0;

    /// <inheritdoc />
    public IEnumerable GetErrors(string? propertyName)
    {
        if (string.IsNullOrEmpty(propertyName))
        {
            // Пустое имя — все ошибки сразу (требование контракта INotifyDataErrorInfo).
            return _errors.Values.SelectMany(static list => list).ToArray();
        }

        return _errors.TryGetValue(propertyName, out var errors) ? errors : Array.Empty<string>();
    }

    /// <summary>Возвращает ошибки конкретного свойства.</summary>
    public IReadOnlyList<string> GetErrorsFor(string propertyName)
        => _errors.TryGetValue(propertyName, out var errors) ? errors : [];

    /// <summary>Поднимает <see cref="PropertyChanged"/>.</summary>
    protected void OnPropertyChanged([CallerMemberName] string? propertyName = null)
        => PropertyChanged?.Invoke(this, new PropertyChangedEventArgs(propertyName));

    /// <summary>Поднимает <see cref="PropertyChanged"/> для нескольких свойств.</summary>
    protected void OnPropertiesChanged(params string[] propertyNames)
    {
        foreach (var propertyName in propertyNames)
        {
            OnPropertyChanged(propertyName);
        }
    }

    /// <summary>
    /// Присваивает значение и уведомляет об изменении, если значение отличается.
    /// Возвращает <c>true</c>, если значение изменилось.
    /// </summary>
    protected bool SetProperty<T>(ref T field, T value, [CallerMemberName] string? propertyName = null)
    {
        if (EqualityComparer<T>.Default.Equals(field, value))
        {
            return false;
        }

        field = value;
        OnPropertyChanged(propertyName);
        return true;
    }

    /// <summary>
    /// Присваивает значение, уведомляет об изменении и валидирует свойство
    /// по атрибутам <see cref="ValidationAttribute"/>.
    /// </summary>
    protected bool SetPropertyAndValidate<T>(ref T field, T value, [CallerMemberName] string? propertyName = null)
    {
        if (!SetProperty(ref field, value, propertyName))
        {
            return false;
        }

        ValidateProperty(value, propertyName);
        return true;
    }

    /// <summary>
    /// Валидирует значение по атрибутам <see cref="ValidationAttribute"/> на свойстве.
    /// Найденные ошибки заменяют прежние ошибки этого свойства.
    /// </summary>
    protected bool ValidateProperty(object? value, [CallerMemberName] string? propertyName = null)
    {
        if (string.IsNullOrEmpty(propertyName))
        {
            return !HasErrors;
        }

        var results = new List<ValidationResult>();
        var context = new ValidationContext(this) { MemberName = propertyName };
        var isValid = Validator.TryValidateProperty(value, context, results);

        SetErrors(propertyName, results.Select(static r => r.ErrorMessage ?? "Недопустимое значение"));
        return isValid;
    }

    /// <summary>Валидирует все свойства объекта по атрибутам.</summary>
    public bool ValidateAll()
    {
        var results = new List<ValidationResult>();
        var context = new ValidationContext(this);
        var isValid = Validator.TryValidateObject(this, context, results, validateAllProperties: true);

        ClearErrors();
        foreach (var group in results.GroupBy(static r => r.MemberNames.FirstOrDefault() ?? string.Empty))
        {
            SetErrors(group.Key, group.Select(static r => r.ErrorMessage ?? "Недопустимое значение"));
        }

        return isValid;
    }

    /// <summary>Добавляет ошибку свойству.</summary>
    protected void AddError(string propertyName, string error)
    {
        if (!_errors.TryGetValue(propertyName, out var errors))
        {
            errors = [];
            _errors[propertyName] = errors;
        }

        if (errors.Contains(error, StringComparer.Ordinal))
        {
            return;
        }

        errors.Add(error);
        OnErrorsChanged(propertyName);
    }

    /// <summary>Заменяет набор ошибок свойства.</summary>
    protected void SetErrors(string propertyName, IEnumerable<string> errors)
    {
        var list = errors.Where(static e => !string.IsNullOrWhiteSpace(e)).Distinct(StringComparer.Ordinal).ToList();

        if (list.Count == 0)
        {
            if (_errors.Remove(propertyName))
            {
                OnErrorsChanged(propertyName);
            }

            return;
        }

        _errors[propertyName] = list;
        OnErrorsChanged(propertyName);
    }

    /// <summary>Очищает ошибки одного свойства или все ошибки, если имя не задано.</summary>
    protected void ClearErrors(string? propertyName = null)
    {
        if (string.IsNullOrEmpty(propertyName))
        {
            if (_errors.Count == 0)
            {
                return;
            }

            var names = _errors.Keys.ToArray();
            _errors.Clear();
            foreach (var name in names)
            {
                OnErrorsChanged(name);
            }

            return;
        }

        if (_errors.Remove(propertyName))
        {
            OnErrorsChanged(propertyName);
        }
    }

    private void OnErrorsChanged(string propertyName)
    {
        ErrorsChanged?.Invoke(this, new DataErrorsChangedEventArgs(propertyName));
        OnPropertyChanged(nameof(HasErrors));
    }
}
