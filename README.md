# Fast Palette

A Windows 11 launcher and calculator built with C++ and Qt Widgets. One portable executable, no installation or runtime extraction.

## Use

Run `dist/FastPalette.exe`. The app stays in the system tray.

- Tap a Windows key or press **Ctrl+Alt+Space** to open.
- Type to search or calculate. **Enter** launches or copies; **Escape** closes.
- Open **Settings** to change shortcuts, add portable apps, or enable launch at sign-in.
- Configured Windows shortcuts replace their normal action while the app runs. Other shortcuts pass through. Windows security shortcuts remain reserved.

Examples: `2+3*4`, `sqrt(81)`, `2^10`, `sin(pi/2)`. Supports `+ - * / ^ %`, parentheses, `pi`, `e`, `sqrt`, `abs`, `sin`, `cos`, `tan`, `ln`, and `log10`. Angles use radians; `10%` means `0.1`. Prefix with `=` for calculation only.

Settings are stored in `HKCU\Software\FastPalette`. Disable launch at sign-in before moving the executable. Use the tray menu to exit.

## Build

Requires Visual Studio C++ Build Tools, Windows SDK, CMake 3.24+, Ninja, and Git.

```powershell
./build.ps1
./tools/package.ps1
```

The first build compiles static Qt; subsequent builds reuse it. Close Fast Palette before building. The build runs the test suite; packaging creates `dist/FastPalette.exe`.

See [distribution](docs/distribution.md) for packaging and Qt source/relink materials.
