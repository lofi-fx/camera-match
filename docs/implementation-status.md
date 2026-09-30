# Implementation status

## Built

- Native OFX filter and arm64 macOS bundle; one Source and one Output.
- OpenGL V1 viewer overlay on macOS with four-corner projective alignment, patch outlines, sample regions, three edit modes, inspector equivalents, orientation/mirror controls, and parameter edit grouping.
- Separate full-size ColorChecker Video and Passport Video color-target layouts. No published patch RGB target values are used.
- Metal float-buffer rendering with a CPU fallback, Source sampling with median luminance outlier rejection, alpha handling, captured snapshots, checksum-validated project payload, reference revision/fingerprint, and frozen match coefficients.
- Independent hue, saturation, exposure, and neutral controls. Chromatic stages preserve scene-linear DWG luminance in the tested domain.
- Selectable existing hue-curve and radial-bias color fits. Radial bias uses a cached 64×64 chroma lookup grid, bilinear GPU sampling, and a 0–200% weight control shown only for that method. CM2 project data still loads as the existing method.
- Explicit Capture reference, Apply reference to this clip, refit, status, selected-patch report, and bypass controls. Capture registers a uniquely named reference for the session; Apply selects by name and copies it into the target node's persistent payload before rendering. Saved references can be registered again after Resolve restarts.
- New nodes default to the Passport Video chart model. Chart corner coordinates are hidden from the inspector; patch and sample controls are grouped in a collapsed Advanced section.

## Automated verification

`cmake --build build -j4` and `ctest --test-dir build --output-on-failure` pass. Tests cover homography round trips and invalid geometry, both patch layouts, signed negative row stride/nonzero image origin, synthetic unique patch colors at half render scale and non-square PAR, Intermediate transfer round trips including negative and highlight values, OKLab round trips, CM2/CM3 snapshot loading, synthetic exposure and radial solving, slider endpoint combinations, exposure-zero luminance, and finite edge behavior. A Metal test executes both match methods on the local GPU and compares output against the CPU transform. A direct dynamic-loader smoke check confirms the bundle exports one OFX plugin.

## Resolve verification: partial

The system installation under `/Library/OFX/Plugins` appeared in Resolve's LoFi FX group. After project reload, Resolve created overlay interact instances but did not send Draw actions while the viewer's Open FX Overlay mode was inactive. The user confirmed that selecting Open FX Overlay restored the guides without removing the effect. The plugin selects OpenGL V1 drawing on macOS and logs redraw request results. The 0.10.0 inspector grouping, prefilled reference names, method switching, and radial controls still need direct host verification. Other **unverified** areas are viewer drawing/drag at multiple zooms and proxies; parameter edit grouping and undo/redo; Source image fetch from `InstanceChanged`; hidden string payload length/copy/reopen; alpha behavior in a Resolve render; color management negotiation; export parity; physical chart patch alignment; real-footage match quality and UHD playback performance. See [host-test-checklist.md](host-test-checklist.md).

## Known limits

- Chart outlines are traced from [Calibrite's full-size Video guide](https://calibrite.com/wp-content/uploads/2023/08/ColorChecker-Video-Guide_EN_0123.pdf) and [Passport Video 2 product imagery](https://calibrite.com/us/product/colorchecker-passport-video-2/?noredirect=en-US); physical-target validation is pending. The Passport model covers the 24-patch video color panel only.
- Manual DWG/Intermediate input contract only. A mismatched upstream color space is not automatically detected.
- The patch-quality flags currently mark too few pixels and high luminance dispersion; gradient and clipping diagnostics need real-footage tuning.
- Uses a single exposure gain, not a tone response curve. The color fit can fall back to neutral/exposure-only when chromatic coverage is insufficient.
- The named-reference registry lasts only for the current Resolve process. Fresh nodes after a restart need **Use this reference for other clips** on each relevant saved reference node. Rendering reads the saved node payload and does not depend on the registry.
- No automatic chart detection, chart tracking, downstream geometric inversion, interlaced-field capture, CUDA rendering, or project-wide persistent reference registry.
