# Local build map

Use this file so you do not search for Qt, ITK, or VTK, and so you do not rebuild them.

Compiling ITK and VTK from source takes on the order of **two hours**. Restore existing binaries instead.

Paths below are relative to the repository root unless noted.

## Where things already are

| What | Path |
|---|---|
| Application source | `.` |
| vcpkg checkout and `vcpkg.exe` (`VCPKG_ROOT`) | `build/vcpkg-tool` |
| Manifest-installed Qt, ITK, VTK, and other deps | `build/release/vcpkg_installed` |
| vcpkg binary cache (zipped built packages) | `%LOCALAPPDATA%/vcpkg/archives` |
| CMake / Visual Studio 2022 Release tree | `build/release` |
| Runnable app | `build/release/Release/radmarky_viewer.exe` |

Dependencies are declared in `vcpkg.json`. There is no separate system Qt/ITK/VTK install to find.

## Do not

- Delete `build/`, `build/release/vcpkg_installed`, `build/vcpkg-tool`, or the vcpkg binary cache.
- Run `cmake --fresh`, wipe the CMake cache to “start clean”, or reconfigure with `VCPKG_ROOT` unset.
- Run a source rebuild of the `itk` or `vtk` vcpkg ports.

An unset `VCPKG_ROOT` makes the `release` preset store `CMAKE_TOOLCHAIN_FILE` as `/scripts/buildsystems/vcpkg.cmake`. A later configure with `VCPKG_ROOT` set can then treat the install as new and empty `vcpkg_installed`.

## Incremental app build (preferred)

Run from the repository root.

```powershell
$env:VCPKG_ROOT = "$PWD\build\vcpkg-tool"
cmake --build --preset release --target radmarky_viewer
./build/release/Release/radmarky_viewer.exe
```

Reconfigure only when `CMakeLists.txt` or `CMakePresets.json` changed, and keep the existing install:

```powershell
$env:VCPKG_ROOT = "$PWD\build\vcpkg-tool"
cmake --preset release -DVCPKG_MANIFEST_INSTALL=OFF
cmake --build --preset release --target radmarky_viewer
```

clang-tidy uses the existing Release `vcpkg_installed` tree. Do not create a
separate CMake preset or reconfigure just to generate a compilation database:

```powershell
.\tools\run-clang-tidy.ps1
```

## If `vcpkg_installed` is missing or incomplete

Restore from the binary cache. This unpacks already-built packages; it must not compile ITK or VTK.

```powershell
$env:VCPKG_ROOT = "$PWD\build\vcpkg-tool"
$env:VCPKG_BINARY_SOURCES = "clear;files,$env:LOCALAPPDATA\vcpkg\archives,read"
& "$env:VCPKG_ROOT\vcpkg.exe" install `
  --triplet x64-windows `
  --x-manifest-root=$PWD `
  --x-install-root="$PWD\build\release\vcpkg_installed" `
  --only-binarycaching
```

`--only-binarycaching` fails instead of compiling if a package is not in the cache.

If that command restores nothing because ABI hashes no longer match (for example after a partial vcpkg reinstall), unpack the latest matching zips from the archives directory into `build/release/vcpkg_installed/x64-windows`. Include `bin`, `lib`, `include`, `share`, `debug`, `tools`, `Qt6`, `qml`, and `metatypes`. Then reconfigure with `-DVCPKG_MANIFEST_INSTALL=OFF` so CMake does not start a source rebuild.

The `release-static` preset uses `x64-windows-static-md` in a separate `build/release-static` tree. Do not point that install at `build/release/vcpkg_installed`; a mixed-triplet install in the working tree removes dynamic packages. Static Qt/ITK/VTK binaries are not in the local cache and must not be compiled from source unless explicitly requested.

## Starting from scratch on a new machine

1. Install CMake 3.25+, Visual Studio 2022 C++ tools, and clone vcpkg into `build/vcpkg-tool` (or another checkout you point `VCPKG_ROOT` at).
2. Set `VCPKG_ROOT` **before** the first `cmake --preset`.
3. Copy or point `VCPKG_BINARY_SOURCES` at an existing archives directory if you have one, then configure. Expect a long first build only when the cache cannot supply ITK and VTK.
4. After that first success, reuse `vcpkg_installed` and the archives directory as above.

# DICOM geometry

RadMarky renders all loaded images in patient LPS coordinates. The fixed slice
mapping is radiological: the left side of axial and coronal images is the
patient's right side.

The fixed display mapping is:

| View | Display code | Screen horizontal | Screen vertical | Slice normal |
| --- | --- | --- | --- | --- |
| Axial | `RPS` | right to left (`+LPS X`) | posterior to anterior (`-LPS Y`) | superior to inferior (`-LPS Z`) |
| Sagittal | `AIR` | anterior to posterior (`+LPS Y`) | inferior to superior (`+LPS Z`) | right to left (`+LPS X`) |
| Coronal | `RIP` | right to left (`+LPS X`) | inferior to superior (`+LPS Z`) | posterior to anterior (`-LPS Y`) |

## Import rules

Before this work, RadMarky selected one Series Instance UID and asked
`gdcm::IPPSorter` to sort the files. It did not validate that the selected
files formed one unambiguous spatial stack. Missing positions fell back to
Instance Number or filename order, duplicate positions were accepted, and
non-uniform spacing was represented as a uniformly sampled ITK image.

`itk::ImageSeriesReader` also defaults `ForceOrthogonalDirection` to `true`.
That default discards the in-plane component of the first-to-last slice
displacement and therefore flattens gantry tilt. RadMarky disables that
option for DICOM series, as recommended by the ITK API, so a uniform tilted
stack retains its non-orthogonal direction matrix.

For a classic multi-file slice stack, every selected file must provide:

- Image Position (Patient)
- Image Orientation (Patient)
- Pixel Spacing
- Rows and Columns

RadMarky orders slices by Image Position projected onto the normal formed by
the two Image Orientation direction cosines. It rejects:

- invalid or changing in-plane direction cosines
- changing dimensions, pixel spacing, or Spacing Between Slices metadata
- mixed or partially missing Frame of Reference UIDs
- repeated SOP Instance UIDs or duplicate slice positions
- gaps consistent with missing slices
- non-uniform spacing
- slice positions that do not follow one consistent stack direction

A uniform gantry tilt is accepted and preserved. A single DICOM file is left
to GDCM/ITK because it may be a valid multi-frame object whose per-frame
geometry is stored in functional groups.

The import dialog offers an explicit override when the only issue is either
non-uniform distance between otherwise collinear slice positions or a uniform
position-derived spacing that disagrees with the declared Spacing Between
Slices value. Changing, invalid, or partially missing spacing metadata remains
a hard rejection.

## Tests

RadMarky's automated tests cover the fixed `RPS/AIR/RIP` mapping, known
patient-right and patient-left marker sampling, physical-space reslicing,
preservation of non-identity direction matrices, position-based DICOM ordering,
tilted stacks, and each rejected ambiguous-stack case. Multi-volume
same-position grouping is not supported because the importer currently loads
one scalar 3-D volume, not a multi-component or 4-D acquisition.
