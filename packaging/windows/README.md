# Windows installer

`radmarky.iss` contains an explicit manifest of application files selected from
`build/release/Release`. It does not package that directory with a wildcard, so
test executables, import libraries, and other build artifacts are excluded. The
installer bundles Microsoft's current Visual C++ v14 x64 Redistributable and
installs or upgrades it automatically when required.

## Build

1. Build `radmarky_viewer` with the CMake `release` preset.
2. If `app-icon.svg` changed, run `.\tools\update-app-icon.ps1` first so the
   ICO, executable, and installer stay in sync.
3. Install Inno Setup 6.
4. From the repository root, run:

   ```powershell
   .\packaging\windows\build-installer.ps1
   ```

The script reads the application version from `cmake/Version.cmake`, downloads
the redistributable from Microsoft's `https://aka.ms/vc14/` permalink if it is
not already cached, verifies its Microsoft Authenticode signature, and writes
the finished installer and a companion `.sha256` checksum file to
`out/installers`. The checksum file contains the lowercase SHA-256 digest,
two spaces, and the installer filename so standard checksum tools can verify it.

Use `-ReleaseDirectory`, `-VCRedistPath`, or `-InnoCompiler` to override the
defaults. The redistributable is embedded in the finished setup executable, so
the target computer does not need internet access during installation.
