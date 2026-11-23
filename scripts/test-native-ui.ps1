$ErrorActionPreference = 'Stop'
Add-Type @'
using System;
using System.Runtime.InteropServices;
public static class NativeUi {
    [StructLayout(LayoutKind.Sequential)] public struct Point { public int X, Y; }
    [StructLayout(LayoutKind.Sequential)] public struct Rect { public int Left, Top, Right, Bottom; }
    [DllImport("user32.dll")] public static extern IntPtr GetDlgItem(IntPtr parent, int id);
    [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr window);
    [DllImport("user32.dll")] public static extern IntPtr SendMessage(IntPtr window, uint message, IntPtr value, IntPtr data);
    [DllImport("user32.dll")] public static extern bool PostMessage(IntPtr window, uint message, IntPtr value, IntPtr data);
    [DllImport("user32.dll", CharSet = CharSet.Unicode)] public static extern IntPtr FindWindow(string className, string title);
    [DllImport("user32.dll")] public static extern IntPtr GetWindow(IntPtr window, uint command);
    [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr window, out Rect rect);
    [DllImport("user32.dll")] public static extern int GetSystemMetrics(int metric);
    [DllImport("user32.dll")] public static extern bool GetCursorPos(out Point point);
    [DllImport("user32.dll")] public static extern bool SetCursorPos(int x, int y);
    [DllImport("user32.dll")] public static extern bool GetWindowDisplayAffinity(IntPtr window, out uint affinity);
    [DllImport("user32.dll")] public static extern int GetWindowRgn(IntPtr window, IntPtr region);
    [DllImport("gdi32.dll")] public static extern IntPtr CreateRectRgn(int left, int top, int right, int bottom);
    [DllImport("gdi32.dll")] public static extern bool DeleteObject(IntPtr objectHandle);
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
    if ([NativeUi]::SendMessage($mode, 0x0146, [IntPtr]::Zero, [IntPtr]::Zero).ToInt32() -ne 2) { # CB_GETCOUNT
        throw 'Only the two SDR output modes should be available.'
    }
    $null = [NativeUi]::SendMessage($mode, 0x014E, [IntPtr]1, [IntPtr]::Zero) # CB_SETCURSEL
    $null = [NativeUi]::PostMessage($window, 0x0111, [IntPtr](0x10000 + 102), $mode) # CBN_SELCHANGE
    $deadline = [DateTime]::UtcNow.AddSeconds(5)
    while (-not [NativeUi]::IsWindowVisible($notice) -and [DateTime]::UtcNow -lt $deadline) {
        Start-Sleep -Milliseconds 100
    }
    if (-not [NativeUi]::IsWindowVisible($notice)) { throw 'The GDI warning did not appear.' }
    Write-Output 'Capture, preview and theme controls stayed visible. The GDI warning appeared on selection.'

    $kind = [NativeUi]::GetDlgItem($window, 108)
    $null = [NativeUi]::SendMessage($kind, 0x014E, [IntPtr]0, [IntPtr]::Zero)
    $null = [NativeUi]::PostMessage($window, 0x0111, [IntPtr](0x10000 + 108), $kind)
    $null = [NativeUi]::SendMessage($mode, 0x014E, [IntPtr]0, [IntPtr]::Zero)
    $null = [NativeUi]::SendMessage($window, 0x0111, [IntPtr](0x10000 + 102), $mode)
    if ([NativeUi]::IsWindowVisible($notice)) { throw 'The GDI warning did not hide after returning to GPU output.' }
    $null = [NativeUi]::PostMessage($window, 0x0111, [IntPtr]100, [IntPtr]::Zero) # Start capture
    $null = [NativeUi]::PostMessage($window, 0x0111, [IntPtr]106, [IntPtr]::Zero) # Open output
    $output = [NativeUi]::FindWindow('HdrBridgeOutput', 'HDR Bridge - Saida SDR')
    $deadline = [DateTime]::UtcNow.AddSeconds(8)
    while (-not [NativeUi]::IsWindowVisible($output) -and [DateTime]::UtcNow -lt $deadline) {
        Start-Sleep -Milliseconds 100
    }
    $rect = [NativeUi+Rect]::new()
    if (-not [NativeUi]::IsWindowVisible($output) -or -not [NativeUi]::GetWindowRect($output, [ref]$rect)) {
        throw 'The capture output did not open.'
    }
    $cursor = [NativeUi+Point]::new()
    $restoreCursor = [NativeUi]::GetCursorPos([ref]$cursor)
    try {
        $desktopRight = [NativeUi]::GetSystemMetrics(76) + [NativeUi]::GetSystemMetrics(78)
        $null = [NativeUi]::SetCursorPos([Math]::Min($rect.Right - 30, $desktopRight - 2), $rect.Top + 7)
        $deadline = [DateTime]::UtcNow.AddSeconds(5)
        do {
            Start-Sleep -Milliseconds 100
            $toolbar = [NativeUi]::GetWindow($output, 6) # GW_ENABLEDPOPUP
            $lower = [NativeUi]::GetDlgItem($toolbar, 120)
        } until ([NativeUi]::IsWindowVisible($lower) -or [DateTime]::UtcNow -ge $deadline)
        if (-not [NativeUi]::IsWindowVisible($lower)) { throw 'The output toolbar did not appear on hover.' }
        $clip = [NativeUi]::CreateRectRgn(0, 0, 0, 0)
        try {
            if ([NativeUi]::GetWindowRgn($toolbar, $clip) -ne 3) { throw 'The output toolbar is missing its rounded region.' }
        } finally {
            $null = [NativeUi]::DeleteObject($clip)
        }
        $affinity = [uint32]0
        if (-not [NativeUi]::GetWindowDisplayAffinity($toolbar, [ref]$affinity) -or $affinity -ne 0x11) {
            throw 'The output toolbar was not excluded from screen captures.'
        }
        Write-Output 'The rounded output toolbar appeared on hover and remained excluded from capture.'
    } finally {
        if ($restoreCursor) { $null = [NativeUi]::SetCursorPos($cursor.X, $cursor.Y) }
    }
} finally {
    if (-not $app.HasExited) { Stop-Process -Id $app.Id }
    $app.Dispose()
}
