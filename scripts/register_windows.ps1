param(
    [string]$ExePath = $(Read-Host "Enter full path to NewnZipWin.exe"),
    [int]$DefaultSplitSizeMB = 100,
    [switch]$SetDefault,
    [switch]$IncludeSplitMenu
)

$AppName = "newnZip"
$ProgId = "newnZip.Archive"
$ArchiveExtensions = @(
    ".zip",
    ".7z",
    ".rar",
    ".tar",
    ".tgz",
    ".tbz2",
    ".txz",
    ".gz",
    ".bz2",
    ".xz",
    ".zst",
    ".zstd",
    ".lz4",
    ".lz5",
    ".br",
    ".brotli",
    ".alz",
    ".egg",
    ".cab",
    ".iso",
    ".wim",
    ".arj",
    ".lzh",
    ".lha",
    ".lzma",
    ".z",
    ".cpio",
    ".rpm",
    ".deb",
    ".msi",
    ".nsis",
    ".asar",
    ".udf",
    ".img",
    ".zpaq",
    ".kz",
    ".001"
)

function Set-DefaultValue($Path, $Value) {
    New-Item -Path $Path -Force | Out-Null
    Set-ItemProperty -Path $Path -Name "(Default)" -Value $Value
}

function Set-StringValue($Path, $Name, $Value) {
    New-Item -Path $Path -Force | Out-Null
    New-ItemProperty -Path $Path -Name $Name -Value $Value -PropertyType String -Force | Out-Null
}

function Register-ShellCommand($Path, $Label, $Command) {
    Set-DefaultValue $Path $Label
    Set-StringValue $Path "Icon" "`"$ExePath`",0"
    Set-StringValue $Path "MultiSelectModel" "Player"
    Set-DefaultValue "$Path\command" $Command
}

function Remove-RegistryKey($Path) {
    if (Test-Path $Path) {
        Remove-Item -Path $Path -Recurse -Force
    }
}

if (-not (Test-Path $ExePath)) {
    throw "Executable not found: $ExePath"
}

Set-DefaultValue "HKCU:\Software\Classes\$ProgId" "$AppName archive"
Set-DefaultValue "HKCU:\Software\Classes\$ProgId\DefaultIcon" "`"$ExePath`",0"
Set-DefaultValue "HKCU:\Software\Classes\$ProgId\shell\open\command" "`"$ExePath`" --extract `"%1`""
Register-ShellCommand "HKCU:\Software\Classes\$ProgId\shell\newnzip.extract" "newnZip으로 압축 풀기" "`"$ExePath`" --extract `"%1`""
Set-DefaultValue "HKCU:\Software\Classes\Applications\NewnZipWin.exe\shell\open\command" "`"$ExePath`" `"%1`""
Set-DefaultValue "HKCU:\Software\Classes\Applications\NewnZipWin.exe\shell\newnzip.extract" "newnZip으로 압축 풀기"
Set-DefaultValue "HKCU:\Software\Classes\Applications\NewnZipWin.exe\shell\newnzip.extract\command" "`"$ExePath`" --extract `"%1`""

foreach ($Extension in $ArchiveExtensions) {
    if ($SetDefault) {
        Set-DefaultValue "HKCU:\Software\Classes\$Extension" $ProgId
    }
    Set-DefaultValue "HKCU:\Software\Classes\$Extension\OpenWithProgids" ""
    Set-StringValue "HKCU:\Software\Classes\$Extension\OpenWithProgids" $ProgId ""
    Register-ShellCommand "HKCU:\Software\Classes\SystemFileAssociations\$Extension\shell\newnzip.extract" "newnZip으로 압축 풀기" "`"$ExePath`" --extract `"%1`""
}

Register-ShellCommand "HKCU:\Software\Classes\*\shell\newnzip.compress" "newnZip으로 압축하기" "`"$ExePath`" --compress `"%1`""
Register-ShellCommand "HKCU:\Software\Classes\Directory\shell\newnzip.compress" "newnZip으로 압축하기" "`"$ExePath`" --compress `"%1`""

if ($IncludeSplitMenu) {
    Register-ShellCommand "HKCU:\Software\Classes\*\shell\newnzip.splitCompress" "newnZip으로 분할 압축하기" "`"$ExePath`" --split-compress $DefaultSplitSizeMB `"%1`""
    Register-ShellCommand "HKCU:\Software\Classes\Directory\shell\newnzip.splitCompress" "newnZip으로 분할 압축하기" "`"$ExePath`" --split-compress $DefaultSplitSizeMB `"%1`""
} else {
    Remove-RegistryKey "HKCU:\Software\Classes\*\shell\newnzip.splitCompress"
    Remove-RegistryKey "HKCU:\Software\Classes\Directory\shell\newnzip.splitCompress"
}

Write-Host "Registered current-user context menus:"
Write-Host "- newnZip으로 압축하기"
Write-Host "- newnZip으로 압축 풀기"
if ($IncludeSplitMenu) {
    Write-Host "- newnZip으로 분할 압축하기"
}
if ($SetDefault) {
    Write-Host "Archive ProgID associations were registered for current user."
    Write-Host "If Windows keeps an existing default app, choose newnZip once in Settings > Apps > Default apps."
}
