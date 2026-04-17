Changelog
=========

0.2.2
-----
- Y/C-separated signals correctly force ``mono`` decoder for luma processing.
- Y/C-separated decodes scale chroma planes against the chroma 4fsc's own
  IRE excursion instead of luma signal’s, fixing potential mismatch.

0.2.1
-----
- Windows wheel now contains full dependency set.

0.2.0
------------------
- Added ``vsanalog`` Python wrapper package with type-hinted signatures and
  auto-loading fallback for older VapourSynth versions that predate
  pip-installable plugins.
- Retagged wheels as independent from the CPython ABI so a single wheel can
  serve any compatible Python interpreter on a given platform.
- More comprehensive documentation.

0.1.1
-----
New build pipeline with reasonably-packaged plugin libraries plus Python
sdists and wheels that can support VapourSynth's upcoming pip-installable
plugin flow. The old drop-in plugin libraries continue to work.

No fixes or feature changes to the plugin itself.

0.1.0
-----
First release of the plugin. The sole ``decode_4fsc_video`` VapourSynth
function is mostly derived from ``ld-decode-tools``, incorporating its
composite video separation/transformation decode processes along with its
dropout correction methods.