using Microsoft.Win32;

namespace NewnZipWin;

public enum DragOverlayDockSide
{
    Left,
    Right
}

public static class AppPreferences
{
    private const string SettingsRegistryPath = @"Software\newnZip";
    private const string ArchivePasswordRegistryName = "ArchivePassword";
    private const string DragOverlayEnabledRegistryName = "DragOverlayEnabled";
    private const string DragOverlayDockSideRegistryName = "DragOverlayDockSide";
    private const string DefaultSplitSizeMbRegistryName = "DefaultSplitSizeMb";
    private const string DefaultSplitPartCountRegistryName = "DefaultSplitPartCount";

    public static event Action? Changed;

    public static string ArchivePassword
    {
        get
        {
            using var key = Registry.CurrentUser.OpenSubKey(SettingsRegistryPath);
            return key?.GetValue(ArchivePasswordRegistryName) as string ?? string.Empty;
        }
        set
        {
            using var key = Registry.CurrentUser.CreateSubKey(SettingsRegistryPath);
            key?.SetValue(ArchivePasswordRegistryName, value ?? string.Empty, RegistryValueKind.String);
            Changed?.Invoke();
        }
    }

    public static bool DragOverlayEnabled
    {
        get
        {
            using var key = Registry.CurrentUser.OpenSubKey(SettingsRegistryPath);
            var value = key?.GetValue(DragOverlayEnabledRegistryName);
            return value is int intValue ? intValue != 0 : true;
        }
        set
        {
            using var key = Registry.CurrentUser.CreateSubKey(SettingsRegistryPath);
            key?.SetValue(DragOverlayEnabledRegistryName, value ? 1 : 0, RegistryValueKind.DWord);
            Changed?.Invoke();
        }
    }

    public static DragOverlayDockSide DragOverlayDockSide
    {
        get
        {
            using var key = Registry.CurrentUser.OpenSubKey(SettingsRegistryPath);
            var value = key?.GetValue(DragOverlayDockSideRegistryName) as string;
            return Enum.TryParse<DragOverlayDockSide>(value, ignoreCase: true, out var side)
                ? side
                : DragOverlayDockSide.Left;
        }
        set
        {
            using var key = Registry.CurrentUser.CreateSubKey(SettingsRegistryPath);
            key?.SetValue(DragOverlayDockSideRegistryName, value.ToString(), RegistryValueKind.String);
            Changed?.Invoke();
        }
    }

    public static int DefaultSplitSizeMb
    {
        get
        {
            using var key = Registry.CurrentUser.OpenSubKey(SettingsRegistryPath);
            var value = key?.GetValue(DefaultSplitSizeMbRegistryName);
            return value is int intValue && intValue > 0 ? intValue : 100;
        }
        set
        {
            using var key = Registry.CurrentUser.CreateSubKey(SettingsRegistryPath);
            key?.SetValue(DefaultSplitSizeMbRegistryName, Math.Max(1, value), RegistryValueKind.DWord);
            Changed?.Invoke();
        }
    }

    public static int DefaultSplitPartCount
    {
        get
        {
            using var key = Registry.CurrentUser.OpenSubKey(SettingsRegistryPath);
            var value = key?.GetValue(DefaultSplitPartCountRegistryName);
            return value is int intValue && intValue > 1 ? intValue : 4;
        }
        set
        {
            using var key = Registry.CurrentUser.CreateSubKey(SettingsRegistryPath);
            key?.SetValue(DefaultSplitPartCountRegistryName, Math.Max(2, value), RegistryValueKind.DWord);
            Changed?.Invoke();
        }
    }
}
