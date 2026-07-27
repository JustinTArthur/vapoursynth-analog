"""SECAM line-sequential chroma handling.

``decode_4fsc_video`` emits SECAM as 4:4:0, each chroma plane holding only
the lines its color difference was really decoded from. These helpers turn
that into a conventional raster, either by resampling the lattice
(:func:`resample_secam`) or by filling it the way a receiver's delay line
does (:func:`fill_secam_by_delay`).
"""

from __future__ import annotations

from typing import Any

import vapoursynth as vs

__all__ = ["fill_secam_by_delay", "resample_secam"]

# --- SECAM line-sequential resampling ------------------------------------------
# core.resize kernels selectable by name, as resize.Bob's `filter` does.
_RESIZE_FILTERS = {
    "point": "Point",
    "bilinear": "Bilinear",
    "bicubic": "Bicubic",
    "spline16": "Spline16",
    "spline36": "Spline36",
    "spline64": "Spline64",
    "lanczos": "Lanczos",
}


def _is_line_sequential(clip: vs.VideoNode) -> bool:
    """True for a clip with half-height chroma and the SECAM ident prop.

    Covers the 4:4:0 decode_4fsc_video emits and the 4:2:0 a horizontal
    subsample of it yields; only the vertical lattice matters here.
    """
    fmt = clip.format
    if fmt is None or fmt.color_family != vs.YUV or fmt.subsampling_h != 1:
        return False
    with clip.get_frame(0) as frame:
        return "AnalogSecamFirstRowComponent" in frame.props


def _first_row_is_cb(props: vs.FrameProps) -> bool:
    """True when frame row 0 carries Db, making Cb the anchor plane.

    The plugin writes the ident as UTF-8 data, which VapourSynth hands back as
    str; a hand-built clip may carry bytes instead, so accept either.
    """
    component = props.get("AnalogSecamFirstRowComponent", "Db")
    if isinstance(component, bytes):
        component = component.decode("utf-8", "replace")
    return component != "Dr"


def _reject_separated_fields(func: str, clip: vs.VideoNode) -> None:
    """Refuse a clip whose fields have already been split apart.

    The correction is expressed in whole-frame terms and leaves the field
    split to resize, so a clip that is already one field would be split a
    second time. Separating fields is safe on the woven output instead.
    """
    with clip.get_frame(0) as frame:
        if "_Field" in frame.props:
            raise ValueError(
                f"{func} needs woven frames, but this clip carries _Field: its "
                "fields are already separated, and the correction splits them "
                "itself. Resample or fill the chroma first, then separate "
                "fields or deinterlace."
            )


def _resize_kernel(filter_name: str) -> Any:
    kernel = _RESIZE_FILTERS.get(
        filter_name.lower() if isinstance(filter_name, str) else ""
    )
    if kernel is None:
        raise ValueError(
            f"filter must be one of {sorted(_RESIZE_FILTERS)}, got {filter_name!r}"
        )
    return getattr(vs.core.resize, kernel)


def _secam_destination(
    func: str, clip: vs.VideoNode, resize_kwargs: dict[str, Any]
) -> tuple[vs.VideoFormat, bool]:
    """Validate a SECAM resample request; return (format, is subsampled)."""
    # Rows are shuffled a field at a time, so the chroma plane's own height
    # has to be even as well.
    if clip.height % 4:
        raise ValueError(
            f"{func} needs a frame height divisible by 4, got {clip.height}"
        )
    for key in ("chromaloc_in", "chromaloc_in_s"):
        if key in resize_kwargs:
            raise ValueError(
                f"{key} is fixed by the SECAM line lattice and cannot be set"
            )

    arg = resize_kwargs.get("format")
    if arg is None:
        dst_format = clip.format
    elif isinstance(arg, vs.VideoFormat):
        dst_format = arg
    else:
        dst_format = vs.core.get_video_format(arg)
    if dst_format.color_family != vs.YUV:
        raise ValueError(
            f"{func} produces YUV; convert to RGB in a following resize call "
            "so the chroma is realigned before it is matrixed"
        )
    return dst_format, bool(dst_format.subsampling_w or dst_format.subsampling_h)


def _rows_by_parity(gray: vs.VideoNode) -> tuple[vs.VideoNode, vs.VideoNode]:
    """Split a GRAY clip into its even-row and odd-row halves."""
    fields = vs.core.std.SeparateFields(gray, tff=True, modify_duration=False)
    return (
        vs.core.std.SelectEvery(fields, 2, 0, modify_duration=False),
        vs.core.std.SelectEvery(fields, 2, 1, modify_duration=False),
    )


def _weave_rows(even: vs.VideoNode, odd: vs.VideoNode) -> vs.VideoNode:
    """Interlace two half-height GRAY clips back into even and odd rows."""
    # DoubleWeave trusts _Field over its tff argument, so drop whatever
    # SeparateFields stamped: the caller decides which half goes on top.
    pairs = vs.core.std.RemoveFrameProps(
        vs.core.std.Interleave([even, odd], modify_duration=False), "_Field"
    )
    return vs.core.std.SelectEvery(
        vs.core.std.DoubleWeave(pairs, tff=True), 2, 0, modify_duration=False
    )


# Lattice correction in source luma rows, keyed by (this is the odd-row
# field, the plane's row offset within that field). Measured identical for
# every destination format and chroma siting, because resize's own parity
# handling absorbs the destination geometry and leaves only the offset.
_LATTICE_SRC_TOP = {
    (False, 0): 0.0,
    (False, 1): -2.0,
    (True, 0): 1.0,
    (True, 1): -1.0,
}


def resample_secam(
    clip: vs.VideoNode,
    *,
    filter: str = "bicubic",  # noqa: A002 - mirrors resize.Bob's parameter name
    **resize_kwargs: Any,
) -> vs.VideoNode:
    """Resample a SECAM 4:4:0 clip, realigning its line-sequential chroma.

    Behaves like ``core.resize.<Filter>``: ``filter`` picks the kernel by name
    (as ``resize.Bob`` does) and every other keyword is forwarded to it, so
    ``format``, ``matrix``, ``range``, ``dither_type`` and friends work as
    usual. Clips that aren't SECAM 4:4:0 are handed straight to that resize.

    SECAM carries one color-difference component per line, so
    ``decode_4fsc_video`` emits 4:4:0 with each plane holding only the lines
    it was really decoded from. The two fields of a 625-line frame sit an odd
    line count apart, giving components that pair up in frame-row order
    (``Db, Dr, Dr, Db, Db, ...``): each plane alternates between the first and
    the second luma row of its pair, so neither is a fixed-step lattice and no
    single ``_ChromaLocation`` describes them. Split by row parity, though,
    one plane is top-sited within a field and the other one line lower, the
    stagger swapping planes between the two fields.

    The chroma planes are woven by row parity the way the luma plane is, so
    a plane row's parity picks the same field on all three. Internally, the
    clip is marked TFF and resampled whole, leaving resize to split into
    fields, resample each, and weave the result back. A resize carries one
    ``src_top`` for both fields while the stagger flips between them, so each
    pass comes out right in one field and wrong in the other; four passes cover
    the two planes' two offsets and are merged by row parity. Which plane is
    which is read per frame from ``AnalogSecamFirstRowComponent``, since that
    flips frame to frame (sometimes referred to as the BR.469 4-field cycle).

    Fields are separated only to shuffle rows, never to resample: the
    row-parity merge is split, select, re-weave, which is a plain copy, and
    the destination keeps resize's own interlaced chroma siting. The lattice
    offsets are whole lines, so unless a fractional ``src_top`` is passed, an
    interpolating kernel carries surviving samples through bit-for-bit except
    when specific ``filter_param_a`` ``filter_param_b`` settings soften even a
    pure realignment.

    Because the correction is keyed to row parity rather than to temporal
    field order, ``_FieldBased`` is ignored **for chroma**: either field order
    works, and a clip re-tagged progressive resamples its chroma identically,
    since the lattice lives in the rows and not in the tag. Only the chroma
    actually moving would change that, which is why this has to run before any
    deinterlacing — see :func:`fill_secam_by_delay` for the ordering rule.

    Luma does honor the tag, so a clip marked progressive is scaled frame-wise
    rather than per field. That only shows up when a vertical scale is asked
    for; without one, luma comes through untouched either way.

    ``chromaloc_in``/``chromaloc_in_s`` are fixed by the lattice and are
    rejected. The target format must be YUV — matrix a following resize call
    to reach RGB, so the chroma is realigned before it is mixed in.
    """
    resize = _resize_kernel(filter)
    if not _is_line_sequential(clip):
        return resize(clip, **resize_kwargs)
    _reject_separated_fields("resample_secam", clip)
    dst_format, subsampled = _secam_destination("resample_secam", clip, resize_kwargs)

    kwargs: dict[str, Any] = dict(resize_kwargs)
    kwargs["chromaloc_in_s"] = "top_left"
    if subsampled and not {"chromaloc", "chromaloc_s"} & kwargs.keys():
        kwargs["chromaloc_s"] = "left"
    src_top = float(kwargs.pop("src_top", 0.0))

    with clip.get_frame(0) as frame:
        source_field_based = int(frame.props.get("_FieldBased", 0))

    # Force TFF so resize's internal field split follows row parity. The
    # chroma planes are already woven that way, so the split reaches each
    # plane's own lines on both of them.
    marked = vs.core.std.SetFieldBased(clip, 2)

    def pass_for(bottom: bool, offset: int) -> vs.VideoNode:
        return resize(
            marked, src_top=src_top + _LATTICE_SRC_TOP[(bottom, offset)], **kwargs
        )

    # The anchor plane (the one matching frame row 0) is top-sited in the
    # even-row field and one line low in the odd-row one; the other plane is
    # the reverse.
    anchor = {False: pass_for(False, 0), True: pass_for(True, 1)}
    other = {False: pass_for(False, 1), True: pass_for(True, 0)}

    def merge_fields(passes: dict[bool, vs.VideoNode], plane: int) -> vs.VideoNode:
        """Even output rows from the top-field pass, odd rows from the other.

        Each pass already carries both fields, woven by resize; this keeps the
        half each got right. Splitting and re-weaving is a plain row copy, so
        the surviving rows come through bit for bit.
        """
        top, _ = _rows_by_parity(
            vs.core.std.ShufflePlanes(passes[False], plane, vs.GRAY)
        )
        _, bottom = _rows_by_parity(
            vs.core.std.ShufflePlanes(passes[True], plane, vs.GRAY)
        )
        return _weave_rows(top, bottom)

    # Luma is only right in the unshifted pass; every other one displaces it.
    # Only chroma is bound to the row-parity lattice, so luma follows the
    # clip's own field marking instead of the forced TFF: on a clip tagged
    # progressive a vertical scale is then a frame-wise one, as a caller would
    # expect. Costs a pass only when the two markings disagree, and nothing at
    # all without a vertical scale, where luma comes through untouched either
    # way. The chroma of this pass is off the lattice and goes unread.
    luma = (
        anchor[False] if source_field_based == 2
        else resize(clip, src_top=src_top, **kwargs)
    )

    def assemble(anchor_is_cb: bool) -> vs.VideoNode:
        anchor_plane = 1 if anchor_is_cb else 2
        other_plane = 2 if anchor_is_cb else 1
        planes = [luma, None, None]
        planes[anchor_plane] = merge_fields(anchor, anchor_plane)
        planes[other_plane] = merge_fields(other, other_plane)
        return vs.core.std.ShufflePlanes(planes, [0, 0, 0], vs.YUV)

    candidates = {True: assemble(True), False: assemble(False)}

    def select(n: int, f: vs.VideoFrame) -> vs.VideoNode:
        return candidates[_first_row_is_cb(f.props)]

    # resize already wove the fields back together, so there is nothing to
    # undo here beyond restoring the clip's own field flag.
    out = vs.core.std.FrameEval(candidates[True], select, prop_src=[clip])
    out = vs.core.std.SetFieldBased(out, source_field_based)
    out = vs.core.std.RemoveFrameProps(out, "AnalogSecamFirstRowComponent")
    if not subsampled:
        out = vs.core.std.RemoveFrameProps(out, "_ChromaLocation")
    return out


def _delay_line(gray: vs.VideoNode) -> vs.VideoNode:
    """Every row replaced by the one above it; the first row repeats itself."""
    return vs.core.std.StackVertical(
        [
            vs.core.std.Crop(gray, bottom=gray.height - 1),
            vs.core.std.Crop(gray, bottom=1),
        ]
    )


def fill_secam_by_delay(clip: vs.VideoNode) -> vs.VideoNode:
    """Fill line-sequential chroma the way a SECAM receiver's delay line does.

    Takes the 4:4:0 ``decode_4fsc_video`` emits (or the 4:2:0 a horizontal
    subsample of it yields) and returns 4:4:4 (or 4:2:2): every row keeps the
    color difference its own line carried and borrows the other from the
    line before it, which is what the 64 µs delay line in a receiver supplies.
    Nothing is interpolated — each output sample is a decoded one, copied.

    That is the canonical picture, vertical chroma error included: the
    borrowed component is a line stale, so chroma resolves at half the line
    rate and a color edge lands one line late. :func:`resample_secam`
    resamples the lattice instead, which is truer to the samples but not to
    what a set would have shown.

    "The line before" means the previous line of the same field, since that
    is the one the delay line held. The first line of each field has no
    predecessor for one of its components and repeats the next one instead.
    ``AnalogSecamFirstRowComponent`` is dropped from the result, which is no
    longer line sequential.

    Run this before deinterlacing, never after. ``_FieldBased`` is ignored —
    the lattice is read from row parity, so a clip re-tagged progressive fills
    identically — but a clip whose fields have actually been separated is
    rejected, since the fill works from woven frames.
    """
    if not _is_line_sequential(clip):
        raise ValueError(
            "fill_secam_by_delay expects a line-sequential SECAM clip: 4:4:0 "
            "or 4:2:0 carrying AnalogSecamFirstRowComponent"
        )
    _reject_separated_fields("fill_secam_by_delay", clip)
    # The chroma plane is split by row parity, so it needs an even height too.
    if clip.height % 4:
        raise ValueError(
            "fill_secam_by_delay needs a frame height divisible by 4, got "
            f"{clip.height}"
        )

    # Force TFF so the row-parity splits below mean the same thing whatever
    # the clip's own field order says.
    marked = vs.core.std.SetFieldBased(clip, 2)

    def fill(anchor_is_cb: bool) -> vs.VideoNode:
        anchor = vs.core.std.ShufflePlanes(marked, 1 if anchor_is_cb else 2, vs.GRAY)
        other = vs.core.std.ShufflePlanes(marked, 2 if anchor_is_cb else 1, vs.GRAY)

        # A plane's row parity matches the parity of the frame row it holds,
        # so splitting by row parity separates the two fields on both planes.
        # The anchor plane (the one matching frame row 0) sits on the even-row
        # field's even lines and the odd-row field's odd lines; the other plane
        # is the reverse.
        anchor_even_field, anchor_odd_field = _rows_by_parity(anchor)
        other_even_field, other_odd_field = _rows_by_parity(other)

        def held(samples: vs.VideoNode, on_odd_lines: bool) -> vs.VideoNode:
            """One field's samples stretched over its lines, each held for two.

            A sample lights its own line and the next one, so a plane sitting
            on odd lines has to reach back a sample for the even ones.
            """
            if on_odd_lines:
                return _weave_rows(_delay_line(samples), samples)
            return _weave_rows(samples, samples)

        anchor_full = _weave_rows(
            held(anchor_even_field, False), held(anchor_odd_field, True)
        )
        other_full = _weave_rows(
            held(other_even_field, True), held(other_odd_field, False)
        )
        planes = (
            [marked, anchor_full, other_full] if anchor_is_cb
            else [marked, other_full, anchor_full]
        )
        return vs.core.std.ShufflePlanes(planes, [0, 0, 0], vs.YUV)

    candidates = {True: fill(True), False: fill(False)}

    def select(n: int, f: vs.VideoFrame) -> vs.VideoNode:
        return candidates[_first_row_is_cb(f.props)]

    out = vs.core.std.FrameEval(candidates[True], select, prop_src=[clip])

    with clip.get_frame(0) as frame:
        source_field_based = int(frame.props.get("_FieldBased", 0))
    out = vs.core.std.SetFieldBased(out, source_field_based)
    out = vs.core.std.RemoveFrameProps(out, "AnalogSecamFirstRowComponent")
    if out.format.subsampling_w:
        # Chroma is now full height; only the horizontal siting still means
        # anything, and the lattice was left-sited.
        out = vs.core.std.SetFrameProps(out, _ChromaLocation=0)
    else:
        out = vs.core.std.RemoveFrameProps(out, "_ChromaLocation")
    return out
