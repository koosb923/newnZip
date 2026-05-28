using Microsoft.UI.Xaml;

namespace NewnZipWin;

public sealed partial class MainWindow : Window
{
    private readonly ArchiveCommand initialCommand;

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
        StatusText.Text = "설정 화면은 다음 단계에서 Windows UI로 확장합니다.";
    }

    private void CloseClick(object sender, RoutedEventArgs e)
    {
        Close();
    }
}
