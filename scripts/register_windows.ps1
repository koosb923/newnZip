param(
    [string]$ExePath = $(Read-Host "Enter full path to NewnZipWin.exe"),
    [int]$DefaultSplitSizeMB = 100,
    [switch]$SetDefault
)

$AppName = "newnZip"
$ProgId = "newnZip.Archive"
$ArchiveExtensions = @(".zip", ".7z", ".rar", ".tar", ".tgz", ".gz", ".bz2", ".xz", ".001")

function Set-DefaultValue($Path, $Value) {
    New-Item -Path $Path -Force | Out-Null
    Set-ItemProperty -Path $Path -Name "(Default)" -Value $Value
}

if (-not (Test-Path $ExePath)) {
    throw "Executable not found: $ExePath"
}

Set-DefaultValue "HKCU:\Software\Classes\$ProgId" "$AppName archive"
Set-DefaultValue "HKCU:\Software\Classes\$ProgId\shell\open\command" "`"$ExePath`" --extract `"%1`""
Set-DefaultValue "HKCU:\Software\Classes\$ProgId\shell\newnzip.extract" "newnZip으로 압축 풀기"
Set-DefaultValue "HKCU:\Software\Classes\$ProgId\shell\newnzip.extract\command" "`"$ExePath`" --extract `"%1`""

foreach ($Extension in $ArchiveExtensions) {
    if ($SetDefault) {
        Set-DefaultValue "HKCU:\Software\Classes\$Extension" $ProgId
    }
    Set-DefaultValue "HKCU:\Software\Classes\SystemFileAssociations\$Extension\shell\newnzip.extract" "newnZip으로 압축 풀기"
    Set-DefaultValue "HKCU:\Software\Classes\SystemFileAssociations\$Extension\shell\newnzip.extract\command" "`"$ExePath`" --extract `"%1`""
}

Set-DefaultValue "HKCU:\Software\Classes\*\shell\newnzip.compress" "newnZip으로 압축하기"
Set-DefaultValue "HKCU:\Software\Classes\*\shell\newnzip.compress\command" "`"$ExePath`" --compress `"%1`""
Set-DefaultValue "HKCU:\Software\Classes\Directory\shell\newnzip.compress" "newnZip으로 압축하기"
Set-DefaultValue "HKCU:\Software\Classes\Directory\shell\newnzip.compress\command" "`"$ExePath`" --compress `"%1`""

Set-DefaultValue "HKCU:\Software\Classes\*\shell\newnzip.splitCompress" "newnZip으로 분할 압축하기"
Set-DefaultValue "HKCU:\Software\Classes\*\shell\newnzip.splitCompress\command" "`"$ExePath`" --split-compress $DefaultSplitSizeMB `"%1`""
Set-DefaultValue "HKCU:\Software\Classes\Directory\shell\newnzip.splitCompress" "newnZip으로 분할 압축하기"
Set-DefaultValue "HKCU:\Software\Classes\Directory\shell\newnzip.splitCompress\command" "`"$ExePath`" --split-compress $DefaultSplitSizeMB `"%1`""

Write-Host "Context menus registered for current user."
if ($SetDefault) {
    Write-Host "Archive file associations registered for current user."
}
