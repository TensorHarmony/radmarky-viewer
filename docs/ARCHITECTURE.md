# RadMarky Viewer Architecture

This document describes the architecture that exists in the repository today.
It is intended for contributors who want to understand the design before
changing it. For prospective work, see [ROADMAP.md](ROADMAP.md).

RadMarky Viewer is an independent C++20 desktop application built with Qt 6,
ITK, and VTK. It follows established medical-viewer conventions while retaining
its own focused architecture, code, assets, and product identity.

## Design goals

The architecture favors:

- correct patient-space geometry over visually plausible shortcuts
- a small application-owned data model
- explicit ownership at Qt, ITK, and VTK boundaries
- persistent rendering pipelines rather than rebuilding them during interaction
- transactional loading that preserves the current workspace on failure
- focused dependencies and independently testable non-UI logic

The application currently focuses on one anatomical 3-D volume with zero or
more annotation layers. It is not a general layer graph or medical-imaging
workstation framework.

## System overview

```text
                       Qt application
               MainWindow, dialogs, settings
                    /          |          \
                   /           |           \
          import workflow   viewer UI   validation UI
                |               |              |
                v               v              v
        readers and checks  OrthogonalViewer  validation service
                |               |              |
                v               v              v
          core data model   ITK/VTK bridge   CPython process
                |               |
                +-------+-------+
                        |
                 patient-space LPS
```

The main source areas are:

| Area | Responsibility |
| --- | --- |
| `src/core/` | Volumes, geometry, viewer state, annotations, editing, measurement, and window/level rules |
| `src/io/` | NIfTI and DICOM I/O, DICOM validation, archive extraction, GIF, and MP4 output |
| `src/rendering/` | ITK-to-VTK bridging, orthogonal reslicing, overlays, and viewport interaction |
| `src/ui/` | Main window, dialogs, tool panels, themes, and presentation logic |
| `src/validation/` | Annotation validation orchestration and out-of-process Python execution |
| `src/app/` | Application metadata and persistent user settings |

The CMake targets reinforce these boundaries:

- `radmarky_app_info` owns version and release metadata.
- `radmarky_core` contains the data model and image I/O without Qt widget or VTK
  rendering dependencies.
- `radmarky_rendering_core` contains the testable ITK-to-VTK bridge and reslice
  configuration.
- `radmarky_validation` contains validation logic and depends on Qt Core, not
  Qt Widgets.
- `radmarky_viewer` composes the libraries with the Qt and VTK user interface.

## Core data model

### Volume and geometry

`core::Volume` owns an `itk::Image<float, 3>`, its scalar range, optional RGB
display data, displayable DICOM metadata, and any measured gaps between the
ordered source DICOM slice positions. The associated
`core::ImageGeometry` stores dimensions, spacing, origin, direction, and the
inverse direction needed for coordinate conversion.

Geometry is part of image identity. Code must not compare volumes by dimensions
alone or silently discard origin, spacing, or direction.

`core::ViewerState` owns the authoritative cursor in physical coordinates,
along with window/level and grayscale inversion state. Slice views derive their
positions from that shared cursor instead of maintaining independent slice
indices.

### Annotations

`core::Annotation` wraps a volume as either:

- a label map with finite integer values from 0 through 65,535, or
- a floating-point scalar map.

Each annotation also owns its name, source path, opacity, modification state,
and label-value ledger. Multiple annotations may be displayed, but editing is
limited to a selected label map.

`core::AnnotationEditor` applies brush and erase operations directly to the
selected label map. One complete stroke is one undo command. Undo and redo store
only changed voxel offsets and values rather than full volume copies.

## Coordinate and display geometry

All loaded images are interpreted in patient LPS physical coordinates. The
application distinguishes among:

```text
voxel index
    <-> continuous image index
    <-> ITK physical / patient LPS coordinates
    <-> VTK world coordinates
    <-> display coordinates
```

`core::ImageGeometry` performs index-to-physical and physical-to-continuous-index
conversion. `core::OrthogonalSliceGeometry` derives each view plane from the
image geometry and maps points to horizontal, vertical, and normal coordinates.

The fixed display mapping is radiological and follows these directions:

| View | Screen horizontal | Screen vertical | Slice normal |
| --- | --- | --- | --- |
| Axial | patient right to left (`+LPS X`) | posterior to anterior (`-LPS Y`) | superior to inferior (`-LPS Z`) |
| Sagittal | anterior to posterior (`+LPS Y`) | inferior to superior (`+LPS Z`) | right to left (`+LPS X`) |
| Coronal | patient right to left (`+LPS X`) | inferior to superior (`+LPS Z`) | posterior to anterior (`-LPS Y`) |

This mapping is tested with non-zero origins, anisotropic spacing,
non-identity directions, oblique geometry, and known patient-right and
patient-left markers. A view that looks reasonable is not considered correct
unless the physical-coordinate mapping is also correct.

## Image import

The input pipeline supports NIfTI volumes, loose DICOM files, recursively
scanned DICOM folders, ZIP archives, and TAR.GZ archives.

```text
input files or dropped folders
    -> classify input
    -> safely extract an archive when needed
    -> scan every file by content for DICOM metadata
    -> group Series Instance UIDs and separate defensible fused stacks
    -> review candidate resolution, spacing, and consistency
    -> select one consistent series or compatible parts of one Series Instance UID
    -> decode pixels into a temporary Volume
    -> install the Volume in the viewer
```

Archive extraction rejects entries that could escape the application-owned
temporary directory. Extraction, DICOM scanning, and DICOM pixel loading run as
cancellable background work. Results return to the GUI thread through Qt; stale
results are ignored by generation checks.

Loading is transactional. A new volume is committed only after reading,
geometry validation, and viewer preparation succeed. Failure or cancellation
leaves the current volume and annotations unchanged.

### DICOM stack validation

Series discovery does not depend on a `.dcm` filename extension. Readable image
objects are first grouped by Series Instance UID. When one UID contains a
non-uniform fused stack, the importer separates independently uniform
acquisitions using Acquisition Number or distinct declared slice spacing. As a
fallback, it recognizes a measured spacing transition only when both sides are
complete uniform stacks with different spacing and the boundary belongs to
neither grid. This conservative rule keeps a stack with missing slices from
being silently truncated into apparently valid pieces.

When discovery finds exactly one consistent candidate, it is imported
automatically without showing the review dialog. Otherwise, the review dialog
presents one row per detected candidate, including its Series Description,
comma-separated filenames, voxel resolution, measured slice spacing, and all
detected consistency problems. Filenames are listed in the
same patient-position order used to load the slices, independent of input or
drag order. Double-clicking the filenames cell copies the complete list to the
clipboard; the Series Description and Series Instance UID cells can be copied
the same way. Unreadable files and DICOM objects without a Series Instance UID
are listed as ignored; candidates not chosen by the user are left behind.
Multiple detected parts can be selected together only when they share one
Series Instance UID. Their combined geometry is validated again and requires
the same explicit spacing-override confirmation as any other non-uniform
selection.
Inconsistent candidates remain visible for diagnosis and cannot normally be
selected. The table includes stable textual geometry error codes and scrolls
horizontally to keep every diagnostic reachable. An explicit override is
available when the candidate's only problem is a gap consistent with missing
slices, non-uniform distance between otherwise collinear slice positions, or a
uniform position-derived spacing that disagrees with the declared Spacing
Between Slices value. Such candidates remain unselected by default, are shown
as warnings, and can be imported only after confirmation. A non-uniform or
missing-slice stack is represented by an ITK volume with one uniform
slice-axis spacing, so the override cannot preserve every original slice
position exactly or reconstruct missing anatomy. The cursor inspector reports
the preserved source gap to the previous and next slice, allowing an imported
spacing anomaly to remain visible even though the output grid is uniform.

Classic multi-file DICOM stacks are ordered by Image Position (Patient)
projected onto the normal formed from Image Orientation (Patient). A stack must
have consistent spatial metadata, dimensions, pixel spacing, frame of reference,
and slice direction.

The importer rejects:

- missing or invalid spatial metadata
- changing in-plane direction cosines
- changing dimensions or pixel spacing
- changing, invalid, or partially missing spacing metadata; a sole disagreement
  between declared spacing and otherwise uniform slice positions can be imported
  through the explicit override described above
- mixed or partially missing Frame of Reference UIDs
- duplicate SOP Instance UIDs or duplicate slice positions
- gaps consistent with missing slices; this issue can be imported only through
  the explicit missing-slice override described above
- irregular spacing or inconsistent stack direction; isolated non-uniform
  spacing can be imported only through the explicit override described above

A uniform gantry tilt is accepted. The DICOM reader disables ITK's forced
orthogonal direction behavior so the in-plane component of slice displacement
is preserved. A single DICOM file is delegated to GDCM/ITK because it may be a
valid multi-frame object with per-frame geometry.

## Rendering

ITK remains the owner of anatomical and annotation image data. VTK is used for
reslicing, window/level display, overlays, crosshairs, rulers, and interaction.

`rendering::ItkVtkImageBridge` is the controlled boundary. For ordinary volume
display it creates VTK image metadata and attaches the ITK-owned pixel buffer
without copying it. The viewer retains the owning `Volume` for at least as long
as VTK can access that buffer. Derived annotation-comparison images receive
their own VTK allocation.

Each viewport keeps a persistent VTK pipeline. Moving the cursor, changing
window/level, or navigating slices updates pipeline inputs and parameters rather
than rebuilding the renderer.

Label maps use nearest-neighbor reslicing so interpolation cannot invent label
values. Scalar annotations use linear interpolation. Annotation opacity affects
only presentation, never stored voxel values.

## User-interface orchestration

`ui::MainWindow` coordinates file workflows, recent workspaces, settings,
dialogs, validation, and the central `rendering::OrthogonalViewer`. UI widgets
do not own independent copies of medical-image geometry.

Qt widgets and VTK render windows are modified on the GUI thread. Long-running
archive, DICOM, and validation operations use background work with explicit
cancellation and queued GUI updates. Callbacks use guarded Qt object references
where their lifetime can cross an asynchronous boundary.

`app::UserSettings` persists theme, always-on-top state, window/level defaults
and presets, recent-image metadata, annotation workspace references, registered
validators, and window layout in an application-owned settings file.

## Validation and saving

Only editable label maps can be saved as annotations. The save path uses an
immutable candidate snapshot so validation and the final write operate on the
same data.

Trusted Python validators execute in a separate installed CPython process. The
application does not embed Python. Each validator receives a temporary NIfTI
candidate and JSON context, then returns a structured pass, rejection, error,
timeout, or cancellation result. Validators are not sandboxed and run with the
current user's permissions.

Validation failures do not modify the live annotation. A rejection may include
a one-based axial slice number so the viewer can navigate to the problem.

## Export

The viewer can capture still images from any orthogonal viewport. Animated
exports sample a requested physical slice range and pass frames to the GIF or
MP4 writer. Export can preserve the current view transform, crosshair choice,
and visible overlays.

## Error handling and ownership

The following rules apply across subsystems:

- Acquire owned ITK outputs before disconnecting reader pipelines.
- Use ITK and VTK smart pointers for framework-owned objects.
- Use Qt parent ownership for widgets and context-bound signal connections.
- Treat raw pointers returned by library getters as borrowed unless documented
  otherwise.
- Catch exceptions at Qt event handlers, callbacks, and worker boundaries.
- Reject invalid state close to the boundary where it enters the application.
- Preserve the current usable state when an operation fails.

More detailed implementation rules are in
[DEVELOPMENT.md](DEVELOPMENT.md).

## Testing strategy

Tests favor small synthetic inputs with known geometry. The suite covers:

- index/physical transformations and fixed display orientation
- NIfTI load/save and geometry preservation
- DICOM UID grouping, fused-stack separation, extensionless files, ignored
  inputs, spatial ordering, invalid stacks, and gantry tilt
- safe archive extraction
- ITK-to-VTK reslicing and annotation alignment
- brush geometry, editing, comparison, and undo/redo
- cursor sampling and window/level behavior
- persistent settings and recent workspaces
- validation engine and save gating
- relevant dialogs and animated export

New geometry-sensitive features should include identity, translated,
anisotropic, rotated or reflected, and oblique fixtures where applicable.

## Extending the system

Before introducing a new abstraction or dependency:

1. Define the required user-visible behavior.
2. Identify the existing ownership and coordinate boundaries it crosses.
3. Put processing in `core`, `io`, `rendering`, or `validation`, not directly in
   a widget event handler.
4. Keep framework conversion at the designated boundary.
5. Specify failure and cancellation behavior.
6. Add tests for geometry, lifetime, and state preservation.
7. Add infrastructure only when a concrete feature requires it.
