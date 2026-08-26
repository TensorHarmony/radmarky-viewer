"""Reject an annotation that contains no labeled voxels."""

import nibabel as nib
import numpy as np


def validate(annotation_path: str, context: dict) -> None | str:
    data = np.asanyarray(nib.load(annotation_path).dataobj)
    return None if np.any(data != 0) else "Annotation is empty."
