$ErrorActionPreference = 'Stop'
Add-Type @'
using System;
using System.Runtime.InteropServices;
public static class NativeUi {
    [DllImport("user32.dll")] public static extern IntPtr GetDlgItem(IntPtr parent, int id);
    [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr window);
    [DllImport("user32.dll")] public static extern IntPtr SendMessage(IntPtr window, uint message, IntPtr value, IntPtr data);
    [DllImport("user32.dll")] public static extern bool PostMessage(IntPtr window, uint message, IntPtr value, IntPtr data);
}
'@
$exe = Join-Path (Split-Path -Parent $PSScriptRoot) 'build/Release/HdrBridge.exe'
$app = Start-Process -FilePath $exe -PassThru
try {
    $deadline = [DateTime]::UtcNow.AddSeconds(15)
    do {
        Start-Sleep -Milliseconds 200
        $app.Refresh()
        if ($app.HasExited) { throw "The Bridge exited during startup ($($app.ExitCode))." }
        $window = $app.MainWindowHandle
        $mode = [NativeUi]::GetDlgItem($window, 102)
    } until ($mode -ne [IntPtr]::Zero -or [DateTime]::UtcNow -ge $deadline)
    if ($mode -eq [IntPtr]::Zero) { throw 'The capture controls did not initialize.' }

    Start-Sleep -Seconds 2
    foreach ($id in @(100, 109, 122, 202, 210, 211)) {
        if (-not [NativeUi]::IsWindowVisible([NativeUi]::GetDlgItem($window, $id))) {
            throw "Control $id was hidden after startup."
        }
    }
    $notice = [NativeUi]::GetDlgItem($window, 214)
    if ($notice -eq [IntPtr]::Zero -or [NativeUi]::IsWindowVisible($notice)) {
        throw 'The GDI warning should exist and be hidden before selecting GDI.'
    }
    $null = [NativeUi]::SendMessage($mode, 0x014E, [IntPtr]2, [IntPtr]::Zero) # CB_SETCURSEL
    $null = [NativeUi]::PostMessage($window, 0x0111, [IntPtr](0x10000 + 102), $mode) # CBN_SELCHANGE
    $deadline = [DateTime]::UtcNow.AddSeconds(5)
    while (-not [NativeUi]::IsWindowVisible($notice) -and [DateTime]::UtcNow -lt $deadline) {
        Start-Sleep -Milliseconds 100
    }
    if (-not [NativeUi]::IsWindowVisible($notice)) { throw 'The GDI warning did not appear.' }
    Write-Output 'Capture, preview and theme controls stayed visible. The GDI warning appeared on selection.'
} finally {
    if (-not $app.HasExited) { Stop-Process -Id $app.Id }
    $app.Dispose()
}
