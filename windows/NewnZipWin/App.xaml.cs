using Microsoft.UI.Xaml;

namespace NewnZipWin;

public partial class App : Application
{
    private Window? window;

    public App()
    {
        InitializeComponent();
    }

    protected override async void OnLaunched(LaunchActivatedEventArgs args)
    {
        var command = ArchiveCommandService.Parse(Environment.GetCommandLineArgs().Skip(1));
        if (command.Kind != ArchiveCommandKind.None)
        {
            var result = await ArchiveCommandService.ExecuteAsync(command);
            ArchiveCommandService.RevealResult(result);
            Exit();
            return;
        }

        window = new MainWindow(command);
        window.Activate();
    }
}
