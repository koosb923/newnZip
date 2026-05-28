$AppName = "newnZip"
$Extensions = @(".zip", ".7z", ".rar", ".tar", ".tgz")
$ExePath = Read-Host "Enter full path to newnZip.exe"

foreach ($Extension in $Extensions) {
    $ProgId = "$AppName$Extension"
    New-Item -Path "HKCU:\Software\Classes\$Extension" -Force | Out-Null
    Set-ItemProperty -Path "HKCU:\Software\Classes\$Extension" -Name "(Default)" -Value $ProgId

    New-Item -Path "HKCU:\Software\Classes\$ProgId\shell\open\command" -Force | Out-Null
    Set-ItemProperty -Path "HKCU:\Software\Classes\$ProgId\shell\open\command" -Name "(Default)" -Value "`"$ExePath`" `"%1`""
}

Write-Host "Associations registered for current user."
