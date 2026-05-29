using Microsoft.UI.Xaml;

namespace NewnZipWin;

public partial class App : Application
{
    private Window? window;
    private DropOverlayWindow? overlayWindow;

    public App()
    {
        InitializeComponent();
        AppPreferences.Changed += RefreshDropOverlayWindow;
    }

    protected override async void OnLaunched(LaunchActivatedEventArgs args)
    {
        var command = ArchiveCommandService.Parse(Environment.GetCommandLineArgs().Skip(1));
        if (command.Kind != ArchiveCommandKind.None)
        {
            window = new HudWindow(command);
            window.Activate();
            await ((HudWindow)window).RunAsync();
            return;
        }

        window = new MainWindow(command);
        window.Activate();
        RefreshDropOverlayWindow();
    }

    private void RefreshDropOverlayWindow()
    {
        if (!AppPreferences.DragOverlayEnabled)
        {
            overlayWindow?.Close();
            overlayWindow = null;
            return;
        }

        if (overlayWindow is null)
        {
            overlayWindow = new DropOverlayWindow(HandleOverlayDrop);
            overlayWindow.Closed += (_, _) => overlayWindow = null;
            overlayWindow.Activate();
        }

        overlayWindow.ApplyCurrentSettings();
    }

    private async void HandleOverlayDrop(IReadOnlyList<string> paths)
    {
        var command = ArchiveCommandService.FromDroppedPaths(paths);
        if (command.Kind == ArchiveCommandKind.None)
        {
            return;
        }

        var hudWindow = new HudWindow(command, exitApplicationWhenDone: false);
        hudWindow.Activate();
        await hudWindow.RunAsync();
    }
}
