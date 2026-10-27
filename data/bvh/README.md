# BVH test fixture

`test_freebvh.bvh` is copied from the `20tab/bvh-python` repository and is
used only as an external test fixture for MotionBridge's independently written
BVH parser.

- Source: <https://github.com/20tab/bvh-python/blob/master/tests/test_freebvh.bvh>
- Upstream repository: <https://github.com/20tab/bvh-python>
- License: MIT; see `20tab-bvh-python-LICENSE.txt` in this directory.

MotionBridge does not import or execute the upstream parser implementation.

## 100STYLE neutral forward walk

The files under `100style/` are excerpts from `Neutral_FW.bvh` in the 100STYLE
locomotion dataset. They are used as the clean forward-walking source motions
for the public MotionBridge demo.

- Dataset: <https://www.ianxmason.com/100style/>
- Original file: `Neutral_FW.bvh`
- Included frame ranges: 470–694 (default demo), 507–687, 530–694
  (calibration/acceptance), and 2093–2176, inclusive
- Capture rate: 60 FPS
- License: [Creative Commons Attribution 4.0 International (CC BY 4.0)](https://creativecommons.org/licenses/by/4.0/)
- Attribution: The 100STYLE Dataset - Ian Mason

The excerpt keeps the source skeleton, channel order, and motion samples
unchanged. MotionBridge adds its own virtual root only in generated build
artifacts.

The research-only SFU motion previously used during local iteration is not
distributed. See `THIRD_PARTY_NOTICES.md` for the archive policy and source
terms.
