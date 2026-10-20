# Third-party data and assets

The MIT license in the repository root applies to MotionBridge source code and
original project material. It does not replace the licenses of the datasets or
Epic Games assets listed below.

## 20tab/bvh-python test fixture

- File: `data/bvh/test_freebvh.bvh`
- Source: <https://github.com/20tab/bvh-python/blob/master/tests/test_freebvh.bvh>
- License: MIT
- Local license copy: `data/bvh/20tab-bvh-python-LICENSE.txt`

The upstream parser is not imported or executed by MotionBridge.

## 100STYLE locomotion excerpts

- Files: `data/bvh/100style/*.bvh`
- Dataset: <https://www.ianxmason.com/100style/>
- License: [Creative Commons Attribution 4.0 International (CC BY 4.0)](https://creativecommons.org/licenses/by/4.0/)
- Attribution: The 100STYLE Dataset — Ian Mason

The files are excerpts of `Neutral_FW.bvh`. MotionBridge preserves the source
skeleton and motion samples and creates derived root-motion/retargeting assets.

## Epic Games Unreal Engine template content

Manny, Quinn, Third Person template content, engine binaries, and engine assets
remain subject to the Unreal Engine End User License Agreement. They are not
committed by this repository; `scripts/setup_unreal_demo.ps1` copies them from
the user's local Unreal Engine installation.
