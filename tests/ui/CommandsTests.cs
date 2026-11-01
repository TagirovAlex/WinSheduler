using WinSheduler.UI.ViewModels;
using Xunit;

namespace WinSheduler.UI.Tests;

/// <summary>D-1: команды — доступность, защита от повторного входа, отмена.</summary>
public class CommandsTests
{
    [Fact]
    public void RelayCommand_УчитываетCanExecuteИПоднимаетСобытие()
    {
        var canExecute = false;
        var executed = 0;
        var command = new RelayCommand(() => executed++, () => canExecute);
        var raised = 0;
        command.CanExecuteChanged += (_, _) => raised++;

        Assert.False(command.CanExecute(null));

        canExecute = true;
        command.RaiseCanExecuteChanged();
        command.Execute(null);

        Assert.Equal(1, executed);
        Assert.Equal(1, raised);
        Assert.True(command.CanExecute(null));
    }

    [Fact]
    public async Task AsyncCommand_ЗащищаетОтПовторногоВхода()
    {
        var gate = new TaskCompletionSource(TaskCreationOptions.RunContinuationsAsynchronously);
        var executions = 0;
        var command = new AsyncCommand(async _ =>
        {
            Interlocked.Increment(ref executions);
            await gate.Task;
        });

        var first = command.ExecuteAsync();
        var second = command.ExecuteAsync();

        Assert.True(command.IsRunning);
        Assert.False(command.CanExecute(null));

        gate.SetResult();
        await Task.WhenAll(first, second);

        Assert.Equal(1, executions);
        Assert.False(command.IsRunning);
        Assert.True(command.CanExecute(null));
    }

    [Fact]
    public async Task AsyncCommand_ОтменаЗавершаетКомандуБезОшибки()
    {
        var started = new TaskCompletionSource(TaskCreationOptions.RunContinuationsAsynchronously);
        var errors = new List<Exception>();
        var command = new AsyncCommand(
            async token =>
            {
                started.SetResult();
                await Task.Delay(Timeout.Infinite, token);
            },
            onError: errors.Add);

        var execution = command.ExecuteAsync();
        await started.Task;

        command.Cancel();
        await execution;

        Assert.Empty(errors);
        Assert.False(command.IsRunning);
    }

    [Fact]
    public async Task AsyncCommand_ОшибкаПередаётсяОбработчику()
    {
        var errors = new List<Exception>();
        var command = new AsyncCommand(
            () => throw new InvalidOperationException("Сбой"),
            onError: errors.Add);

        await command.ExecuteAsync();

        Assert.Single(errors);
        Assert.IsType<InvalidOperationException>(errors[0]);
    }

    [Fact]
    public async Task AsyncCommandСПараметром_ПередаётЗначение()
    {
        int? received = null;
        var command = new AsyncCommand<int>((value, _) =>
        {
            received = value;
            return Task.CompletedTask;
        });

        await command.ExecuteAsync(42);

        Assert.Equal(42, received);
    }
}
