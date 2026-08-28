# RadMarky Viewer Annotation Validation

RadMarky Viewer runs validators with an installed CPython 3.11 or newer and
calls one application-facing function:

```python
def validate(annotation_path: str, context: dict) -> None | str:
    ...
```

Return `None` to accept the current annotation. Return a non-empty string to
reject it. A validator can instead return `(message, slice_number)` to reject
and move the viewer to that issue. `slice_number` is a positive, one-based
index along the NIfTI volume's third dimension. Each call receives a disposable
NIfTI snapshot containing current unsaved edits. The context provides:

- `intended_destination_path`
- `anatomical_image_source_path`
- `active_annotation_name`
- `dimensions`, `spacing`, `origin`, and `direction`
- `companion_json_path` when one exists

Validation code runs in a separate process with the user's permissions. It is
not sandboxed and can access files or start other programs, so add only trusted
scripts. RadMarky forcibly stops the validator process on cancellation or
timeout, including while native extension code is running.

## Interpreter discovery

RadMarky first checks the `RADMARKY_PYTHON_EXECUTABLE` environment variable,
then the interpreter selected when the application was built, and finally
`python3`, `python`, or the Windows `py` launcher on `PATH`.

A validator's imports must be installed in that same environment:

```powershell
python -m pip install --user numpy nibabel
```

## Bundled presets

The Validation Management dialog includes small continuity, non-empty, and
allowed-label examples. They are disabled by default and their source can be
inspected in the built-in code viewer before enabling them.
