# LoFi FX Camera Match

A ColorChecker-based camera matching **OFX plugin for DaVinci Resolve**. Match the cameras in your scene to a reference camera, then reuse each correction across your footage—with GPU rendering on Apple silicon Macs.

When a cut changes skin tones, shifts a neutral wall, or makes one angle feel more saturated, camera matching becomes work you have to do before the creative grade. LoFi FX Camera Match uses chart footage from each camera to build a shared starting point. Choose the camera you want to match, capture its reference, and bring the other angles toward it without leaving Resolve’s Color page.

## Features

- **Reference camera matching** — use the camera you choose as the reference for color, skin tones, neutral balance, saturation, and brightness across the scene.
- **ColorChecker chart support** — built-in layouts for ColorChecker Video and the Passport Video color panel, including the video color target in Passport Video 2.
- **On-screen chart alignment** — position four corner handles, rotate or mirror the layout, and adjust sample areas to line up with your chart footage.
- **Selective patch sampling** — exclude patches affected by glare or obstructions so they do not drive the match.
- **Named references** — keep separate references for different scenes and lighting setups, then apply the right one to each camera.
- **Adjustable match strength** — dial the overall correction with Reference match %, plus separate Saturation match and Exposure match controls.
- **Reusable corrections** — copy a solved node to other shots from the same camera and lighting setup. Those shots do not need a chart, and the correction stays saved with the project.
- **Native DWG/Intermediate workflow** — match in DaVinci Wide Gamut / DaVinci Intermediate before your creative grade.
- **GPU rendering** — Metal rendering when Resolve supplies GPU buffers, with a CPU fallback. The current build targets Apple silicon macOS.

## Supported charts

The plugin has **two chart layouts**: full-size Video and Passport Video. Select the layout that matches your physical chart.

| Chart | Supported target | Choose in the plugin |
| --- | --- | --- |
| **Calibrite ColorChecker Video** | The full-size video color target with skin-tone patches, gray scales, and color patches. | **Color Checker Video** |
| **ColorChecker Passport Video** | The video color panel with 24 patches: color, skin-tone, gray, and check patches. Original X-Rite and Calibrite-branded charts must match this layout. | **Color Checker Passport Video** |
| **Calibrite ColorChecker Passport Video 2** | Its video color panel, using the Passport Video layout. The separate Classic panel is not used. | **Color Checker Passport Video** |

**Passport Video is the default.** Align the overlay to its color panel only; leave the separate three-bar grayscale panel outside the corners. The supported grid has four columns and six rows before rotation.

## Download

[Download v0.9 beta (macOS, Apple silicon)](https://github.com/lofi-fx/camera-match/releases/tag/v0.9-beta)

The download includes `LoFiFxCameraMatch.ofx.bundle`. Source builds are also available using the instructions below.

## Installing the plugin

Extract the downloaded zip to get `LoFiFxCameraMatch.ofx.bundle`. If building from source, the bundle is in `build/LoFiFxCameraMatch.ofx.bundle`.

1. Save your project and quit DaVinci Resolve.
2. In Finder, press **Cmd+Shift+G**, enter `/Library/OFX/Plugins/`, and move `LoFiFxCameraMatch.ofx.bundle` into that folder. Administrator permission may be required.
3. Restart Resolve. Find **LoFi FX Camera Match** under **LoFi FX** in the OFX effects list.

The beta is not notarized. If macOS blocks the downloaded plugin, use **System Settings → Privacy & Security → Allow Anyway** for this plugin, then restart Resolve.

For an automated build and installation, use the script under **Install from source** below.

## Using the plugin

### 1. Prepare your chart footage

Choose a chart frame from the reference camera and each comparison camera under the lighting you want to match. Avoid glare and shadows across the patches.

Bring both cameras into **DaVinci Wide Gamut / DaVinci Intermediate** before the plugin. Place Camera Match before creative grading or display conversion. If Resolve color management already supplies this space, no extra conversion is needed.

### 2. Capture your reference

Add Camera Match to the reference clip. Select the **Chart model**, activate **Open FX Overlay** in Resolve’s viewer, and align the four corners around the supported color target. Adjust rotation or mirroring until the sample labels match the physical patches.

Under **Capture**, enter a name in **Save reference as**, such as `Interview daylight`, then press **Capture reference**. The reference image stays unchanged.

### 3. Match another camera

Add Camera Match to the comparison camera’s chart frame. Enter your saved name in **Apply reference named**, select the same chart layout, and align its overlay.

Press **Apply reference to this clip** to measure this camera and save its match. Repeat for each camera in the setup. **List captured references** shows the references available in the current session.

### 4. Dial it in and reuse it

Start with all three controls at 100%, compare the shots, and adjust to taste.

| Control | What it does |
| --- | --- |
| **Reference match %** | Dials the overall correction. 0% leaves the image unchanged, 100% applies the fitted match, and up to 200% pushes it further. |
| **Saturation match** | Dials the saturation change. 0% retains input relative saturation while keeping matched hue for chromatic colors. |
| **Exposure match** | Dials the brightness change. 0% retains input luminance; 100% retains matched luminance. It follows the fitted correction rather than acting as a global Offset control. |

Use **Bypass** for a before-and-after comparison. Check skin, highlights, and familiar objects beyond the chart, then copy the solved node to other shots from that camera and lighting setup. Capture a new named reference when the lighting changes.

### Keeping a match up to date

- After moving or resizing sample areas, recapture. After changing patch exclusions, use **Advanced → Refit captured samples** to update the match from saved measurements.
- After changing exposure or color upstream, recapture the affected reference and/or press Apply reference again on the comparison chart frame. The saved match does not adapt automatically; Refit alone does not measure new pixels.
- After reopening Resolve, solved nodes keep working. To make a saved reference available to fresh nodes, press **Advanced → Use this reference for other clips** on its reference node. Reselect Open FX Overlay if the guides are hidden.

## Building from source

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
```

Requires: Apple silicon macOS, CMake 3.20+, a C++17 compiler with macOS development tools, and an internet connection on the first build to fetch the OpenFX SDK headers.

Output: `build/LoFiFxCameraMatch.ofx.bundle`

To build and run the automated checks without installing or launching Resolve:

```sh
./build-and-launch.sh --build-only
```

## Install from source

Save your Resolve project, then run:

```sh
./build-and-launch.sh
```

The script builds, runs tests, installs the bundle in `/Library/OFX/Plugins`, clears the stale plugin cache, and launches Resolve. If Resolve is running, it waits for you to quit before replacing the bundle. A first installation may require an administrator password.

You can also copy the built bundle manually using **Installing the plugin** above. To uninstall, remove `/Library/OFX/Plugins/LoFiFxCameraMatch.ofx.bundle` and restart Resolve.

## Current status

The plugin uses a single RBF model to match your chosen reference camera. Results depend on usable chart samples, accurate manual alignment, and comparable lighting. It does not automatically detect or track charts or continuously adapt a correction during playback. At very low or nonpositive luminance, the component controls retain the fitted result.

Automated regression checks cover the model, GPU agreement, saved corrections, and controls. Complete Resolve workflow verification is still pending, and reported highlight artifacts remain unresolved. See [implementation status](docs/implementation-status.md), [the host checklist](docs/host-test-checklist.md), and [algorithm notes](docs/algorithm.md) for details.

For older projects, Passport captures from versions 0.13–0.16 need both observations recaptured because the layout changed.
