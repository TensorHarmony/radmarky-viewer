# RadMarky Viewer Roadmap

This roadmap describes the project's current priorities and boundaries. It is
not a release-date commitment. Completed behavior is documented in the
[README](../README.md), and the current design is described in
[ARCHITECTURE.md](ARCHITECTURE.md).

## Status definitions

- **Current** — implemented in the repository.
- **Release candidate** — required before the first public stable release.
- **Candidate** — useful work that still needs a focused design and issue.
- **Not planned** — intentionally outside the current product scope.

## Current baseline

RadMarky Viewer currently provides:

- patient-space axial, sagittal, and coronal viewing
- synchronized cursor, navigation, zoom, pan, window/level, and measurement
- NIfTI and defensively validated DICOM import
- ZIP and TAR.GZ DICOM archive import
- multiple label-map or scalar annotation overlays and comparison
- axial label-map painting, erasing, scoped erase, undo, and redo
- NIfTI annotation saving with optional trusted Python validation
- still-image, GIF, and MP4 export
- persistent display settings, layout, recent images, and annotation workspace
  references
- automated geometry, I/O, rendering, editing, validation, and UI-focused tests

## First public stable release

Status: **Release candidate**

The first stable release is about making the existing viewer dependable and
understandable, not expanding its feature set.

- Replace README screenshot placeholders with synthetic or properly
  de-identified examples.
- Verify the documented clean Windows x64 configure, build, test, and installer
  workflow.
- Run the complete test suite and static analysis from the existing Release
  dependency tree.
- Exercise NIfTI, loose DICOM, ZIP, TAR.GZ, annotation editing, validation, and
  export workflows in the packaged application.
- Confirm application and third-party license notices, source attribution, and
  release metadata.
- Publish checksums with the installer and document known limitations.
- Keep the medical-device disclaimer and patient-data handling guidance visible.

## Maintenance priorities

Status: **Candidate**

These improvements fit the existing architecture but should be accepted and
specified individually:

- improve accessibility and keyboard coverage across dialogs and viewer tools
- expand malformed-input and cancellation regression tests
- measure representative DICOM import and rendering performance, then optimize
  only demonstrated bottlenecks
- improve diagnostics without logging patient metadata, pixel data, or full
  source paths at normal log levels
- reduce platform assumptions in the source while keeping Windows x64 as the
  documented supported configuration until another platform is tested
- add focused architecture decision records when a change alters data ownership,
  coordinate conventions, file compatibility, or release dependencies

## Compatibility policy

Status: **Current**

- Preserve NIfTI dimensions, spacing, origin, and direction during supported
  annotation round trips.
- Reject ambiguous DICOM stacks rather than guessing slice order or geometry.
- Preserve the fixed radiological LPS display mapping unless a separately
  designed user preference is introduced with complete geometry tests.
- Keep settings migrations backward compatible where practical.
- Treat changes to the Python validator contract as public compatibility changes.

## Not planned

Status: **Not planned**

The following are outside the current product direction unless a future issue
defines a compelling use case, maintenance cost, and test strategy:

- 3-D volume or mesh rendering
- registration or active-contour segmentation
- bundled automated segmentation
- oblique reformatting controls
- 4-D or time-series editing
- multiple anatomical-volume workspaces
- plugin or scripting APIs beyond the documented annotation-validator contract
- compatibility with another viewer's projects, workspaces, preferences, or
  complete user interface
- cloud inference or upload of medical images

RadMarky may adopt familiar medical-viewer behavior where it improves usability,
but it remains an independent implementation with its own code, assets, scope,
and product identity.
