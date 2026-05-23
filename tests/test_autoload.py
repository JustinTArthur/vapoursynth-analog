"""Regression test for Windows error 126 / plugin autoload.

VapourSynth's plugin autoloader must load vsanalog.dll directly from
site-packages/vapoursynth/plugins/vsanalog/ — resolving its bundled
dependency DLLs via manifest.vs + co-location (Windows) or RPATH (Linux/macOS)
— without any prior ``import vsanalog``. Verified in a fresh subprocess so
the result doesn't depend on other tests' import side-effects.
"""
import subprocess
import sys
import textwrap


def test_core_analog_autoloads_without_wrapper_import():
    script = textwrap.dedent("""
        import sys
        import vapoursynth as vs
        if not hasattr(vs.core, 'analog'):
            print('core.analog did not autoload', file=sys.stderr)
            sys.exit(2)
        names = [f.name for f in vs.core.analog.functions()]
        if 'decode_4fsc_video' not in names:
            print(f'decode_4fsc_video missing from core.analog: {names}',
                  file=sys.stderr)
            sys.exit(3)
    """)
    r = subprocess.run(
        [sys.executable, "-c", script],
        capture_output=True, text=True, timeout=60,
    )
    assert r.returncode == 0, (
        f"autoload failed (rc={r.returncode})\n"
        f"--- stdout ---\n{r.stdout}\n--- stderr ---\n{r.stderr}"
    )