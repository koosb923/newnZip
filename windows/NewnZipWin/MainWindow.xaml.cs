using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;

namespace NewnZipWin;

public sealed partial class MainWindow : Window
{
    private readonly ArchiveCommand initialCommand;
    private bool updatingDefaultArchiveToggle;
    private bool updatingConflictPolicyBox;

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
    }

    public async Task RunCommandAsync(ArchiveCommand command)
    {
        ProgressRing.IsActive = true;
        DetailText.Text = string.Join(Environment.NewLine, command.Paths);

        var result = await ArchiveCommandService.ExecuteAsync(command);
        ProgressRing.IsActive = false;
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

    private void ApplyDefaultArchiveStatus(WindowsDefaultAppStatus status)
    {
        updatingDefaultArchiveToggle = true;
        DefaultArchiveToggle.IsOn = status.IsDefault;
        DefaultArchiveStatusText.Text = status.Message;
        updatingDefaultArchiveToggle = false;
    }
}
