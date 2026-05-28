using System.Diagnostics;

namespace NewnZipWin;

internal static class Program
{
    private static int Main(string[] args)
    {
        if (args.Length < 2)
        {
            PrintUsage();
            return 1;
        }

        var command = args[0];
        var rawPaths = command == "--split-compress" ? args.Skip(2) : args.Skip(1);
        var paths = rawPaths.Where(File.Exists).Concat(rawPaths.Where(Directory.Exists)).Distinct().ToArray();
        if (paths.Length == 0)
        {
            Console.Error.WriteLine("처리할 파일이나 폴더를 찾지 못했습니다.");
            return 1;
        }

        return command switch
        {
            "--compress" => Compress(paths, splitSizeMb: 0),
            "--extract" => Extract(paths),
            "--split-compress" => Compress(paths, args.Length > 1 ? ParseSplitSize(args[1]) : 0),
            _ => Unknown(command)
        };
    }

    private static int Compress(string[] paths, int splitSizeMb)
    {
        if (paths.Length == 0)
        {
            Console.Error.WriteLine("압축할 항목이 없습니다.");
            return 1;
        }

        var first = paths[0];
        var parent = Path.GetDirectoryName(first) ?? Environment.CurrentDirectory;
        var baseName = Directory.Exists(first)
            ? new DirectoryInfo(first).Name
            : Path.GetFileNameWithoutExtension(first);
        var output = Path.Combine(parent, $"{baseName}.zip");

        if (splitSizeMb > 0)
        {
            var sevenZip = FindExecutable("7zz.exe") ?? FindExecutable("7z.exe");
            if (sevenZip is null)
            {
                Console.Error.WriteLine("분할 압축에는 7zz 또는 7z가 필요합니다.");
                return 1;
            }
            return Run(sevenZip, new[] { "a", "-tzip", $"-v{splitSizeMb}m", output }.Concat(paths));
        }

        var engine = FindBundledEngine();
        if (engine is null)
        {
            Console.Error.WriteLine("newnZip 엔진을 찾지 못했습니다.");
            return 1;
        }
        return Run(engine, new[] { "create", output }.Concat(paths));
    }

    private static int Extract(string[] paths)
    {
        var exitCode = 0;
        foreach (var archive in paths)
        {
            var parent = Path.GetDirectoryName(archive) ?? Environment.CurrentDirectory;
            var destination = Path.Combine(parent, ExtractionDirectoryName(archive));
            var extension = Path.GetExtension(archive).ToLowerInvariant();

            if (extension == ".zip" && FindBundledEngine() is { } engine)
            {
                exitCode = Math.Max(exitCode, Run(engine, new[] { "extract", archive, destination }));
                continue;
            }

            var sevenZip = FindExecutable("7zz.exe") ?? FindExecutable("7z.exe");
            if (sevenZip is null)
            {
                Console.Error.WriteLine("이 압축 파일을 풀려면 7zz 또는 7z가 필요합니다.");
                return 1;
            }
            exitCode = Math.Max(exitCode, Run(sevenZip, new[] { "x", archive, $"-o{destination}", "-y" }));
        }
        return exitCode;
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

    private static string? FindExecutable(string name)
    {
        var paths = (Environment.GetEnvironmentVariable("PATH") ?? "").Split(Path.PathSeparator);
        return paths.Select(path => Path.Combine(path, name)).FirstOrDefault(File.Exists);
    }

    private static int Run(string executable, IEnumerable<string> arguments)
    {
        var startInfo = new ProcessStartInfo
        {
            FileName = executable,
            UseShellExecute = false
        };
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

    private static int Unknown(string command)
    {
        Console.Error.WriteLine($"알 수 없는 명령입니다: {command}");
        PrintUsage();
        return 1;
    }

    private static void PrintUsage()
    {
        Console.WriteLine("사용법:");
        Console.WriteLine("  NewnZipWin.exe --compress <파일-또는-폴더>...");
        Console.WriteLine("  NewnZipWin.exe --extract <압축파일>...");
        Console.WriteLine("  NewnZipWin.exe --split-compress <MB> <파일-또는-폴더>...");
    }
}
