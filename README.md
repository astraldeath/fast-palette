# Fast Palette

A Windows 11 launcher, calculator, and search palette built with C++ and Qt Widgets. One portable executable, no installation or runtime extraction.

## Use

Download `FastPalette.exe` from [Releases](https://github.com/astraldeath/fast-palette/releases/latest) and run it. The app stays in the system tray.

- Tap a Windows key or press **Ctrl+Alt+Space** to open.
- Type to search or calculate. **Enter** launches or copies; **Escape** closes.
- Open **Settings** to change shortcuts, add portable apps, or enable launch at sign-in.
- Under **Settings → Search**, toggle applications, calculator, Windows Settings, paths, and Everything independently.
- Search settings by name (`Bluetooth`, `Windows Update`), or open a path such as `%appdata%`, `%temp%`, or `C:\Users`.
- Enable **Everything** to search its index while Everything is running. Prefix with `? ` for file results only; Everything search syntax is supported.
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

## Releases

Push to `main` with Conventional Commits: `fix:` or `perf:` bumps the patch version, `feat:` bumps minor, and `!` or `BREAKING CHANGE:` bumps major. Other commits do not release. Pushing a `vMAJOR.MINOR.PATCH` tag publishes that version directly. Releases include the executable, SHA-256 checksum, and Qt relink kit.
