using Microsoft.UI.Windowing;
using Microsoft.UI.Xaml;
using Windows.ApplicationModel.DataTransfer;
using WinRT.Interop;

namespace NewnZipWin;

public sealed partial class DropOverlayWindow : Window
{
    private readonly Action<IReadOnlyList<string>> onDrop;
    private const int OverlayWidth = 56;

    public DropOverlayWindow(Action<IReadOnlyList<string>> onDrop)
    {
        this.onDrop = onDrop;
        InitializeComponent();
        Activated += (_, _) => ApplyCurrentSettings();
    }

    public void ApplyCurrentSettings()
    {
        var hwnd = WindowNative.GetWindowHandle(this);
        if (hwnd == IntPtr.Zero)
        {
            return;
        }

        var windowId = Win32Interop.GetWindowIdFromWindow(hwnd);
        var appWindow = AppWindow.GetFromWindowId(windowId);
        var displayArea = DisplayArea.GetFromWindowId(windowId, DisplayAreaFallback.Primary);
        var workArea = displayArea.WorkArea;

        if (appWindow.Presenter is OverlappedPresenter presenter)
        {
            presenter.IsAlwaysOnTop = true;
            presenter.IsResizable = false;
            presenter.IsMaximizable = false;
            presenter.IsMinimizable = false;
            presenter.SetBorderAndTitleBar(false, false);
        }

        var x = AppPreferences.DragOverlayDockSide == DragOverlayDockSide.Left
            ? workArea.X
            : workArea.X + workArea.Width - OverlayWidth;

        appWindow.MoveAndResize(new Windows.Graphics.RectInt32(x, workArea.Y, OverlayWidth, workArea.Height));
    }

    private void OverlayRootDragOver(object sender, DragEventArgs e)
    {
        e.AcceptedOperation = e.DataView.Contains(StandardDataFormats.StorageItems)
            ? DataPackageOperation.Copy
            : DataPackageOperation.None;
    }

    private async void OverlayRootDrop(object sender, DragEventArgs e)
    {
        if (!e.DataView.Contains(StandardDataFormats.StorageItems))
        {
            return;
        }

        var items = await e.DataView.GetStorageItemsAsync();
        var paths = items
            .OfType<Windows.Storage.IStorageItem>()
            .Select(item => item.Path)
            .Where(path => !string.IsNullOrWhiteSpace(path))
            .ToArray();

        if (paths.Length > 0)
        {
            onDrop(paths);
        }
    }
}
