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
2. On the reference chart frame, select the correct **Chart model** in **Color chart**. **Color Checker Passport Video** is the default and refers to its 24-patch video color target in four rows of six, not its Classic, grayscale, or white-balance panel.
3. In Resolve's Color page, select the Camera Match node and activate the viewer's **Open FX Overlay** tool. Keep **Show color chart overlay** enabled in **Color chart**. For Passport Video, place TL, TR, BR, and BL around the lower 24-patch color panel only; leave the upper three-bar grayscale panel outside the corners. The labels identify patch orientation. Set **Rotate chart** / **Mirror patch identity** as needed. The corner coordinate fields are hidden from the inspector. After reopening Resolve, select Open FX Overlay again if the guides are hidden.
4. In Select patches mode, click a patch to include or exclude it. In Adjust samples mode, drag a sample center or one of its corners. Open **Advanced** for selected-patch controls, sample offsets and dimensions, global sample size, reset, and refit. The default sample size is the center 50% of each patch.
5. In **Capture**, **Save reference as** is prefilled with the next available name (`ref1`, `ref2`, and so on). Keep it or edit it (for example, `Scene 1`), then press **Capture reference**. Capture each scene's reference under a different name. The reference node stays a pass-through carrier and registers that named reference for the current Resolve session.
6. On a comparison clip, add a fresh Camera Match node. In **Apply reference adjustments**, enter the corresponding name in **Apply reference named**, choose **Existing match** or **RBF match** under **Match method**, select the same chart model, align the chart, and press **Apply reference to this clip**. It copies that named reference, measures the current Source clip, fits the selected method, and saves the reference and correction in this node. **List captured references** displays the names currently registered. You can also copy a reference node to a comparison clip and press Apply reference with **Apply reference named** empty; the copied node uses its embedded reference.
7. Repeat with another name for Scene 2. In **Reference adjustments**, Existing match exposes **Hue match**, **Saturation match**, **Exposure match**, and **Neutral balance match** (0–100%). RBF match uses all selected chart patches, including neutrals, and exposes only **RBF weight %** (0–200%, default 100%). A weight of 0 bypasses the RBF correction; 100% applies the fitted result. Changing Match method on a solved node immediately refits its saved captures; no recapture is needed. Copy each solved node to other shots from that camera and lighting setup.

After reopening Resolve, press **Use this reference for other clips** under **Advanced** on each saved reference node that you want to use with fresh nodes. Existing solved nodes keep their own saved reference and correction. Older reference nodes all named `Hero camera` can be assigned distinct names by editing **Save reference as** and pressing **Use this reference for other clips**; no new chart capture is needed. If Apply reference cannot find a name, register its saved reference or capture it again.

Changing chart geometry after capture leaves the last correction active. Recapture to use new sample areas. Changing exclusions can be applied with **Refit captured samples** under **Advanced**. Changing Match method refits the two saved captures; playback and rendering never refit. `Bypass` is exact pass-through. Existing nodes saved with the earlier 2D Radial bias method retain their result until they are reapplied or switched between methods.

After updating from version 0.12 to 0.13, **recapture both the reference and target on Passport Video nodes**: the earlier Passport grid measured the wrong physical positions. Saved corrections still render until replaced. For full-size Color Checker Video RBF nodes, press **Refit captured samples** to use the smoother fit and finer lookup grid.

See [the algorithm notes](docs/algorithm.md) for the fitted model and limits.

## Current verification limits

The code builds and passes the automated checks, including a Metal comparison against the CPU transform. The system-installed plugin has appeared in Resolve's LoFi FX group. The revised inspector groups and labels need direct host verification; capture, node copying, undo, save/reopen, and export need the same. Follow [the host test checklist](docs/host-test-checklist.md) before using the effect in a production project.

Chart patch rectangles were traced from manufacturer imagery and still need alignment validation against physical targets. The manual input-space contract cannot detect an incorrectly normalized upstream clip. The matcher fits a global exposure gain, relative neutral balance, and smooth hue/saturation fields; it does not fit tone curves, gamut compression, or a 3D LUT.
