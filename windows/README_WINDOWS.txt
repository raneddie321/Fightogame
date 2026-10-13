FIGHTO - Windows
================

Run FIGHTO.exe. Keep the "assets" folder next to the executable.

Requirements: 64-bit Windows 10 or 11, a GPU with OpenGL 3.3 support and current drivers.
No installer, no runtime libraries and no game engine are needed: the executable is
statically linked and talks to Windows (Win32, WGL/OpenGL, XInput, waveOut) directly.

Saves, settings and the log are stored in %APPDATA%\FIGHTO.

Command line options:
  --windowed            start in a window instead of fullscreen
  --width N --height N  window size
  --autotest [dir]      run the automated gameplay test (writes autotest_report.txt and screenshots)

Controls (rebindable in Settings > Controls; Xbox-compatible gamepads are supported):
  WASD move | Mouse look | Shift sprint | Space jump / handbrake | C crouch | E interact / enter vehicle
  LMB attack / shoot | RMB block / aim | R reload | Q lock-on | F kick | Ctrl dodge | X/Z next/previous weapon
  Alt walk | Tab journal | M map | Esc pause | F5 quick save | 1/2 dialogue choices | H horn (in vehicles)
