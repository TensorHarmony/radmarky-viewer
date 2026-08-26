"""Reject an annotation that disappears and later resumes on axial slices."""

import nibabel as nib
import numpy as np


def validate(annotation_path: str, context: dict) -> None | str | tuple[str, int]:
    data = np.asanyarray(nib.load(annotation_path).dataobj)
    occupied = np.any(data != 0, axis=(0, 1))
    seen_annotation = False
    seen_gap = False
    for index, has_annotation in enumerate(occupied):
        if has_annotation and seen_gap:
            slice_number = index + 1
            return "Annotation resumes after an empty axial slice.", slice_number
        seen_gap = seen_gap or (seen_annotation and not has_annotation)
        seen_annotation = seen_annotation or bool(has_annotation)
    return None
