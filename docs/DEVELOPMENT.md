# RadMarky Viewer Development

This document defines practical rules for minimizing crashes in RadMarky Viewer.
It is intentionally specific to the project's C++, Qt, ITK, and VTK stack.

## 1. Make ownership explicit

Most hard-to-diagnose crashes in this stack are lifetime errors. Every pointer
should have an identifiable owner.

- Use `std::unique_ptr` for exclusive C++ ownership.
- Use `std::shared_ptr` only when ownership is genuinely shared.
- Use `itk::SmartPointer<T>` for owned ITK objects.
- Use `vtkSmartPointer<T>` for owned VTK objects.
- Use Qt parent ownership for `QObject` and widget hierarchies.
- Treat pointers returned by library getters as borrowed unless the API
  explicitly documents ownership transfer.
- Do not retain raw pointers across asynchronous work, pipeline replacement, or
  owner destruction.

Avoid `auto` when it hides whether a library call returns a raw pointer or a
smart pointer. State the type at ownership boundaries:

```cpp
Volume::ImageType::Pointer image = reader->GetOutput();
image->DisconnectPipeline();
```

This is important because `GetOutput()` returns a raw pointer. Holding an ITK
smart pointer before disconnecting the pipeline prevents the image from being
destroyed while it is still in use.

## 2. Keep the ITK-to-VTK boundary narrow

- Perform image conversion in the designated bridge, not in arbitrary widgets.
- Document whether conversion copies or shares pixel memory.
- When memory is shared, keep the source image alive for the entire VTK use
  period and test teardown order.
- Validate dimensions, scalar type, region, and buffer availability before
  copying.
- Keep the VTK pipeline persistent. Update inputs and parameters rather than
  repeatedly destroying and reconstructing pipeline objects.

The current bridge performs one controlled pixel copy. This costs memory but
gives the VTK image independent, predictable ownership.

## 3. Validate data before using it

Reject invalid state near the boundary where it enters the application.

- Require non-empty image dimensions.
- Require finite, positive spacing.
- Require finite origins and direction matrices.
- Reject singular transforms.
- Check integer conversions before passing dimensions to VTK extents.
- Check files exist before invoking readers.
- Convert library exceptions into contextual application errors.
- Never continue rendering after a failed load with partially updated state.

Load new data into temporary objects first. Replace the displayed volume only
after reading, geometry construction, and renderer setup succeed.

## 4. Respect GUI and rendering thread rules

- Create and modify Qt widgets only on the GUI thread.
- Perform VTK render-window operations on the GUI thread unless a specific VTK
  API explicitly guarantees otherwise.
- Do not update a widget from a worker callback directly; deliver the result
  through a queued Qt signal.
- Give background jobs owned cancellation and completion state.
- Do not capture raw widget pointers in long-running callbacks.
- During shutdown, cancel or join background work before destroying its inputs
  or observers.

Future asynchronous loading should produce a fully owned `Volume` in the
worker, then transfer that result to the GUI thread for display.

## 5. Handle Qt, ITK, and VTK callbacks defensively

- Prefer Qt connections with a receiver context so they disconnect when the
  receiver is destroyed.
- Store and remove VTK observer tags when callbacks can outlive their owner.
- Avoid callbacks that mutate the collection currently invoking them.
- Keep callback captures small and ownership-aware.
- Guard optional state before dereferencing it.
- Make repeated initialization and teardown safe.

## 6. Preserve exception boundaries

- Do not allow exceptions to escape Qt event handlers, VTK callbacks, thread
  entry points, or C-compatible callbacks.
- Catch exceptions at these boundaries, log context, and return the application
  to a valid state.
- Keep destructors `noexcept` in practice: they should release resources, not
  perform fallible application work.
- Report user-actionable failures through the UI while retaining detailed logs
  for diagnosis.

## 7. Guard numerical and coordinate operations

- Check for finite values before building transforms or camera parameters.
- Treat direction matrices, origin, and spacing as part of image identity.
- Use physical coordinates as the authoritative navigation space.
- Test non-zero origins, anisotropic spacing, non-identity directions, oblique
  images, and boundary indices.
- Never assume array indices are valid because a screen or world coordinate
  appears visually plausible.
- Clamp only when clamping is the intended interaction behavior; otherwise
  reject invalid input visibly.

Incorrect geometry often causes silent corruption before it causes a crash, so
geometry checks should be as strict as memory checks.

## 8. Make loading transactional

A file-open operation should follow this order:

1. Validate the path.
2. Read into a new ITK image.
3. Acquire owned output before disconnecting the reader pipeline.
4. Construct and validate `ImageGeometry`.
5. Build or update the VTK representation.
6. Commit the new volume to application state.
7. Update titles, status text, and logs.

If any step fails, preserve the previously loaded volume and viewer state.

## 9. Test lifetimes as well as values

Every new I/O or rendering path should have tests covering both correctness and
object lifetime.

Minimum checks for a new reader or bridge:

- Construct, load, use, and destroy the result.
- Destroy the reader before accessing the loaded image.
- Replace one loaded volume with another.
- Exercise failure before and after allocation.
- Repeat load/unload operations.
- Verify metadata and representative pixels.
- Run the test in both Debug and Release when practical.

Small synthetic files are preferred because they are deterministic and make
orientation markers easy to verify.

## 10. Use diagnostic builds during development

- Keep compiler warnings enabled and treat new warnings as defects.
- Run `.\tools\run-clang-tidy.ps1` after relevant C++ changes and treat new
  project diagnostics as defects. The configured checks favor lifetime, null,
  exception-boundary, and copy issues over style nits.
- Run `ctest --output-on-failure` after relevant changes.
- Use AddressSanitizer on supported compiler configurations for focused memory
  investigations.
- Use debugger stack traces for access violations; do not infer the cause only
  from the last visible UI action.
- Add temporary stage logging to isolate a crash, then remove it once the cause
  is covered by a permanent test.
- Avoid relying only on a Release smoke test, since optimization can expose or
  conceal lifetime bugs.

## 11. Use SVG for all icons

Every user-interface icon must use SVG as its source format.

- Store icon assets as `.svg` files under `resources/icons/`.
- Do not add PNG, JPEG, GIF, WebP, or other raster files for icons.
- Do not keep raster fallback copies of an SVG icon in the repository.
- Embed SVG icons through Qt's resource system so packaged builds do not depend
  on source-tree paths.
- Give each SVG an appropriate `viewBox` and verify that it remains legible at
  the small sizes used by the toolbar.
- Keep icon backgrounds transparent and avoid embedding raster images inside
  SVG files.
- Preserve accessible names and tooltips on icon-only controls.
- If an icon asset exists, load the SVG rather than duplicating the artwork in
  widget-specific `QPainter` code.

Native application packaging files are the sole exception. Platform-required
derivatives such as Windows `.ico` and macOS `.icns` files must be generated
from the canonical SVG, stored under `resources/platform/<platform>/`, and used
only for executable or bundle metadata. They must not be used by Qt controls.

Raster medical images, screenshots, and image thumbnails are not UI icons and
are outside this rule. Code review should reject any newly introduced raster
icon asset.

## 12. Release gate

Before considering a milestone complete:

- The configured Release build succeeds.
- The complete automated test suite passes.
- The packaged application launches and closes cleanly.
- New file workflows are exercised with representative valid and invalid data.
- `git diff --check` passes.
- No temporary diagnostics or disabled assertions remain.
- Newly introduced raw pointers have documented borrowed lifetimes.
- Error paths leave the application usable.

## 13. Crash response checklist

When a crash is found:

1. Record the exact input and user action.
2. Reproduce it with the smallest deterministic case.
3. Identify the last confirmed stage without assuming it is the cause.
4. Obtain a stack trace or narrowly instrument the boundary.
5. Audit ownership and teardown order first.
6. Fix the underlying lifetime or state contract.
7. Add a regression test that fails without the fix.
8. Remove temporary diagnostics.
9. Run the full suite and a GUI smoke test.

Do not suppress an access violation with a null check unless null is a valid
documented state. The goal is to repair the ownership or state transition that
made the invalid access possible.
