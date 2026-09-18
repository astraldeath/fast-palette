# Fast Palette

A Windows 11 launcher, calculator, and search palette built with C++ and Qt Widgets. One portable executable, no installation or runtime extraction.

## Use

Download `FastPalette.exe` from [Releases](https://github.com/astraldeath/fast-palette/releases/latest) and run it. The app stays in the system tray.

- Tap a Windows key or press **Ctrl+Alt+Space** to open.
- Type to search or calculate. **Enter** launches or copies; **Escape** closes.
- Open **Settings** to change shortcuts, add portable apps, or enable launch at sign-in.
- Under **Settings → Search**, toggle each module, choose its prefix, or restrict it to prefixed searches.
- Right-click a result for actions. **Ctrl+Shift+Enter** runs as administrator, **Ctrl+Enter** opens its containing folder, and **Ctrl+Shift+C** copies its path.
- Create named app, folder, or command shortcuts under **Settings → Aliases**. Enter arguments separately from the target.
- Search settings by name (`Bluetooth`, `Windows Update`), or open a path such as `%appdata%`, `%temp%`, or `C:\Users`.
- Enable **Everything** to search its index while Everything is running. Its prefix (default `? `) shows only Everything results. Enable **Only with prefix** to exclude it from ordinary searches. Everything syntax passes through unchanged, including `ext:pdf`, `size:>1mb`, wildcards, Boolean operators, and `regex:`.
- Configured Windows shortcuts replace their normal action while the app runs. Other shortcuts pass through. Windows security shortcuts remain reserved.

Calculator examples: `2pi`, `2(3+4)`, `sqrt(81)`, `2^10`, `100!`, `sin(pi/2)`. Supports `+ - * / ^ % !`, parentheses, `pi`, `e`, `sqrt`, `abs`, `sin`, `cos`, `tan`, `asin`, `acos`, `atan`, `ln`, `log`/`log10`, `round`, `floor`, `ceil`, and `trunc`. Factorials accept 0–170; large results use scientific notation. Angles use radians unless degrees are enabled in Search settings. `10%` means `0.1`; `log` is base 10. The default calculator prefix is `=`.

Convert length, weight, temperature, time, storage, or transfer rates: `180cm to ftin`, `5'11" to cm`, `74in to ftin`, `2gb to mb`, `2 GiB to MiB`, `72f to c`. `ftin` means feet and inches. Storage shorthand `gb`/`mb` means decimal bytes; `GiB`/`MiB` uses 1024. Speeds respect case: `2 MB/s` or `2 MBps` gives `16 Mbps`; `2 mbps` or `2 mb/s` gives `0.25 MB/s`.

Settings are stored in `HKCU\Software\FastPalette`. Disable launch at sign-in before moving the executable. Use the tray menu to exit.

Updates install automatically when the palette and Settings are closed. Turn this off or check manually under **Settings → Updates**. Updating verifies the release checksum, replaces the portable executable in place, and restarts it; its folder must be writable. Versions before 1.2.0 need one manual download to get the updater.

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
