"""Dropout reporting and masking.

``decode_4fsc_video(annotate_dropouts=True)`` records where each frame's
dropouts are instead of concealing them, in the ``AnalogDropoutSpans`` frame
property. These helpers read that back as spans (:func:`dropout_spans`) or
rasterise it into a mask clip (:func:`create_dropouts_mask`) for
``core.std.MaskedMerge`` and the other ``std`` mask functions.
"""

from __future__ import annotations

import enum
from collections.abc import Iterable
from typing import Any, NamedTuple

import vapoursynth as vs

from .plugin import requires_plugin

__all__ = [
    "DropoutOrigin", "DropoutSpan", "create_dropouts_mask", "dropout_spans",
]


class DropoutOrigin(enum.IntEnum):
    """Where a reported dropout region came from."""

    #: Flagged upstream by ld-decode / vhs-decode and stored in the sidecar.
    SOURCE_METADATA = 0
    #: Detected and concealed by the decoder itself (SECAM FM click concealment).
    DECODER_CONCEALMENT = 8


class DropoutSpan(NamedTuple):
    """One run of dropped samples on a single output row.

    ``x`` is half-open — ``[x_start, x_end)`` — and both axes are in the decoded
    clip's own pixel coordinates.
    """

    y: int
    x_start: int
    x_end: int
    origin: DropoutOrigin


# Ints per span in the AnalogDropoutSpans property.
_SPAN_STRIDE = 4


def dropout_spans(frame: vs.VideoFrame) -> list[DropoutSpan]:
    """Read a frame's dropout regions, as recorded by ``annotate_dropouts=True``.

    Returns an empty list for a frame with no dropouts, and raises
    :class:`ValueError` if the clip was not decoded with ``annotate_dropouts``.
    """
    try:
        flat = frame.props["AnalogDropoutSpans"]
    except KeyError:
        raise ValueError(
            "frame has no AnalogDropoutSpans property; decode the clip with "
            "annotate_dropouts=True"
        ) from None
    # Four ints per span, so the array length is never 1 and VapourSynth never
    # collapses it to a bare int.
    return [
        DropoutSpan(flat[i], flat[i + 1], flat[i + 2], DropoutOrigin(flat[i + 3]))
        for i in range(0, len(flat), _SPAN_STRIDE)
    ]


@requires_plugin
def create_dropouts_mask(
    clip: vs.VideoNode,
    origins: Iterable[DropoutOrigin | int] | None = None,
) -> vs.VideoNode:
    """Rasterise a clip's annotated dropout regions into a mask clip.

    Returns a single-plane clip matching ``clip``'s dimensions and precision —
    ``0`` for clean samples, full scale for dropped ones — ready to pass to
    ``core.std.MaskedMerge`` and the other ``std`` mask functions. ``clip`` must
    have been decoded with ``annotate_dropouts=True``.

    Because the mask is full-size, subsampled clips need no special handling:
    ``MaskedMerge`` resamples the mask for the chroma planes itself, honouring
    the clip's ``_ChromaLocation``.

    ``origins`` restricts the mask to particular :class:`DropoutOrigin` values;
    by default every origin is drawn.
    """
    kwargs: dict[str, Any] = {}
    if origins is not None:
        kwargs["origins"] = [int(o) for o in origins]
    return vs.core.analog.create_dropouts_mask(clip, **kwargs)
