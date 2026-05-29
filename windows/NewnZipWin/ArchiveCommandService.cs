using System.Diagnostics;
using Microsoft.Win32;
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

public sealed record ArchiveProgress(string Stage, int Completed, int Total, string Name)
{
    public double Fraction => Total > 0 ? (double)Completed / Total : 0;
}

public enum OutputConflictPolicy
{
    Append,
    Overwrite,
    Ask
}

public static class ArchiveCommandService
{
    private const string SettingsRegistryPath = @"Software\newnZip";
    private const string ConflictPolicyRegistryName = "OutputConflictPolicy";
    private static readonly HashSet<string> ArchiveExtensions = new(StringComparer.OrdinalIgnoreCase)
    {
        ".zip", ".7z", ".rar", ".tar", ".tgz", ".gz", ".bz2", ".xz", ".zst", ".zstd",
        ".lz4", ".br", ".brotli", ".cab", ".iso", ".wim", ".arj", ".lzh", ".lha",
        ".cpio", ".rpm", ".deb", ".img", ".001"
    };

    public static OutputConflictPolicy ConflictPolicy
    {
        get
        {
            using var key = Registry.CurrentUser.OpenSubKey(SettingsRegistryPath);
            var value = key?.GetValue(ConflictPolicyRegistryName) as string;
            return Enum.TryParse<OutputConflictPolicy>(value, ignoreCase: true, out var policy)
                ? policy
                : OutputConflictPolicy.Append;
        }
        set
        {
            using var key = Registry.CurrentUser.CreateSubKey(SettingsRegistryPath);
            key?.SetValue(ConflictPolicyRegistryName, value.ToString(), RegistryValueKind.String);
        }
    }

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

    public static Task<ArchiveCommandResult> ExecuteAsync(
        ArchiveCommand command,
        Action<string>? onLine = null,
        CancellationToken cancellationToken = default)
    {
        return Task.Run(() => command.Kind switch
        {
            ArchiveCommandKind.Compress => Compress(command.Paths, splitSizeMb: 0, password: null, onLine, cancellationToken),
            ArchiveCommandKind.SplitCompress => Compress(command.Paths, command.SplitSizeMb, password: null, onLine, cancellationToken),
            ArchiveCommandKind.Extract => Extract(command.Paths, password: null, onLine, cancellationToken),
            _ => new ArchiveCommandResult(false, "처리할 명령이 없습니다.")
        }, cancellationToken);
    }

    public static Task<ArchiveCommandResult> ExecuteCompressAsync(
        string[] paths,
        int splitSizeMb = 0,
        string? password = null,
        Action<string>? onLine = null,
        CancellationToken cancellationToken = default)
    {
        return Task.Run(() => Compress(paths, splitSizeMb, password, onLine, cancellationToken), cancellationToken);
    }

    public static Task<ArchiveCommandResult> ExecuteExtractAsync(
        string[] paths,
        string? password = null,
        Action<string>? onLine = null,
        CancellationToken cancellationToken = default)
    {
        return Task.Run(() => Extract(paths, password, onLine, cancellationToken), cancellationToken);
    }

    public static ArchiveProgress? ParseProgressLine(string line)
    {
        var parts = line.Split('\t');
        if (parts.Length < 5 || parts[0] != "NEWNZIP_PROGRESS")
        {
            return null;
        }

        return int.TryParse(parts[2], out var completed) && int.TryParse(parts[3], out var total)
            ? new ArchiveProgress(parts[1], completed, total, parts[4])
            : null;
    }

    public static ArchiveCommand FromDroppedPaths(IEnumerable<string> paths)
    {
        var resolved = paths
            .Where(path => File.Exists(path) || Directory.Exists(path))
            .Distinct(StringComparer.OrdinalIgnoreCase)
            .ToArray();

        if (resolved.Length == 0)
        {
            return new ArchiveCommand(ArchiveCommandKind.None, []);
        }

        var shouldExtract = resolved.All(IsArchivePath);
        return new ArchiveCommand(
            shouldExtract ? ArchiveCommandKind.Extract : ArchiveCommandKind.Compress,
            resolved);
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

    private static ArchiveCommandResult Compress(
        string[] paths,
        int splitSizeMb,
        string? password,
        Action<string>? onLine,
        CancellationToken cancellationToken)
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
        output = ResolveOutputPath(output, splitSizeMb > 0, ConflictPolicy);

        var engine = FindBundledEngine();
        if (engine is null)
        {
            return new ArchiveCommandResult(false, "newnZip 엔진을 찾지 못했습니다.");
        }

        var tempOutput = splitSizeMb > 0
            ? Path.Combine(Path.GetTempPath(), $"newnzip-{Guid.NewGuid():N}.zip")
            : output;

        var archivePassword = string.IsNullOrWhiteSpace(password) ? AppPreferences.ArchivePassword.Trim() : password.Trim();
        var createArguments = new List<string> { "create" };
        if (!string.IsNullOrEmpty(archivePassword))
        {
            createArguments.Add($"--password={archivePassword}");
        }
        createArguments.Add(tempOutput);
        createArguments.AddRange(paths);

        var exitCode = Run(engine, createArguments, onLine, cancellationToken);
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

    private static ArchiveCommandResult Extract(string[] paths, string? password, Action<string>? onLine, CancellationToken cancellationToken)
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
            destination = ResolveOutputPath(destination, isSplitOutput: false, ConflictPolicy);
            var archivePassword = string.IsNullOrWhiteSpace(password) ? AppPreferences.ArchivePassword.Trim() : password.Trim();
            var extractArguments = new List<string> { "extract" };
            if (!string.IsNullOrEmpty(archivePassword))
            {
                extractArguments.Add($"--password={archivePassword}");
            }
            extractArguments.Add(archiveToExtract);
            extractArguments.Add(destination);
            exitCode = Math.Max(exitCode, Run(engine, extractArguments, onLine, cancellationToken));
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

    private static string ResolveOutputPath(string requestedPath, bool isSplitOutput, OutputConflictPolicy policy)
    {
        var collisionPath = isSplitOutput ? $"{requestedPath}.001" : requestedPath;
        if (!File.Exists(collisionPath) && !Directory.Exists(collisionPath))
        {
            return requestedPath;
        }

        var resolvedPolicy = policy == OutputConflictPolicy.Ask
            ? AskConflictPolicy()
            : policy;

        if (resolvedPolicy == OutputConflictPolicy.Overwrite)
        {
            RemoveOutput(requestedPath, isSplitOutput);
            return requestedPath;
        }

        var directory = Path.GetDirectoryName(requestedPath) ?? Environment.CurrentDirectory;
        var extension = Path.GetExtension(requestedPath);
        var baseName = string.IsNullOrEmpty(extension)
            ? Path.GetFileName(requestedPath)
            : Path.GetFileNameWithoutExtension(requestedPath);

        for (var index = 2; index <= 9999; index++)
        {
            var candidate = string.IsNullOrEmpty(extension)
                ? Path.Combine(directory, $"{baseName} {index}")
                : Path.Combine(directory, $"{baseName} {index}{extension}");
            var candidateCollision = isSplitOutput ? $"{candidate}.001" : candidate;
            if (!File.Exists(candidateCollision) && !Directory.Exists(candidateCollision))
            {
                return candidate;
            }
        }

        return requestedPath;
    }

    private static OutputConflictPolicy AskConflictPolicy()
    {
        var result = MessageBox(
            IntPtr.Zero,
            "같은 이름의 결과가 있습니다. 사본으로 추가할까요?\n\n'아니요'를 누르면 기존 항목을 덮어씁니다.",
            "newnZip",
            0x00000004 | 0x00000020);
        return result == 7 ? OutputConflictPolicy.Overwrite : OutputConflictPolicy.Append;
    }

    private static void RemoveOutput(string requestedPath, bool isSplitOutput)
    {
        if (isSplitOutput)
        {
            for (var index = 1; index <= 9999; index++)
            {
                var part = $"{requestedPath}.{index:000}";
                if (!File.Exists(part))
                {
                    break;
                }
                File.Delete(part);
            }
            return;
        }

        if (Directory.Exists(requestedPath))
        {
            Directory.Delete(requestedPath, recursive: true);
        }
        else if (File.Exists(requestedPath))
        {
            File.Delete(requestedPath);
        }
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

    private static bool IsArchivePath(string path)
    {
        if (path.EndsWith(".zip.001", StringComparison.OrdinalIgnoreCase) ||
            path.EndsWith(".7z.001", StringComparison.OrdinalIgnoreCase))
        {
            return true;
        }

        var extension = Path.GetExtension(path);
        return !string.IsNullOrEmpty(extension) && ArchiveExtensions.Contains(extension);
    }

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

    private static int Run(
        string executable,
        IEnumerable<string> arguments,
        Action<string>? onLine,
        CancellationToken cancellationToken)
    {
        var startInfo = new ProcessStartInfo
        {
            FileName = executable,
            UseShellExecute = false,
            RedirectStandardOutput = true,
            RedirectStandardError = true
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
        if (process is null)
        {
            return 1;
        }

        using var registration = cancellationToken.Register(() =>
        {
            try
            {
                if (!process.HasExited)
                {
                    process.Kill(entireProcessTree: true);
                }
            }
            catch
            {
                // Best effort cancellation.
            }
        });

        process.OutputDataReceived += (_, eventArgs) =>
        {
            if (!string.IsNullOrEmpty(eventArgs.Data))
            {
                onLine?.Invoke(eventArgs.Data);
            }
        };
        process.ErrorDataReceived += (_, eventArgs) =>
        {
            if (!string.IsNullOrEmpty(eventArgs.Data))
            {
                onLine?.Invoke(eventArgs.Data);
            }
        };
        process.BeginOutputReadLine();
        process.BeginErrorReadLine();
        process.WaitForExit();
        return cancellationToken.IsCancellationRequested ? 130 : process.ExitCode;
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
