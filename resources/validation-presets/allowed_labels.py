"""Reject voxel labels outside a small, project-defined set."""

import nibabel as nib
import numpy as np


ALLOWED_LABELS = {0, 1, 2}


def validate(annotation_path: str, context: dict) -> None | str | tuple[str, int]:
    data = np.asanyarray(nib.load(annotation_path).dataobj)
    invalid = ~np.isin(data, list(ALLOWED_LABELS))
    if not np.any(invalid):
        return None
    slice_number = int(np.argwhere(invalid)[0, 2]) + 1
    values = sorted(np.unique(data[invalid]).tolist())
    return f"Unexpected label values: {values}", slice_number
