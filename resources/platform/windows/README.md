# Windows Application Icon

`radmarky.ico` is the Windows packaging derivative of
[`../../icons/app-icon.svg`](../../icons/app-icon.svg). It contains 16, 20, 24,
32, 40, 48, 64, 128, and 256 pixel 32-bit icon entries.

Edit the SVG source rather than the ICO. Regenerate the ICO whenever the source
artwork changes:

```powershell
.\tools\update-app-icon.ps1
```

That rasterizes the SVG with Qt's renderer, embeds the ICO in
`radmarky_viewer.exe`, and rebuilds the Windows installer so desktop and Start
Menu shortcuts use the new icon. Pass `-SkipInstaller` to update only the ICO
and executable.
