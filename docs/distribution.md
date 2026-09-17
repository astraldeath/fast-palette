# Distribution

Run `./tools/package.ps1` after a successful Release build.

- `dist/FastPalette.exe`: the only runtime file.
- `build/FastPalette-relink-kit.zip`: Qt source, licenses, application objects, and rebuild/relink scripts.
- `build/package/`: dependency audit and SHA-256.

Packaging requires static Qt and the static MSVC runtime, and rejects non-Windows DLL dependencies. Preserve or move existing `build/package`, `build/relink-kit`, and `build/FastPalette-relink-kit.zip` before packaging again. Unrelated files in `dist` are never overwritten.

## Qt licensing

Fast Palette uses Qt under LGPLv3. Notices are embedded in the executable under **About and licenses**. When distributing the executable, also provide access to the matching source/relink archive in accordance with the included licenses. The archive is not needed to run the app.

To relink, extract the archive and enter `relink-kit`. Its Qt source archive and `rebuild-qt.ps1` support building a modified Qt. Then run:

```powershell
./relink.ps1 -QtPrefix C:\path\to\static-qt
```

Use a compatible x64 Release Qt build with the static MSVC runtime. Output: `relink-build/FastPalette.exe`.
