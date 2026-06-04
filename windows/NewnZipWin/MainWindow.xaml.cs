using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Input;
using Windows.ApplicationModel.DataTransfer;

namespace NewnZipWin;

public sealed partial class MainWindow : Window
{
    private readonly ArchiveCommand initialCommand;
    private SettingsSection activeSettingsSection = SettingsSection.General;
    private bool updatingDefaultArchiveToggle;
    private bool updatingConflictPolicyBox;
    private bool updatingArchivePassword;
    private bool updatingDragOverlayToggle;
    private bool updatingDragOverlayDockSideBox;
    private bool updatingDefaultSplitSizeBox;
    private bool updatingDefaultSplitPartCountBox;

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
        RefreshSplitDefaults();
        ApplySettingsSection();
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
        ApplySettingsSection();
    }

    private void GeneralSettingsTabButtonClick(object sender, RoutedEventArgs e)
    {
        activeSettingsSection = SettingsSection.General;
        ApplySettingsSection();
    }

    private void CompressionSettingsTabButtonClick(object sender, RoutedEventArgs e)
    {
        activeSettingsSection = SettingsSection.Compression;
        ApplySettingsSection();
    }

    private void OverlaySettingsTabButtonClick(object sender, RoutedEventArgs e)
    {
        activeSettingsSection = SettingsSection.Overlay;
        ApplySettingsSection();
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

    private void DefaultSplitSizeBoxTextChanged(object sender, TextChangedEventArgs e)
    {
        if (updatingDefaultSplitSizeBox)
        {
            return;
        }

        if (int.TryParse(DefaultSplitSizeBox.Text, out var value))
        {
            AppPreferences.DefaultSplitSizeMb = Math.Max(1, value);
            StatusText.Text = "기본 분할 용량을 저장했습니다.";
        }
    }

    private void DefaultSplitPartCountBoxTextChanged(object sender, TextChangedEventArgs e)
    {
        if (updatingDefaultSplitPartCountBox)
        {
            return;
        }

        if (int.TryParse(DefaultSplitPartCountBox.Text, out var value))
        {
            AppPreferences.DefaultSplitPartCount = Math.Max(2, value);
            StatusText.Text = "기본 분할 개수를 저장했습니다.";
        }
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

    private void RefreshSplitDefaults()
    {
        updatingDefaultSplitSizeBox = true;
        DefaultSplitSizeBox.Text = AppPreferences.DefaultSplitSizeMb.ToString();
        updatingDefaultSplitSizeBox = false;

        updatingDefaultSplitPartCountBox = true;
        DefaultSplitPartCountBox.Text = AppPreferences.DefaultSplitPartCount.ToString();
        updatingDefaultSplitPartCountBox = false;
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

    private void ApplySettingsSection()
    {
        GeneralSettingsSection.Visibility = activeSettingsSection == SettingsSection.General
            ? Visibility.Visible
            : Visibility.Collapsed;
        CompressionSettingsSection.Visibility = activeSettingsSection == SettingsSection.Compression
            ? Visibility.Visible
            : Visibility.Collapsed;
        OverlaySettingsSection.Visibility = activeSettingsSection == SettingsSection.Overlay
            ? Visibility.Visible
            : Visibility.Collapsed;
    }

    private void DropSurfaceDragOver(object sender, DragEventArgs e) => ApplyDropOperation(e);
    private void PasswordDropSurfaceDragOver(object sender, DragEventArgs e) => ApplyDropOperation(e);
    private void SplitDropSurfaceDragOver(object sender, DragEventArgs e) => ApplyDropOperation(e);

    private void ApplyDropOperation(DragEventArgs e)
    {
        e.AcceptedOperation = e.DataView.Contains(StandardDataFormats.StorageItems)
            ? DataPackageOperation.Copy
            : DataPackageOperation.None;
    }

    private async void DropSurfaceDrop(object sender, DragEventArgs e)
    {
        var paths = await ReadDroppedPathsAsync(e);
        if (paths.Length == 0)
        {
            return;
        }

        await HandleStandardDropAsync(paths);
    }

    private async void PasswordDropSurfaceDrop(object sender, DragEventArgs e)
    {
        var paths = await ReadDroppedPathsAsync(e);
        if (paths.Length == 0)
        {
            return;
        }

        await HandlePasswordCompressAsync(paths);
    }

    private async void SplitDropSurfaceDrop(object sender, DragEventArgs e)
    {
        var paths = await ReadDroppedPathsAsync(e);
        if (paths.Length == 0)
        {
            return;
        }

        await HandleSplitCompressAsync(paths);
    }

    private void DropSurfaceTapped(object sender, TappedRoutedEventArgs e)
    {
        StatusText.Text = "기본 영역은 파일을 드래그해서 넣어 주세요.";
    }

    private void PasswordDropSurfaceTapped(object sender, TappedRoutedEventArgs e)
    {
        StatusText.Text = "암호 압축은 파일을 드래그해서 넣어 주세요.";
    }

    private void SplitDropSurfaceTapped(object sender, TappedRoutedEventArgs e)
    {
        StatusText.Text = "분할 압축은 파일을 드래그해서 넣어 주세요.";
    }

    private async Task HandleStandardDropAsync(string[] paths)
    {
        var existing = paths.Where(path => File.Exists(path) || Directory.Exists(path)).ToArray();
        if (existing.Length == 0)
        {
            return;
        }

        var archives = existing.Where(IsArchivePath).ToArray();
        if (archives.Length == existing.Length)
        {
            if (archives.Length == 1)
            {
                await RunExtractAsync(archives, password: null);
                return;
            }

            var action = await PromptMultipleArchiveActionAsync();
            switch (action)
            {
            case MultipleArchiveAction.ExtractEach:
                await RunExtractAsync(archives, password: null);
                break;
            case MultipleArchiveAction.CompressTogether:
                await RunCompressAsync(archives, splitSizeMb: 0, password: null);
                break;
            default:
                break;
            }
            return;
        }

        await RunCompressAsync(existing, splitSizeMb: 0, password: null);
    }

    private async Task HandlePasswordCompressAsync(string[] paths)
    {
        var password = await PromptPasswordAsync(AppPreferences.ArchivePassword);
        if (string.IsNullOrWhiteSpace(password))
        {
            return;
        }

        AppPreferences.ArchivePassword = password;
        await RunCompressAsync(paths, splitSizeMb: 0, password: password);
    }

    private async Task HandleSplitCompressAsync(string[] paths)
    {
        var splitOption = await PromptSplitOptionAsync(paths);
        if (splitOption is null)
        {
            return;
        }

        await RunCompressAsync(paths, splitOption.SplitSizeMb, password: null);
    }

    private async Task RunCompressAsync(string[] paths, int splitSizeMb, string? password)
    {
        ProgressRing.IsActive = true;
        DetailText.Text = string.Join(Environment.NewLine, paths);

        var result = await ArchiveCommandService.ExecuteCompressAsync(paths, splitSizeMb, password);
        ProgressRing.IsActive = false;
        ArchiveCommandService.RevealResult(result);
        StatusText.Text = result.Success ? "작업이 완료되었습니다." : "작업에 실패했습니다.";
        DetailText.Text = result.Message;
    }

    private async Task RunExtractAsync(string[] paths, string? password)
    {
        ProgressRing.IsActive = true;
        DetailText.Text = string.Join(Environment.NewLine, paths);

        var result = await ArchiveCommandService.ExecuteExtractAsync(paths, password);
        ProgressRing.IsActive = false;
        ArchiveCommandService.RevealResult(result);
        StatusText.Text = result.Success ? "작업이 완료되었습니다." : "작업에 실패했습니다.";
        DetailText.Text = result.Message;
    }

    private async Task<string[]> ReadDroppedPathsAsync(DragEventArgs e)
    {
        if (!e.DataView.Contains(StandardDataFormats.StorageItems))
        {
            return [];
        }

        var items = await e.DataView.GetStorageItemsAsync();
        return items
            .Select(item => item.Path)
            .Where(path => !string.IsNullOrWhiteSpace(path))
            .Distinct(StringComparer.OrdinalIgnoreCase)
            .ToArray();
    }

    private async Task<MultipleArchiveAction> PromptMultipleArchiveActionAsync()
    {
        var dialog = new ContentDialog
        {
            Title = "여러 압축파일을 어떻게 처리할까요?",
            PrimaryButtonText = "각각 압축 풀기",
            SecondaryButtonText = "하나로 압축하기",
            CloseButtonText = "취소",
            Content = new TextBlock
            {
                Text = "각각 압축을 풀거나, 선택한 압축파일들을 다시 하나로 압축할 수 있습니다.",
                TextWrapping = TextWrapping.Wrap
            },
            XamlRoot = RootGrid.XamlRoot
        };

        return await dialog.ShowAsync() switch
        {
            ContentDialogResult.Primary => MultipleArchiveAction.ExtractEach,
            ContentDialogResult.Secondary => MultipleArchiveAction.CompressTogether,
            _ => MultipleArchiveAction.Cancel
        };
    }

    private async Task<string?> PromptPasswordAsync(string defaultValue)
    {
        var passwordBox = new PasswordBox
        {
            Password = defaultValue
        };

        var dialog = new ContentDialog
        {
            Title = "암호로 압축",
            PrimaryButtonText = "압축",
            CloseButtonText = "취소",
            Content = passwordBox,
            XamlRoot = RootGrid.XamlRoot
        };

        var result = await dialog.ShowAsync();
        return result == ContentDialogResult.Primary
            ? passwordBox.Password.Trim()
            : null;
    }

    private async Task<SplitOption?> PromptSplitOptionAsync(string[] paths)
    {
        var modeBox = new ComboBox();
        modeBox.Items.Add(new ComboBoxItem { Content = "몇 MB씩 나누기", Tag = "size" });
        modeBox.Items.Add(new ComboBoxItem { Content = "몇 개로 나누기", Tag = "count" });
        modeBox.SelectedIndex = 0;

        var valueBox = new TextBox
        {
            Text = AppPreferences.DefaultSplitSizeMb.ToString()
        };

        modeBox.SelectionChanged += (_, _) =>
        {
            valueBox.Text = (modeBox.SelectedItem as ComboBoxItem)?.Tag?.ToString() == "count"
                ? AppPreferences.DefaultSplitPartCount.ToString()
                : AppPreferences.DefaultSplitSizeMb.ToString();
        };

        var panel = new StackPanel { Spacing = 8 };
        panel.Children.Add(modeBox);
        panel.Children.Add(valueBox);

        var dialog = new ContentDialog
        {
            Title = "분할 압축 옵션",
            PrimaryButtonText = "적용",
            CloseButtonText = "취소",
            Content = panel,
            XamlRoot = RootGrid.XamlRoot
        };

        if (await dialog.ShowAsync() != ContentDialogResult.Primary)
        {
            return null;
        }

        var value = Math.Max(1, int.TryParse(valueBox.Text, out var parsed) ? parsed : 1);
        var mode = (modeBox.SelectedItem as ComboBoxItem)?.Tag?.ToString() ?? "size";
        if (mode == "count")
        {
            AppPreferences.DefaultSplitPartCount = Math.Max(2, value);
            return new SplitOption(EstimateSplitSizeMb(paths, Math.Max(2, value)));
        }

        AppPreferences.DefaultSplitSizeMb = value;
        return new SplitOption(value);
    }

    private int EstimateSplitSizeMb(IEnumerable<string> paths, int partCount)
    {
        var totalBytes = paths.Sum(GetItemSize);
        return Math.Max(1, (int)Math.Ceiling(totalBytes / 1048576d / Math.Max(2, partCount)));
    }

    private long GetItemSize(string path)
    {
        if (File.Exists(path))
        {
            return new FileInfo(path).Length;
        }

        if (!Directory.Exists(path))
        {
            return 0;
        }

        long total = 0;
        foreach (var file in Directory.EnumerateFiles(path, "*", SearchOption.AllDirectories))
        {
            try
            {
                total += new FileInfo(file).Length;
            }
            catch
            {
                // Ignore unreadable files while estimating split size.
            }
        }
        return total;
    }

    private bool IsArchivePath(string path)
    {
        var lower = path.ToLowerInvariant();
        if (lower.EndsWith(".zip.001") || lower.EndsWith(".7z.001"))
        {
            return true;
        }
        return ArchiveCommandService.FromDroppedPaths([path]).Kind == ArchiveCommandKind.Extract;
    }
}

internal enum MultipleArchiveAction
{
    ExtractEach,
    CompressTogether,
    Cancel
}

internal enum SettingsSection
{
    General,
    Compression,
    Overlay
}

internal sealed record SplitOption(int SplitSizeMb);
