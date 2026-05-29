using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using Windows.ApplicationModel.DataTransfer;

namespace NewnZipWin;

public sealed partial class MainWindow : Window
{
    private readonly ArchiveCommand initialCommand;
    private bool updatingDefaultArchiveToggle;
    private bool updatingConflictPolicyBox;
    private bool updatingArchivePassword;
    private bool updatingDragOverlayToggle;
    private bool updatingDragOverlayDockSideBox;

    public MainWindow(ArchiveCommand initialCommand)
    {
        this.initialCommand = initialCommand;
        InitializeComponent();

        if (initialCommand.Kind != ArchiveCommandKind.None)
        {
            StatusText.Text = initialCommand.Kind switch
            {
                ArchiveCommandKind.Compress => "선택한 항목을 압축할 준비 중입니다.",
                ArchiveCommandKind.SplitCompress => "선택한 항목을 분할 압축할 준비 중입니다.",
                ArchiveCommandKind.Extract => "선택한 압축파일을 해제할 준비 중입니다.",
                _ => StatusText.Text
            };
        }

        RefreshDefaultArchiveStatus();
        RefreshConflictPolicy();
        RefreshArchivePassword();
        RefreshDragOverlaySettings();
    }

    public async Task RunCommandAsync(ArchiveCommand command)
    {
        ProgressRing.IsActive = true;
        DetailText.Text = string.Join(Environment.NewLine, command.Paths);

        var result = await ArchiveCommandService.ExecuteAsync(command);
        ProgressRing.IsActive = false;
        ArchiveCommandService.RevealResult(result);
        StatusText.Text = result.Success ? "작업이 완료되었습니다." : "작업에 실패했습니다.";
        DetailText.Text = result.Message;
    }

    private void OpenSettingsClick(object sender, RoutedEventArgs e)
    {
        SettingsPanel.Visibility = SettingsPanel.Visibility == Visibility.Visible
            ? Visibility.Collapsed
            : Visibility.Visible;
        RefreshDefaultArchiveStatus();
    }

    private void DefaultArchiveToggleToggled(object sender, RoutedEventArgs e)
    {
        if (updatingDefaultArchiveToggle)
        {
            return;
        }

        if (DefaultArchiveToggle.IsOn)
        {
            var status = WindowsDefaultAppService.RegisterAsDefaultCandidate();
            ApplyDefaultArchiveStatus(status);
            StatusText.Text = "newnZip을 기본 앱 후보로 등록했습니다.";
            DetailText.Text = "Windows 기본 앱 설정에서 newnZip을 선택하면 토글이 켜진 상태로 유지됩니다.";
        }
        else
        {
            WindowsDefaultAppService.OpenDefaultAppsSettings();
            RefreshDefaultArchiveStatus();
            StatusText.Text = "기본 앱 변경은 Windows 설정에서 선택해 주세요.";
        }
    }

    private void ConflictPolicySelectionChanged(object sender, SelectionChangedEventArgs e)
    {
        if (updatingConflictPolicyBox || ConflictPolicyBox.SelectedItem is not ComboBoxItem item)
        {
            return;
        }

        if (Enum.TryParse<OutputConflictPolicy>(item.Tag?.ToString(), out var policy))
        {
            ArchiveCommandService.ConflictPolicy = policy;
            StatusText.Text = "같은 이름 결과 처리 설정을 저장했습니다.";
        }
    }

    private void ArchivePasswordBoxPasswordChanged(object sender, RoutedEventArgs e)
    {
        if (updatingArchivePassword)
        {
            return;
        }

        AppPreferences.ArchivePassword = ArchivePasswordBox.Password;
        StatusText.Text = "압축 암호 설정을 저장했습니다.";
    }

    private void DragOverlayToggleToggled(object sender, RoutedEventArgs e)
    {
        if (updatingDragOverlayToggle)
        {
            return;
        }

        AppPreferences.DragOverlayEnabled = DragOverlayToggle.IsOn;
        StatusText.Text = "드래그 오버레이 설정을 저장했습니다.";
    }

    private void DragOverlayDockSideSelectionChanged(object sender, SelectionChangedEventArgs e)
    {
        if (updatingDragOverlayDockSideBox || DragOverlayDockSideBox.SelectedItem is not ComboBoxItem item)
        {
            return;
        }

        if (Enum.TryParse<DragOverlayDockSide>(item.Tag?.ToString(), out var dockSide))
        {
            AppPreferences.DragOverlayDockSide = dockSide;
            StatusText.Text = "드래그 오버레이 위치를 저장했습니다.";
        }
    }

    private void CloseClick(object sender, RoutedEventArgs e)
    {
        Close();
    }

    private void RefreshDefaultArchiveStatus()
    {
        ApplyDefaultArchiveStatus(WindowsDefaultAppService.GetStatus());
    }

    private void RefreshConflictPolicy()
    {
        updatingConflictPolicyBox = true;
        var current = ArchiveCommandService.ConflictPolicy.ToString();
        foreach (var item in ConflictPolicyBox.Items.OfType<ComboBoxItem>())
        {
            if (item.Tag?.ToString() == current)
            {
                ConflictPolicyBox.SelectedItem = item;
                break;
            }
        }
        updatingConflictPolicyBox = false;
    }

    private void RefreshArchivePassword()
    {
        updatingArchivePassword = true;
        ArchivePasswordBox.Password = AppPreferences.ArchivePassword;
        updatingArchivePassword = false;
    }

    private void RefreshDragOverlaySettings()
    {
        updatingDragOverlayToggle = true;
        DragOverlayToggle.IsOn = AppPreferences.DragOverlayEnabled;
        updatingDragOverlayToggle = false;

        updatingDragOverlayDockSideBox = true;
        var current = AppPreferences.DragOverlayDockSide.ToString();
        foreach (var item in DragOverlayDockSideBox.Items.OfType<ComboBoxItem>())
        {
            if (item.Tag?.ToString() == current)
            {
                DragOverlayDockSideBox.SelectedItem = item;
                break;
            }
        }
        updatingDragOverlayDockSideBox = false;
    }

    private void ApplyDefaultArchiveStatus(WindowsDefaultAppStatus status)
    {
        updatingDefaultArchiveToggle = true;
        DefaultArchiveToggle.IsOn = status.IsDefault;
        DefaultArchiveStatusText.Text = status.Message;
        updatingDefaultArchiveToggle = false;
    }

    private void DropSurfaceDragOver(object sender, DragEventArgs e)
    {
        e.AcceptedOperation = e.DataView.Contains(StandardDataFormats.StorageItems)
            ? DataPackageOperation.Copy
            : DataPackageOperation.None;
    }

    private async void DropSurfaceDrop(object sender, DragEventArgs e)
    {
        if (!e.DataView.Contains(StandardDataFormats.StorageItems))
        {
            return;
        }

        var items = await e.DataView.GetStorageItemsAsync();
        var command = ArchiveCommandService.FromDroppedPaths(items.Select(item => item.Path));
        if (command.Kind == ArchiveCommandKind.None)
        {
            StatusText.Text = "드롭한 항목을 처리할 수 없습니다.";
            return;
        }

        await RunCommandAsync(command);
    }
}
