# Fuck sRGB

A Windows 11 x64 tray utility that changes the transfer curve of supported SDR content in the Desktop Window Manager (DWM) while HDR is active. This build patches **DWM only**: it does not inspect, inject into, or modify Edge, Chrome, WebView2, or any other application process.

## What it changes

Windows converts ordinary full-range sRGB surfaces to linear scRGB before HDR desktop composition. The sRGB transfer function is piecewise; it is not a pure gamma 2.2 power curve. This tool offers an optional pure-power interpretation for viewers who want that response. It is a display policy override, not a claim that sRGB decoding is mathematically incorrect. [Microsoft Advanced Color documentation](https://learn.microsoft.com/en-us/windows/win32/direct3darticles/high-dynamic-range), [DXGI color space definitions](https://learn.microsoft.com/en-us/windows/win32/api/dxgicommon/ne-dxgicommon-dxgi_color_space_type).

The SDR reference-white scaling, BT.709 primaries, and downstream output conversion retain their original roles. Changing gamma does not replace Windows' SDR content brightness adjustment or a display calibration.

## Usage

Build the executable or use an available release, then run it. Administrator rights are required to verify and patch DWM. Right-click the notification-area icon to select a mode.

| Mode | Behavior on supported DWM SDR paths |
| --- | --- |
| Native | Restores the original Windows shaders and cancels automatic curve reapplication. |
| Gamma2.2 | Uses the pure power curve `x^2.2`. |
| Gamma2.4 | Uses the pure power curve `x^2.4`. |
| Linear | Uses `x`, intended for identifying affected rendering paths. |
| Exit | Stops monitoring; does not undo existing process-memory edits. Select Native first to undo them. |

A mode change restarts DWM and can briefly blank the screen. The application remembers the selected curve while running and reapplies it after an independent DWM restart.

### HDR-aware behavior

- At least one **active display must currently use HDR** before a non-native curve can be applied. Merely reporting HDR capability, or using SDR automatic color management, is not sufficient.
- A mixed HDR/SDR desktop is permitted. The shader edits are session-wide, not separate per-monitor gamma controls. Mixed-display behavior must still be verified for a particular graphics/display configuration.
- With no active HDR output, startup does not apply the correction or restart DWM if it is already Native. If a correction was active, the monitor restores the original shaders once HDR becomes inactive. The requested curve is retained and reapplied when HDR returns.
- If display state cannot be queried, automatic changes pause rather than assuming HDR is enabled. Native can still be selected whenever DWM state is known.
- Failed verification stops automatic patch retries until the display state changes, DWM is replaced, or the user selects a mode again. Unknown shaders are never patched by guessing.

Display notifications and a two-second monitor update the HDR and DWM state. A normal SDR output does not use the patched HDR conversion path; restoration also clears the edits when the desktop no longer has active HDR.

### Command line

```bat
Fuck-sRGB.exe --gamma22 --quiet
```

`--gamma22` selects gamma 2.2 at startup. Without it, the tray initially follows the currently observed DWM mode. `--quiet` suppresses warning/error dialogs; errors and state transitions are recorded in `logs/startup.log` beside the executable. The tray icon remains available. `--windows-only` is accepted as a compatibility alias; this build is always DWM-only.

## Elevated, silent startup at sign-in

After building, run the installer from a PowerShell console that permits local scripts:

```powershell
.\install-startup.ps1 -ExecutablePath .\Fuck-sRGB.exe
```

The installer requests elevation once, copies the executable into `%ProgramFiles%\Fuck-sRGB`, verifies the copied hash, and registers the **Fuck-sRGB Gamma2.2** scheduled task for the current user:

- Trigger: user sign-in, delayed 15 seconds to let the interactive display session initialize.
- Principal: the current user's interactive token, with highest available privileges; no password is stored.
- Action: the protected executable directly, with `--gamma22 --quiet`. No user-writable launcher script runs elevated at sign-in.
- No console or UAC prompt on subsequent scheduled launches. The existing tray interface remains available.
- No execution time limit; duplicate task starts are ignored. The program also enforces a per-session single instance.

DWM belongs to an interactive user session, so this is a **sign-in task**, not a pre-login system service. The installer also starts the task immediately. `start-gamma22.ps1` can start the installed task manually and never starts a browser component.

To remove automatic startup without deleting the executable:

```powershell
.\install-startup.ps1 -Remove
```

To stop the correction as well, select Native and exit the tray. Removing the scheduled task alone does not reverse process-memory edits.

## Compatibility and implementation

Requires Windows 11 x64 and a driver supporting the current display configuration APIs. The disk scanner has matched all six target shaders in `dwmcore.dll` **10.0.26100.9711**, **10.0.26100.9818**, and **10.0.26100.9949**. Compatibility is decided by shader contents, not the DLL version string.

The scanner parses DXBC libraries in the read-only data section of the on-disk system DLL and requires each full SHA-256 shader signature to appear exactly once. It verifies the original constants and library checksums, prepares the curve variants, and recomputes the shader and container checksums. On a mode change it restarts the current session's DWM and briefly suspends the new instance while applying verified edits to copy-on-write pages. It validates the resulting library hashes before resuming DWM. System files on disk are not modified.

## Limitations

- Applications can linearize SDR themselves or combine SDR and HDR into scRGB/PQ before submission. DWM cannot reliably recover the original SDR curve from those pixels. Such paths may require an application-specific setting; this build does not patch browsers to force them onto another path.
- Independent-flip or true exclusive-fullscreen presentation can bypass the patched conversion. A video codec or container tag does not prove which presentation path is used.
- The edits target SDR transfer branches, not native linear scRGB or HDR10 PQ. Software capture checks describe the tested signal path, not the monitor's optical EOTF, ICC calibration, or all possible driver presentation paths.
- HDR deactivation and restoration are detected asynchronously. Switching modes can briefly blank the screen; do not use repeated DWM restarts as a rapid visual comparison tool.
- Quiet startup cannot fix an unsupported shader layout or missing process privileges. Consult the log and tray status instead of assuming the desired curve was applied.

## Build and checks

Use an x64 MSVC developer command prompt with a recent Windows SDK:

```bat
rc /nologo sdr-gamma.rc
cl /nologo /std:c++17 /W4 /O2 /utf-8 /EHsc /MT /Fe:Fuck-sRGB.exe sdr-gamma-tray.cpp sdr-gamma.res /link /SUBSYSTEM:WINDOWS /MANIFEST:EMBED /MANIFESTUAC:"level='requireAdministrator' uiAccess='false'" bcrypt.lib advapi32.lib user32.lib shell32.lib
```

The HDR policy checks do not change display state or patch DWM:

```bat
cl /nologo /std:c++17 /W4 /WX /O2 /utf-8 /EHsc /MT /Fe:hdr-policy.exe tests\hdr-policy.cpp /link bcrypt.lib advapi32.lib user32.lib shell32.lib
hdr-policy.exe
```

## Acknowledgments

Thanks to [ledoge's dwm_eotf](https://github.com/ledoge/dwm_eotf) and [dwm_eotf_rs](https://github.com/SERGEYDJUM/dwm_eotf_rs) for the shader-patching approach and allow-list. The repository's historical browser patch sources credit [Browser Gamma Fix](https://github.com/mrsaliericz/browser-gamma-fix); they are not compiled into this DWM-only application.

Developed by GPT.
