# Host compatibility evidence

Observed on September 28, 2026:

- macOS 26.6.2, arm64 (`uname -m`, `sw_vers`).
- DaVinci Resolve application installed at `/Applications/DaVinci Resolve/DaVinci Resolve.app`; bundle version 21.1.0.
- Resolve developer files installed under `/Library/Application Support/Blackmagic Design/DaVinci Resolve/Developer/OpenFX/`.
- Developer OpenFX headers at `OpenFX-1.4/include`; its shipped `GainPlugin/GainPlugin.cpp` registers an overlay and draws with `OfxDrawSuiteV1`.
- This project pins the OpenFX 1.5.1 Git revision `ab779510b2655b4d11a7e01e5c521f9aa8c88976` for headers. The local build used an existing checkout of that exact revision as `FETCHCONTENT_SOURCE_DIR_OPENFX`. A normal clean build fetches the pinned revision.
- Compiled bundle: Mach-O arm64, macOS minimum 11.0, SDK 26.5. It links system C++ and System libraries plus the Metal, Foundation, and OpenGL frameworks.

Resolve’s September 28 launch did not list the user-level Camera Match bundle. Its `OFXPluginCacheV2.xml` contains the working plugins under `/Library/OFX/Plugins`, and no entries from `~/Library/OFX/Plugins`. The installer therefore targets `/Library/OFX/Plugins` and requires administrator privileges. A later system installation appeared in Resolve’s LoFi FX group; this observation does not establish verification of every subsequent build.

The plugin registers one overlay entry: OpenGL `OverlayInteractV1` on macOS, with DrawSuite V2 used on hosts where that path is selected. Resolve created overlay interact instances after project reload but did not issue a Draw action when Show overlay was toggled. The plugin now subscribes each interact instance to its parameters and logs `interactRedraw` results. Resolve owns the viewer’s Open FX Overlay mode; the user subsequently confirmed that selecting it restored the guides. Full drawing/drag verification remains on the host checklist. There is no evidence here for Windows, Linux, Intel macOS, or earlier Resolve versions.

The plugin advertises float RGB/RGBA, Metal and CPU render, tiles, and multiple resolutions. It does not advertise CUDA. It assumes progressive imagery. Source coordinates come from canonical OFX geometry, image render scale, pixel aspect ratio, image bounds, and signed row stride.
