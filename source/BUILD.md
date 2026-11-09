# Building and signing Golden Gate

## What's here
- `src/`        - the application source (C++ / Win32 / Direct2D), one file per area
- `assets/`     - cursors (Bibata), Dock icons (MacTahoe, rendered to PNG), fonts
                  (Inter), wallpapers (original), the app icon, and tools/
- `installer/`  - the NSIS installer script
- `dist/`       - the built `GoldenGate.exe`, licenses and the README that ship
- `build.sh`    - cross-compiles the exe
- `GoldenGate-Setup.exe` (in dist) - the per-user installer

## Build the app
On Windows with Visual Studio: compile every `src/*.cpp` into one app, link
against d2d1, dwrite, windowscodecs, dwmapi, ole32, oleaut32, shell32, shlwapi,
propsys, version, powrprof, comctl32, runtimeobject. Subsystem: Windows.
Include `src/app.manifest` (per-monitor-v2 DPI) and `src/app.rc` (icon + version).

Cross-compiling from Linux (what produced the bundled exe):
    sudo apt install mingw-w64 nsis
    ./build.sh            # -> build/GoldenGate.exe

## Build the installer
    cd installer && makensis goldengate.nsi   # -> dist/GoldenGate-Setup.exe
The installer is per-user (no admin): it installs to %LocalAppData%\GoldenGate,
registers a per-user Inter font, adds a Run-key entry, and writes an uninstaller
that runs `GoldenGate.exe --restore` to undo every theme change first.

## Code signing (strongly recommended before sharing)
Unsigned, SmartScreen will warn on first run. To sign:
1. Get a certificate - either Azure Artifact Signing (~$10/month, individuals in
   US/Canada) or an OV certificate on a hardware token (~$150-300/yr). EV no
   longer removes SmartScreen prompts on its own.
2. Sign BOTH the app and the installer:
       signtool sign /fd SHA256 /tr http://timestamp.digicert.com /td SHA256 ^
           dist\GoldenGate.exe
       signtool sign /fd SHA256 /tr http://timestamp.digicert.com /td SHA256 ^
           dist\GoldenGate-Setup.exe
3. SmartScreen reputation builds up over downloads; a signed installer from a
   consistent certificate clears the warning fastest.

## Notes on scope
This is the "no-patching" build: it changes nothing in Windows itself and
injects no code into other processes, so Defender/Smart App Control leave it
alone and Windows updates can't break it. The deeper macOS touches that would
need system patching (left-side traffic-light buttons on every window, a custom
dial clock on the real lock screen, restyling the real taskbar) are deliberately
out, per the research report.
