using System.Diagnostics;
using Microsoft.Win32;

namespace NewnZipWin;

public sealed record WindowsDefaultAppStatus(bool IsDefault, string Message);

public static class WindowsDefaultAppService
{
    private const string ProgId = "newnZip.Archive";
    private const string AppName = "newnZip";

    private static readonly string[] ArchiveExtensions =
    [
        ".zip", ".jar", ".7z", ".rar", ".tar", ".tgz", ".tbz2", ".txz", ".gz", ".bz2", ".xz",
        ".zst", ".zstd", ".lz4", ".lz5", ".br", ".brotli", ".alz", ".egg", ".cab",
        ".iso", ".wim", ".arj", ".lzh", ".lha", ".ace", ".uue", ".uu", ".lzma", ".z", ".cpio", ".rpm",
        ".deb", ".msi", ".nsis", ".asar", ".udf", ".img", ".dmg", ".zpaq", ".kz", ".001"
    ];

    public static WindowsDefaultAppStatus GetStatus()
    {
        var defaultZipProgId = Registry.GetValue(
            @"HKEY_CURRENT_USER\Software\Microsoft\Windows\CurrentVersion\Explorer\FileExts\.zip\UserChoice",
            "ProgId",
            null) as string;

        if (!string.IsNullOrWhiteSpace(defaultZipProgId))
        {
            return string.Equals(defaultZipProgId, ProgId, StringComparison.OrdinalIgnoreCase)
                ? new WindowsDefaultAppStatus(true, "현재 newnZip이 ZIP 기본 앱입니다.")
                : new WindowsDefaultAppStatus(false, $"현재 ZIP 기본 앱은 {defaultZipProgId}입니다.");
        }

        var classProgId = Registry.GetValue(@"HKEY_CURRENT_USER\Software\Classes\.zip", string.Empty, null) as string;
        return string.Equals(classProgId, ProgId, StringComparison.OrdinalIgnoreCase)
            ? new WindowsDefaultAppStatus(true, "현재 newnZip이 ZIP 기본 앱 후보로 등록되어 있습니다.")
            : new WindowsDefaultAppStatus(false, "현재 다른 앱이 기본 압축 앱으로 설정되어 있습니다.");
    }

    public static WindowsDefaultAppStatus RegisterAsDefaultCandidate()
    {
        var exePath = Environment.ProcessPath
            ?? Process.GetCurrentProcess().MainModule?.FileName
            ?? "NewnZipWin.exe";

        RegisterProgId(exePath);
        RegisterContextMenus(exePath);
        OpenDefaultAppsSettings();
        return GetStatus();
    }

    public static void OpenDefaultAppsSettings()
    {
        try
        {
            Process.Start(new ProcessStartInfo
            {
                FileName = "ms-settings:defaultapps",
                UseShellExecute = true
            });
        }
        catch
        {
            // Settings can fail in restricted environments. The status text still explains what happened.
        }
    }

    private static void RegisterProgId(string exePath)
    {
        SetDefaultValue($@"Software\Classes\{ProgId}", $"{AppName} archive");
        SetDefaultValue($@"Software\Classes\{ProgId}\DefaultIcon", $"\"{exePath}\",0");
        SetDefaultValue($@"Software\Classes\{ProgId}\shell\open\command", $"\"{exePath}\" --extract \"%1\"");
        RegisterShellCommand($@"Software\Classes\{ProgId}\shell\newnzip.extract", "newnZip으로 압축 풀기", $"\"{exePath}\" --extract \"%1\"", exePath);

        foreach (var extension in ArchiveExtensions)
        {
            using var openWith = Registry.CurrentUser.CreateSubKey($@"Software\Classes\{extension}\OpenWithProgids");
            openWith?.SetValue(ProgId, string.Empty, RegistryValueKind.String);
            RegisterShellCommand($@"Software\Classes\SystemFileAssociations\{extension}\shell\newnzip.extract", "newnZip으로 압축 풀기", $"\"{exePath}\" --extract \"%1\"", exePath);
        }
    }

    private static void RegisterContextMenus(string exePath)
    {
        RegisterShellCommand(@"Software\Classes\*\shell\newnzip.compress", "newnZip으로 압축하기", $"\"{exePath}\" --compress \"%1\"", exePath);
        RegisterShellCommand(@"Software\Classes\Directory\shell\newnzip.compress", "newnZip으로 압축하기", $"\"{exePath}\" --compress \"%1\"", exePath);
        Registry.CurrentUser.DeleteSubKeyTree(@"Software\Classes\*\shell\newnzip.splitCompress", throwOnMissingSubKey: false);
        Registry.CurrentUser.DeleteSubKeyTree(@"Software\Classes\Directory\shell\newnzip.splitCompress", throwOnMissingSubKey: false);
    }

    private static void RegisterShellCommand(string keyPath, string label, string command, string exePath)
    {
        SetDefaultValue(keyPath, label);
        using var key = Registry.CurrentUser.CreateSubKey(keyPath);
        key?.SetValue("Icon", $"\"{exePath}\",0", RegistryValueKind.String);
        key?.SetValue("MultiSelectModel", "Player", RegistryValueKind.String);
        SetDefaultValue($@"{keyPath}\command", command);
    }

    private static void SetDefaultValue(string keyPath, string value)
    {
        using var key = Registry.CurrentUser.CreateSubKey(keyPath);
        key?.SetValue(string.Empty, value, RegistryValueKind.String);
    }
}
