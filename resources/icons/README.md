# Icon Resources

The application-wide icon directory is the permanent location for SVG icon
assets. UI icons must not be stored in a raster format.

- `zoom-in.svg`
- `zoom-out.svg`
- `reset-view.svg`
- `app-icon.svg` — canonical application artwork. After changing it, run
  `.\tools\update-app-icon.ps1` to regenerate the Windows `.ico`, embed it in
  the executable, and rebuild the installer.
- `cursor.svg`
- `zoom.svg`
- `pan.svg`
- `contrast.svg`
- `invert.svg` — Timothée Giet, CC BY-SA 4.0; attribution and license metadata
  are embedded in the SVG supplied through SVG Repo.
- `measure.svg` — GIS ruler artwork supplied through SVG Repo. Verify the
  upstream license before distribution or replace it with RadMarky artwork.
- `camcorder.svg` — user-supplied SVG Repo artwork, normalized and recolored
  for RadMarky's toolbar. Verify the upstream license before distribution or
  replace it with RadMarky artwork.

Record the source and license for every third-party SVG before distribution, or
replace it with RadMarky's own artwork.

## Usage

SVG assets should be embedded through Qt's resource system and used directly by
the controls they represent. See
[`docs/DEVELOPMENT.md`](../../docs/DEVELOPMENT.md)
for the project-wide icon policy.
