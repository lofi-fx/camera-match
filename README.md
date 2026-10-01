# LoFi FX Camera Match

A native OpenFX filter for the DaVinci Resolve Color page. It measures a reference camera and another camera from a ColorChecker Video or ColorChecker Passport Video **color target**, saves the samples and correction inside the node, and applies a frozen relative match to later shots. Rendering uses Metal when Resolve supplies GPU buffers, with a CPU fallback.

## One-command build and launch

Save your Resolve project, then run:

```sh
./build-and-launch.sh
```

The script builds, runs the automated tests, installs the OFX bundle in `/Library/OFX/Plugins`, clears Resolve’s stale OFX cache, and opens Resolve. A first installation may require an administrator password; updates to a writable existing bundle do not. If Resolve is still running, the script waits for you to quit it before replacing the bundle. To check the build without installing or launching, run `./build-and-launch.sh --build-only`.

## Build

Requires CMake 3.20+, a C++17 compiler, and the pinned OpenFX SDK commit `ab779510b2655b4d11a7e01e5c521f9aa8c88976` (OpenFX 1.5.1). CMake fetches the SDK headers on the first build.

```sh
cmake -S . -B build
cmake --build build -j
ctest --test-dir build --output-on-failure
```

The bundle is `build/LoFiFxCameraMatch.ofx.bundle`. This build targets macOS arm64. Resolve scans `/Library/OFX/Plugins` on this machine. The launch script installs there; the earlier user-level copy under `~/Library/OFX/Plugins` is not discovered. To uninstall, remove `/Library/OFX/Plugins/LoFiFxCameraMatch.ofx.bundle` with administrator permission and relaunch.

## Workflow

1. Normalize reference and comparison clips to **DaVinci Wide Gamut / DaVinci Intermediate** upstream of Camera Match. Place Camera Match before creative grading, tone mapping, or display conversion. Do not add a second transform if Resolve color management already provides that space.
2. On the reference chart frame, select the correct **Chart model** in **Color chart**. **Color Checker Passport Video** is the default and refers to its 24-patch video color target in the original main-branch layout of six rows and four columns, not its Classic, grayscale, or white-balance panel.
3. In Resolve's Color page, select the Camera Match node and activate the viewer's **Open FX Overlay** tool. Keep **Show color chart overlay** enabled in **Color chart**. For Passport Video, place TL, TR, BR, and BL around the lower 24-patch color panel only; leave the upper three-bar grayscale panel outside the corners. The labels identify patch orientation. Set **Rotate chart** / **Mirror patch identity** as needed. The corner coordinate fields are hidden from the inspector. After reopening Resolve, select Open FX Overlay again if the guides are hidden.
4. In Select patches mode, click a patch to include or exclude it. In Adjust samples mode, drag a sample center or one of its corners. Open **Advanced** for selected-patch controls, sample offsets and dimensions, global sample size, reset, and refit. The default sample size is the center 50% of each patch.
5. In **Capture**, **Save reference as** is prefilled with the next available name (`ref1`, `ref2`, and so on). Keep it or edit it (for example, `Scene 1`), then press **Capture reference**. Capture each scene's reference under a different name. The reference node stays a pass-through carrier and registers that named reference for the current Resolve session.
6. On a comparison clip, add a fresh Camera Match node. In **Apply reference adjustments**, enter the corresponding name in **Apply reference named**, select the same chart model, align the chart, and press **Apply reference to this clip**. It copies that named reference, measures the current Source clip, fits RBF, and saves the reference and correction in this node. **List captured references** displays the names currently registered. You can also copy a reference node to a comparison clip and press Apply reference with **Apply reference named** empty; the copied node uses its embedded reference.
7. Repeat with another name for Scene 2. **Reference adjustments** exposes **Reference match %** (0–200%, default 100%), **Saturation match**, and **Exposure match** (both 0–100%, default 100%). Reference match 0 bypasses the correction; 100% applies the fitted result with the two component controls at 100%. RBF uses selected chart patches, including neutrals. Copy each solved node to other shots from that camera and lighting setup.

After reopening Resolve, press **Use this reference for other clips** under **Advanced** on each saved reference node that you want to use with fresh nodes. Existing solved nodes keep their own saved reference and correction. Older reference nodes all named `Hero camera` can be assigned distinct names by editing **Save reference as** and pressing **Use this reference for other clips**; no new chart capture is needed. If Apply reference cannot find a name, register its saved reference or capture it again.

Changing chart geometry after capture leaves the last correction active. Recapture to use new sample areas. Changing exclusions can be applied with **Refit captured samples** under **Advanced**. Refit uses the two saved captures; playback and rendering never remeasure or refit. Upstream exposure changes affect the incoming pixels but leave the saved fit unchanged. Recapture the affected reference and/or apply the reference again on the target chart frame to incorporate those changes. `Bypass` is exact pass-through. Older nodes saved with a removed matcher reuse their saved captures to obtain an RBF fit on load. A failed migration leaves the correction invalid; realign or recapture as needed.

Version 0.17 restores the original Passport dimensions, patch identities, default selection, and orientation from main. **Recapture both reference and target for Passport nodes captured in 0.13–0.16** before applying a new match. Existing saved corrections continue rendering. Original main-branch captures remain compatible. For RBF nodes saved in 0.14, install 0.15, restart Resolve, and press **Advanced → Refit captured samples** to use the new native DI fit. Saved captures can be reused. Until refitted, 0.14 nodes retain their linear fit.

See [the algorithm notes](docs/algorithm.md) for the fitted model and limits.

## Current verification limits

The code builds and passes the automated checks, including a Metal comparison against the CPU transform. The system-installed plugin has appeared in Resolve's LoFi FX group. The revised inspector groups and labels need direct host verification; capture, node copying, undo, save/reopen, and export need the same. Follow [the host test checklist](docs/host-test-checklist.md) before using the effect in a production project.

Chart patch rectangles were traced from manufacturer imagery and still need alignment validation against physical targets. The manual input-space contract cannot detect an incorrectly normalized upstream clip. RBF evaluates its complete Gaussian/affine mapping directly on Metal. Fit-time neutral brightness checks reject demonstrated tonal folds, but do not guarantee smoothness on every chromatic trajectory or at extreme strengths. The reported highlight artifact remains unconfirmed by numerical regression checks.

Both component controls at 100% preserve the exact weighted RBF output. At 0%, saturation retains input relative saturation while keeping matched hue for chromatic colors; exposure retains input scene-linear luminance. Intermediate exposure amounts blend a per-pixel luminance ratio in stops, rather than applying one global exposure gain. Signed or very low luminance retains the RBF result. See [the algorithm notes](docs/algorithm.md) for formulas, migration rules, and test coverage.
