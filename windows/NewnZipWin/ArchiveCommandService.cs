using System.Diagnostics;
using System.Runtime.InteropServices;

namespace NewnZipWin;

public enum ArchiveCommandKind
{
    None,
    Compress,
    SplitCompress,
    Extract
}

public sealed record ArchiveCommand(ArchiveCommandKind Kind, string[] Paths, int SplitSizeMb = 0);

public sealed record ArchiveCommandResult(bool Success, string Message, string[] RevealPaths)
{
    public ArchiveCommandResult(bool success, string message) : this(success, message, [])
    {
    }
}

public static class ArchiveCommandService
{
    public static ArchiveCommand Parse(IEnumerable<string> rawArgs)
    {
        var args = rawArgs.ToArray();
        if (args.Length == 0)
        {
            return new ArchiveCommand(ArchiveCommandKind.None, []);
        }

        var command = args[0];
        var rawPaths = command == "--split-compress" ? args.Skip(2) : args.Skip(1);
        var paths = rawPaths.Where(File.Exists).Concat(rawPaths.Where(Directory.Exists)).Distinct().ToArray();

        return command switch
        {
            "--compress" => new ArchiveCommand(ArchiveCommandKind.Compress, paths),
            "--extract" => new ArchiveCommand(ArchiveCommandKind.Extract, paths),
            "--split-compress" => new ArchiveCommand(ArchiveCommandKind.SplitCompress, paths, args.Length > 1 ? ParseSplitSize(args[1]) : 0),
            _ => new ArchiveCommand(ArchiveCommandKind.None, [])
        };
    }

    public static Task<ArchiveCommandResult> ExecuteAsync(ArchiveCommand command)
    {
        return Task.Run(() => command.Kind switch
        {
            ArchiveCommandKind.Compress => Compress(command.Paths, splitSizeMb: 0),
            ArchiveCommandKind.SplitCompress => Compress(command.Paths, command.SplitSizeMb),
            ArchiveCommandKind.Extract => Extract(command.Paths),
            _ => new ArchiveCommandResult(false, "처리할 명령이 없습니다.")
        });
    }

    public static void RevealResult(ArchiveCommandResult result)
    {
        if (!result.Success)
        {
            ShowFailure(result.Message);
            return;
        }

        foreach (var path in result.RevealPaths.Where(path => !string.IsNullOrWhiteSpace(path)).Distinct())
        {
            if (File.Exists(path))
            {
                SelectInExplorer(path);
                continue;
            }

            if (Directory.Exists(path))
            {
                OpenInExplorer(path);
            }
        }
    }

    private static ArchiveCommandResult Compress(string[] paths, int splitSizeMb)
    {
        if (paths.Length == 0)
        {
            return new ArchiveCommandResult(false, "압축할 항목이 없습니다.");
        }

        var first = paths[0];
        var parent = Path.GetDirectoryName(first) ?? Environment.CurrentDirectory;
        var baseName = Directory.Exists(first)
            ? new DirectoryInfo(first).Name
            : Path.GetFileNameWithoutExtension(first);
        var output = Path.Combine(parent, $"{baseName}.zip");

        var engine = FindBundledEngine();
        if (engine is null)
        {
            return new ArchiveCommandResult(false, "newnZip 엔진을 찾지 못했습니다.");
        }

        var tempOutput = splitSizeMb > 0
            ? Path.Combine(Path.GetTempPath(), $"newnzip-{Guid.NewGuid():N}.zip")
            : output;

        var exitCode = Run(engine, new[] { "create", tempOutput }.Concat(paths));
        if (exitCode != 0)
        {
            return new ArchiveCommandResult(false, $"압축 실패: exit code {exitCode}");
        }

        if (splitSizeMb > 0)
        {
            try
            {
                SplitFile(tempOutput, output, splitSizeMb);
                File.Delete(tempOutput);
                return new ArchiveCommandResult(true, $"분할 압축 완료: {output}.001", [$"{output}.001"]);
            }
            catch (Exception ex)
            {
                return new ArchiveCommandResult(false, $"분할 압축 실패: {ex.Message}");
            }
        }

        return new ArchiveCommandResult(true, $"압축 완료: {output}", [output]);
    }

    private static ArchiveCommandResult Extract(string[] paths)
    {
        if (paths.Length == 0)
        {
            return new ArchiveCommandResult(false, "해제할 압축파일이 없습니다.");
        }

        var engine = FindBundledEngine();
        if (engine is null)
        {
            return new ArchiveCommandResult(false, "newnZip 엔진을 찾지 못했습니다.");
        }

        var exitCode = 0;
        var destinations = new List<string>();
        foreach (var archive in paths)
        {
            var archiveToExtract = archive;
            var temporaryJoinedArchive = string.Empty;

            if (IsSplitArchiveStart(archive))
            {
                temporaryJoinedArchive = Path.Combine(Path.GetTempPath(), $"newnzip-joined-{Guid.NewGuid():N}.zip");
                JoinSplitFiles(archive, temporaryJoinedArchive);
                archiveToExtract = temporaryJoinedArchive;
            }

            var parent = Path.GetDirectoryName(archive) ?? Environment.CurrentDirectory;
            var destination = Path.Combine(parent, ExtractionDirectoryName(archive));
            exitCode = Math.Max(exitCode, Run(engine, new[] { "extract", archiveToExtract, destination }));
            if (exitCode == 0)
            {
                destinations.Add(destination);
            }

            if (!string.IsNullOrEmpty(temporaryJoinedArchive))
            {
                File.Delete(temporaryJoinedArchive);
            }
        }

        return exitCode == 0
            ? new ArchiveCommandResult(true, "압축 해제가 완료되었습니다.", destinations.ToArray())
            : new ArchiveCommandResult(false, $"압축 해제 실패: exit code {exitCode}");
    }

    private static void SelectInExplorer(string path)
    {
        StartExplorer($"/select,\"{path}\"");
    }

    private static void OpenInExplorer(string path)
    {
        StartExplorer($"\"{path}\"");
    }

    private static void StartExplorer(string arguments)
    {
        try
        {
            Process.Start(new ProcessStartInfo
            {
                FileName = "explorer.exe",
                UseShellExecute = true,
                Arguments = arguments
            });
        }
        catch
        {
            // Explorer reveal is best-effort; archive operations have already completed.
        }
    }

    private static void ShowFailure(string message)
    {
        try
        {
            MessageBox(IntPtr.Zero, message, "newnZip", 0x00000010);
        }
        catch
        {
            // Keep headless shell commands quiet if the notification helper is unavailable.
        }
    }

    [DllImport("user32.dll", CharSet = CharSet.Unicode)]
    private static extern int MessageBox(IntPtr hWnd, string text, string caption, uint type);

    private static string ExtractionDirectoryName(string archive)
    {
        var name = Path.GetFileName(archive);
        var lower = name.ToLowerInvariant();
        if (lower.EndsWith(".zip.001") || lower.EndsWith(".7z.001") ||
            lower.EndsWith(".tar.gz") || lower.EndsWith(".tar.bz2") || lower.EndsWith(".tar.xz"))
        {
            return Path.GetFileNameWithoutExtension(Path.GetFileNameWithoutExtension(name));
        }
        return Path.GetFileNameWithoutExtension(name);
    }

    private static string? FindBundledEngine()
    {
        var baseDir = AppContext.BaseDirectory;
        var candidates = new[]
        {
            Path.Combine(baseDir, "newnzip-engine.exe"),
            Path.Combine(baseDir, "newnzip_engine", "newnzip-engine.exe")
        };
        return candidates.FirstOrDefault(File.Exists);
    }

    private static int Run(string executable, IEnumerable<string> arguments)
    {
        var startInfo = new ProcessStartInfo
        {
            FileName = executable,
            UseShellExecute = false
        };
        var backendDir = Path.Combine(AppContext.BaseDirectory, "newnzip_engine", "backends");
        if (Directory.Exists(backendDir))
        {
            startInfo.Environment["NEWNZIP_BACKEND_DIR"] = backendDir;
        }
        foreach (var argument in arguments)
        {
            startInfo.ArgumentList.Add(argument);
        }

        using var process = Process.Start(startInfo);
        process?.WaitForExit();
        return process?.ExitCode ?? 1;
    }

    private static int ParseSplitSize(string value)
    {
        return int.TryParse(value, out var parsed) && parsed > 0 ? parsed : 0;
    }

    private static bool IsSplitArchiveStart(string path)
    {
        var lower = Path.GetFileName(path).ToLowerInvariant();
        return lower.EndsWith(".zip.001") || lower.EndsWith(".7z.001") || lower.EndsWith(".001");
    }

    private static void SplitFile(string source, string destinationBase, int splitSizeMb)
    {
        var chunkSize = Math.Max(1, splitSizeMb) * 1024 * 1024;
        using var input = File.OpenRead(source);
        var buffer = new byte[1024 * 1024];
        var part = 1;

        while (input.Position < input.Length)
        {
            var partPath = $"{destinationBase}.{part:000}";
            using var output = File.Create(partPath);
            var remaining = chunkSize;

            while (remaining > 0)
            {
                var read = input.Read(buffer, 0, Math.Min(buffer.Length, remaining));
                if (read == 0)
                {
                    break;
                }
                output.Write(buffer, 0, read);
                remaining -= read;
            }

            part += 1;
        }
    }

    private static void JoinSplitFiles(string startPath, string destination)
    {
        var basePath = startPath.EndsWith(".001", StringComparison.OrdinalIgnoreCase)
            ? startPath[..^4]
            : Path.Combine(Path.GetDirectoryName(startPath) ?? string.Empty, Path.GetFileNameWithoutExtension(startPath));

        using var output = File.Create(destination);
        for (var part = 1; part < 10000; part++)
        {
            var partPath = $"{basePath}.{part:000}";
            if (!File.Exists(partPath))
            {
                break;
            }
            using var input = File.OpenRead(partPath);
            input.CopyTo(output);
        }
    }
}
