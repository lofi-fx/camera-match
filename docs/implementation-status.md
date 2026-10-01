# Implementation status

## Built

- Native OFX filter and arm64 macOS bundle; one Source and one Output.
- OpenGL V1 viewer overlay on macOS with four-corner projective alignment, patch outlines, sample regions, three edit modes, inspector equivalents, orientation/mirror controls, and parameter edit grouping.
- Separate full-size ColorChecker Video and Passport Video color-target layouts. Passport Video uses the original main-branch four-column, six-row geometry again in 0.17. The intermediate six-column, four-row model remains readable for saved corrections; matching against the restored geometry requires recapture. No published patch RGB target values are used.
- Metal float-buffer rendering with a CPU fallback, Source sampling with median luminance outlier rejection, alpha handling, captured snapshots, checksum-validated project payload, reference revision/fingerprint, and frozen match coefficients.
- RBF is the sole matching model in the core, CPU and Metal renderers, and UI. It includes selected color and neutral patches plus a black anchor. Reference match % (0–200%), Saturation match, and Exposure match (0–100%) adjust the single fitted result. Direct Gaussian/affine GPU evaluation retains full kernel terms and the Python DWG/DI preset defaults.
- CM7 saves RBF coefficients and fitting space only. CM2–CM6 references remain readable; CM5/CM6 RBF corrections remain frozen, CM4 retains refit-on-load migration, and obsolete solved models are refitted from saved captures using RBF.
- Explicit Capture reference, Apply reference to this clip, refit, status, selected-patch report, and bypass controls. Capture registers a uniquely named reference for the session; Apply selects by name and copies it into the target node's persistent payload before rendering. Saved references can be registered again after Resolve restarts.
- New nodes default to the Passport Video chart model. Chart corner coordinates are hidden from the inspector; patch and sample controls are grouped in a collapsed Advanced section.

## Automated verification

`cmake --build build -j4` and `ctest --test-dir build --output-on-failure` pass. Tests cover homography round trips and invalid geometry, both patch layouts, signed negative row stride/nonzero image origin, synthetic unique patch colors at half render scale and non-square PAR, Intermediate transfer round trips including negative and highlight values, OKLab round trips, CM2–CM7 snapshot loading, synthetic exposure and RBF solving, slider endpoint combinations, exposure-zero luminance, and finite edge behavior. A Metal test executes the match kernels on the local GPU and compares output against the CPU transform. An OFX descriptor test loads the built plugin and verifies the three RBF controls, their saved IDs/defaults/ranges, GPU support, retained capture/geometry descriptors, and removal of the method selector. All three suites pass for 0.19.

## Resolve verification: partial

The system installation under `/Library/OFX/Plugins` appeared in Resolve's LoFi FX group. After project reload, Resolve created overlay interact instances but did not send Draw actions while the viewer's Open FX Overlay mode was inactive. The user confirmed that selecting Open FX Overlay restored the guides without removing the effect. The plugin selects OpenGL V1 drawing on macOS and logs redraw request results. The user reports improved posterization after the RBF and chart changes, but reported highlight artifacts remain unresolved. Automated checks do not replace a complete host verification of the current 0.19 build. The optional isolated 4K Metal benchmark excludes Resolve overhead; see algorithm.md for its scope. Other **unverified** areas are viewer drawing/drag at multiple zooms and proxies; parameter edit grouping and undo/redo; Source image fetch from `InstanceChanged`; hidden string payload length/copy/reopen; alpha behavior in a Resolve render; color management negotiation; export parity; exact physical patch boundary alignment; real-footage match quality. See [host-test-checklist.md](host-test-checklist.md).

## Known limits

- Chart outlines are traced from [Calibrite's full-size Video guide](https://calibrite.com/wp-content/uploads/2023/08/ColorChecker-Video-Guide_EN_0123.pdf), [Passport Video 2 product information](https://calibrite.com/us/product/colorchecker-passport-video-2/?noredirect=en-US), and the user-supplied frame; exact physical patch boundary validation is pending. The Passport model covers the 24-patch video color panel only.
- Manual DWG/Intermediate input contract only. A mismatched upstream color space is not automatically detected.
- The patch-quality flags currently mark too few pixels and high luminance dispersion; gradient and clipping diagnostics need real-footage tuning.
- The RBF is unconstrained and can fold with inconsistent captures; real-footage gradient evaluation remains necessary.
- The named-reference registry lasts only for the current Resolve process. Fresh nodes after a restart need **Use this reference for other clips** on each relevant saved reference node. Rendering reads the saved node payload and does not depend on the registry.
- No automatic chart detection, chart tracking, downstream geometric inversion, interlaced-field capture, CUDA rendering, or project-wide persistent reference registry.
