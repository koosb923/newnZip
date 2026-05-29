using Microsoft.UI.Xaml;

namespace NewnZipWin;

public sealed partial class HudWindow : Window
{
    private readonly ArchiveCommand command;
    private readonly CancellationTokenSource cancellation = new();
    private readonly bool exitApplicationWhenDone;

    public HudWindow(ArchiveCommand command, bool exitApplicationWhenDone = true)
    {
        this.command = command;
        this.exitApplicationWhenDone = exitApplicationWhenDone;
        InitializeComponent();

        TitleText.Text = command.Kind switch
        {
            ArchiveCommandKind.Compress => "압축 중...",
            ArchiveCommandKind.SplitCompress => "분할 압축 중...",
            ArchiveCommandKind.Extract => "압축 해제 중...",
            _ => "작업 중..."
        };
        DetailText.Text = command.Paths.FirstOrDefault() ?? "-";
    }

    public async Task RunAsync()
    {
        var result = await ArchiveCommandService.ExecuteAsync(
            command,
            line =>
            {
                var progress = ArchiveCommandService.ParseProgressLine(line);
                if (progress is null)
                {
                    return;
                }

                DispatcherQueue.TryEnqueue(() =>
                {
                    ProgressBar.Value = Math.Clamp(progress.Fraction * 100, 0, 100);
                    PercentText.Text = $"{(int)(progress.Fraction * 100)}%";
                    DetailText.Text = progress.Name;
                });
            },
            cancellation.Token);

        if (!cancellation.IsCancellationRequested)
        {
            ArchiveCommandService.RevealResult(result);
        }

        DispatcherQueue.TryEnqueue(async () =>
        {
            TitleText.Text = result.Success ? "완료되었습니다." : "작업에 실패했습니다.";
            DetailText.Text = result.Message;
            ProgressBar.Value = result.Success ? 100 : ProgressBar.Value;
            CancelButton.IsEnabled = false;
            await Task.Delay(900);
            Close();
            if (exitApplicationWhenDone)
            {
                Application.Current.Exit();
            }
        });
    }

    private void CancelClick(object sender, RoutedEventArgs e)
    {
        cancellation.Cancel();
        TitleText.Text = "취소하는 중...";
        CancelButton.IsEnabled = false;
    }
}
