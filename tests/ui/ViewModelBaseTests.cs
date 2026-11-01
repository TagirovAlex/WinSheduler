using System.ComponentModel.DataAnnotations;
using WinSheduler.UI.ViewModels;
using Xunit;

namespace WinSheduler.UI.Tests;

/// <summary>D-1: базовый ViewModel — уведомления и валидация.</summary>
public class ViewModelBaseTests
{
    [Fact]
    public void SetProperty_ПоднимаетPropertyChangedТолькоПриИзменении()
    {
        var viewModel = new SampleViewModel();
        var changes = new List<string?>();
        viewModel.PropertyChanged += (_, e) => changes.Add(e.PropertyName);

        viewModel.Title = "Задача";
        viewModel.Title = "Задача";

        Assert.Single(changes);
        Assert.Equal(nameof(SampleViewModel.Title), changes[0]);
    }

    [Fact]
    public void ValidateProperty_ОбязательноеПоле_ДаётОшибкуИСобытие()
    {
        var viewModel = new SampleViewModel { Name = "Черновик" };
        var errorsChanged = new List<string?>();
        viewModel.ErrorsChanged += (_, e) => errorsChanged.Add(e.PropertyName);

        viewModel.Name = string.Empty;

        Assert.True(viewModel.HasErrors);
        Assert.Contains(nameof(SampleViewModel.Name), errorsChanged);
        Assert.NotEmpty(viewModel.GetErrors(nameof(SampleViewModel.Name)).Cast<string>());
    }

    [Fact]
    public void ValidateProperty_КорректноеЗначение_СнимаетОшибку()
    {
        var viewModel = new SampleViewModel { Name = "Черновик" };
        viewModel.Name = string.Empty;
        Assert.True(viewModel.HasErrors);

        viewModel.Name = "Ночной бэкап";

        Assert.False(viewModel.HasErrors);
        Assert.Empty(viewModel.GetErrors(nameof(SampleViewModel.Name)).Cast<string>());
    }

    [Fact]
    public void AddError_ИClearErrors_УправляютНаборомОшибок()
    {
        var viewModel = new SampleViewModel();

        viewModel.SetManualError("Недостаточно прав");
        Assert.True(viewModel.HasErrors);

        viewModel.ResetErrors();
        Assert.False(viewModel.HasErrors);
    }

    private sealed class SampleViewModel : ViewModelBase
    {
        private string _name = string.Empty;
        private string _title = string.Empty;

        [Required(ErrorMessage = "Обязательное поле.")]
        public string Name
        {
            get => _name;
            set => SetPropertyAndValidate(ref _name, value);
        }

        public string Title
        {
            get => _title;
            set => SetProperty(ref _title, value);
        }

        public void SetManualError(string error) => AddError(nameof(Name), error);

        public void ResetErrors() => ClearErrors();
    }
}
