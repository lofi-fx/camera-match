# LoFi FX Camera Match

A native OpenFX filter for the DaVinci Resolve Color page. It measures a hero camera and another camera from a ColorChecker Video or ColorChecker Passport Video **color target**, saves the samples and correction inside the node, and applies a frozen relative match to later shots. Rendering uses Metal when Resolve supplies GPU buffers, with a CPU fallback.

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

1. Normalize hero and comparison clips to **DaVinci Wide Gamut / DaVinci Intermediate** upstream of Camera Match. Place Camera Match before creative grading, tone mapping, or display conversion. Do not add a second transform if Resolve color management already provides that space.
2. On the hero chart frame, select the correct Chart model. **Color Checker Passport Video** is the default and refers to its 24-patch video color target, not its Classic, grayscale, or white-balance panel.
3. In Resolve's Color page, select the Camera Match node and activate the viewer's **Open FX Overlay** tool. Keep **Show overlay** enabled in the inspector. Drag TL, TR, BR, and BL to the printed target boundary. The labels identify patch orientation. Set Rotate chart / Mirror as needed. The corner coordinate fields are hidden from the inspector.
4. In Select patches mode, click a patch to include or exclude it. In Adjust samples mode, drag a sample center or one of its corners. Open **Advanced** for selected-patch controls, sample offsets and dimensions, global sample size, reset, and refit. The default sample size is the center 50% of each patch.
5. Enter a unique name in **Capture hero as** (for example, `Scene 1`) and press **Capture Hero**. Capture each scene's hero under a different name. The hero node stays a pass-through reference carrier and registers that named hero for the current Resolve session.
6. On a comparison clip, add a fresh Camera Match node. Enter the corresponding name in **Apply hero named**, select the same Chart model, align the chart, and press **Apply Hero to This Clip**. It copies that named hero, measures the current Source clip, fits the match, and saves the hero and correction in this node. **List Captured Heroes** displays the names currently registered. You can also copy a hero node to a comparison clip and press Apply Hero with **Apply hero named** empty; the copied node uses its embedded hero.
7. Repeat with another name for Scene 2. Use Hue Match, Saturation Match, Exposure Match, and Neutral Balance Match (all 0–100%). For color matching without exposure change, set Exposure Match to 0%. Copy each solved node to other shots from that camera and lighting setup.

After reopening Resolve, press **Use This Hero for Other Clips** on each saved hero node that you want to use with fresh nodes. Existing solved nodes keep their own saved hero and correction. Older hero nodes all named `Hero camera` can be assigned distinct names by editing **Capture hero as** and pressing **Use This Hero for Other Clips**; no new chart capture is needed. If Apply Hero cannot find a name, register its saved hero or capture it again.

Changing chart geometry after capture leaves the last correction active. Recapture to use new sample areas. Changing exclusions can be applied with **Refit Captured Samples**. Every capture and solve is explicit; playback and rendering never refit. `Bypass` is exact pass-through.

See [the algorithm notes](docs/algorithm.md) for the fitted model and limits.

## Current verification limits

The code builds and passes the automated checks, including a Metal comparison against the CPU transform. The system-installed plugin has appeared in Resolve's LoFi FX group. The named-hero transfer and explicit overlay redraw after parameter changes still need direct host verification on chart footage; capture, node copying, undo, save/reopen, and export need the same. Follow [the host test checklist](docs/host-test-checklist.md) before using the effect in a production project.

Chart patch rectangles were traced from manufacturer imagery and still need alignment validation against physical targets. The manual input-space contract cannot detect an incorrectly normalized upstream clip. The matcher fits a global exposure gain, relative neutral balance, and smooth hue/saturation fields; it does not fit tone curves, gamut compression, or a 3D LUT.
