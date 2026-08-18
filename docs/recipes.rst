Recipes
=======
Patterns for common jobs are expressed here as complete scripts. Installation
hints are included for recipes that require more than VapourSynth and vsanalog.

.. _recipe-captions:

Cropping Out Captions and Half-Lines
------------------------------------
525-line content often carries closed caption data on lines 21 and 284. Both
of those lines are in the 486i default ST 170 standard active window used by
:py:func:`vsanalog.decode_4fsc_video` and are likely unwanted in the
final video.

An active window size can be selected during the decode (see
:ref:`active-window`) to remove those caption data lines and the analog half
lines at the edge of the active window, giving an even-lined 482i crop for the
interlaced weave:

.. code-block:: python

    import vsanalog

    clip = vsanalog.decode_4fsc_video(
        "capture.tbc", first_active_line=22, last_active_line=525)

The top row is now a field 1 line, so output switches from bottom-field-first
to top-field-first, reflected automatically in the ``_FieldBased`` frame
property for downstream filters to use.

.. _recipe-ivtc:

Inverse Telecine with VIVTC
---------------------------
Film transferred to 525-line video at 2:3 pulldown decodes as 29.97 fps
interlaced frames, two in every five of which are spurious combinations of two
film frames. `VIVTC <https://github.com/vapoursynth/vivtc>`_ undoes that:
``VFM`` re-pairs fields into the original film frames and ``VDecimate`` drops
the one duplicate in five that matching leaves behind, returning 23.976 fps
progressive.

.. tab-set::
    :sync-group: installer

    .. tab-item:: pip
        :sync: pip

        .. code-block:: bash

            pip install vapoursynth vsanalog vapoursynth-vivtc

    .. tab-item:: uv
        :sync: uv

        .. code-block:: bash

            uv add vapoursynth vsanalog vapoursynth-vivtc

Both filters make their decisions on integer frames — ``VFM`` on 8-bit ones,
``VDecimate`` on 8- to 16-bit — but each takes a ``clip2`` of the same length
to emit output frames from. Analyse a converted copy and hand the decoder's
own 32-bit float clip through ``clip2``, and the frames that reach the rest of
your script are matched but contain original lines from decoder output.

.. code-block:: python

    import vapoursynth as vs
    import vsanalog

    src = vsanalog.decode_4fsc_video("film_transfer.tbc")

    # Field matching: decisions from an 8-bit copy, frames from original.
    # order=0 (bottom-field-first) for decode_4fsc_video's default 486i
    # window, but _FieldBased frame property will be used first anyway.
    # Optionally, y0 and y1 exclude lines with captions data from calculations. 
    match_reference = src.resize.Point(format=vs.YUV444P8)
    matched = match_reference.vivtc.VFM(order=0, y0=0, y1=2, clip2=src)

    # Decimation: VDecimate wants integer metrics too, so measure a 16-bit
    # view of the matched clip and again emit float frames.
    dedupe_reference = matched.resize.Point(format=vs.YUV444P16)
    film = dedupe_reference.vivtc.VDecimate(clip2=matched)

    film.set_output()

``film`` is ``YUV444PS`` at 24000/1001 fps with ``_FieldBased`` cleared, and
each frame carries ``VFMMatch`` and ``_Combed`` from the matcher. Frames the
matcher could not pair cleanly (a bad edit, a broken cadence) come out combed;
inspect ``_Combed`` and post-process those with a deinterlacer if there are
enough to matter.

The 8-bit analysis copy is deliberate: ``VFM``'s combing threshold
``cthresh`` and the field-difference metrics are calibrated in 8-bit terms, and
the PyPI release's ``VFM`` accepts nothing deeper in any case. It only affects
which fields get paired, never the pixels you keep.

.. _recipe-scan-interlaced:

CRT-Style Interlaced Display
----------------------------
Interlaced video was never meant to be seen a frame at a time: a CRT painted
one field while lines from the other faded, and the eye and brain did the rest.
`vsfieldkit <https://github.com/JustinTArthur/vsfieldkit>`_'s
``scan_interlaced`` reproduces that display and perception, one output frame
per field with the freshly painted lines over the retained ones, a "bob"
without interpolation, showing exactly the lines the tape or disc carried.

.. tab-set::
    :sync-group: installer

    .. tab-item:: pip
        :sync: pip

        .. code-block:: bash

            pip install vapoursynth vsanalog vsfieldkit

    .. tab-item:: uv
        :sync: uv

        .. code-block:: bash

            uv add vapoursynth vsanalog vsfieldkit

.. code-block:: python

    import vsanalog
    import vsfieldkit

    src = vsanalog.decode_4fsc_video("capture.tbc")

    # Each output frame paints one field's lines fresh at 120% brightness over
    # the prior field's, as a phosphor's newer scan outshines its older one.
    scanned = vsfieldkit.scan_interlaced(src, attack_factor=1.2, decay_factor=0.2)

    scanned.set_output()

The result has twice the frames at twice the frame rate (60000/1001 for
525-line sources), tagged progressive. ``scan_interlaced`` reads the decoded
clip's ``_FieldBased`` for the field order and takes the 32-bit float
``YUV444PS`` directly. ``attack_factor`` multiplies the fresh field's luma
(clamped at the format's white), ``decay_factor`` dims the retained field
toward black.


.. _recipe-qtgmc-denoise:

Deinterlace and Denoise with vs-jetpack
---------------------------------------
Tape noise is best attacked once the fields are woven into full frames, and
`vs-jetpack <https://github.com/Jaded-Encoding-Thaumaturgy/vs-jetpack>`_
covers both halves: QTGMC in ``vsdeinterlace``, and in ``vsdenoise`` a
motion-compensated degrain to steady a reference clip, BM3D for the luma and
NLMeans for the chroma. Every stage here takes the decoder's ``YUV444PS``
as-is. Colorimetry work and integer conversion can happen after.

The extras bring in the native plugins these functions call, a few of whose
wheels live on JET's own index rather than PyPI — hence the second index in
the install line. Add ``nvidia`` (or ``amd``, ``cl``) to the extras for the
GPU BM3D and NLMeans backends, which the functions pick up automatically.

.. tab-set::
    :sync-group: installer

    .. tab-item:: pip
        :sync: pip

        .. code-block:: bash

            pip install vapoursynth vsanalog "vsjetpack[deinterlace,denoise]" \
                --extra-index-url https://jaded-encoding-thaumaturgy.github.io/vs-wheels/simple

    .. tab-item:: uv
        :sync: uv

        .. code-block:: bash

            uv add vapoursynth vsanalog "vsjetpack[deinterlace,denoise]" \
                --index https://jaded-encoding-thaumaturgy.github.io/vs-wheels/simple

.. code-block:: python

    import vsanalog
    from vsdeinterlace import QTempGaussMC
    from vsdenoise import MVToolsPreset, bm3d, mc_degrain, nl_means

    src = vsanalog.decode_4fsc_video("tape.tbc", "tape_chroma.tbc")

    # Field-order and format come from the decoded clip's frame properties.
    progressive = QTempGaussMC().deinterlace(src)

    # A lightly degrained, motion-compensated reference: the block matcher and
    # NLMeans both search it instead of the noisy picture, so they find real
    # detail rather than noise that happens to look alike.
    ref = mc_degrain(progressive, preset=MVToolsPreset.HQ_SAD, thsad=100)

    # BM3D on the luma plane, using the reference as its basic estimate.
    denoised = bm3d(progressive, sigma=0.8, tr=2,
                    profile=bm3d.Profile.NORMAL, ref=ref, planes=0)

    # NLMeans on the two chroma planes, searching the same reference.
    denoised = nl_means(denoised, 0.2, tr=2, ref=ref, planes=[1, 2])

    denoised.set_output()

``sigma``, ``h`` (NLMeans' second positional argument) and ``thsad`` are the
strengths to tune to the tape; ``tr`` is the temporal radius in frames each
side. BM3D's non-legacy backends only take 32-bit float, so the decoder's
output is exactly what they want; the chroma planes pass through the luma-only
BM3D call untouched and are then denoised in place by NLMeans, whose
``planes=[1, 2]`` merges its result over the luma BM3D produced.
