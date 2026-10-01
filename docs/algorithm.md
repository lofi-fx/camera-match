# Matching model

## Color space and capture contract

Inputs are **DaVinci Wide Gamut / DaVinci Intermediate**. Intermediate decoding and DWG↔XYZ(D65) matrices follow [Blackmagic’s specification](https://documents.blackmagicdesign.com/InformationNotes/DaVinci_Resolve_17_Wide_Gamut_Intermediate.pdf). OKLab follows [Björn Ottosson’s XYZ(D65) matrices](https://bottosson.github.io/posts/oklab/), using signed cube roots. Negative and above-one channels are retained; no display clipping is applied.

Captures store robust scene-linear patch means, dispersion, pixel counts, quality flags, geometry, source bounds, render scale, pixel aspect ratio, chart model, name, time, and revision. Reference and target measurements come from the Source clip before this node’s correction. The checksum-validated node payload stores both captures and the frozen solution. Playback does not remeasure or refit.

Upstream exposure changes affect pixels entering the plugin, but do not update its saved measurements or fitted correction. To incorporate those changes, recapture the affected reference and/or apply the reference again on the target chart frame. **Refit captured samples** uses saved measurements, including updated exclusions; it does not fetch new pixels.

Passport Video uses the original model 1: four columns and six rows, patch origins x = 0.025 + column × 0.245 and y = 0.018 + row × 0.164, width 0.205 and height 0.13. Labels, roles, default inclusion of the first three columns, and zero default rotation match the original main branch. Saved rotations remain user-controlled. The temporary six-column model 2 remains readable for old captures and frozen corrections; recapture both observations before matching with restored model 1.

## RBF fitting and rendering

RBF is the sole matching model. Selected target/reference patch means are encoded into DI and fitted with a Gaussian radial-basis model plus an affine term and a black-to-black observation. At least seven usable patches are required. Support radius 0.1 and regularization 0.2 match Color Workspace’s DWG/DI preset in `rbf_model.py`, `pipeline.py`, and `lut_builder_tab.py`. The augmented Gaussian/affine system includes a zero polynomial constraint block. Singular coverage produces a fit error rather than an unconstrained fallback. The regularized black observation is not an exact zero constraint.

Metal evaluates every Gaussian and the affine term directly using 32-bit floats, with fast math disabled. The CPU reference uses doubles. There is no LUT, radius cutoff, clipping, or per-frame fitting. **Reference match %** blends input and fitted output in the saved model space, over 0–200%. Bypass and zero reference strength copy exactly when source/output formats match.

At fit time, 4,097 neutral-ramp samples check for decreasing decoded DWG luminance from zero through at least DI 1, extended to the largest captured channel if higher. A failed check rejects the new fit and retains the previous correction. This catches a real captured tonal fold reproduced with the Python solver. It does not prove monotonicity on arbitrary chromatic trajectories or at strengths above 100%; inconsistent captures and extrapolation remain limitations of an unconstrained RBF.

## Saturation and exposure controls

The component controls adjust the weighted RBF output without another fit. Both at 100% return that output exactly, avoiding any color-space round trip.

For positive usable source and matched luminance, **Exposure match** selects scene-linear luminance as `Yout = Yin × (Ymatched / Yin)^amount`. Zero retains input luminance; 100% retains matched luminance; intermediate values blend the ratio in stops. The ratio varies with each pixel’s RBF result. This control is not a single global exposure gain or DI offset.

**Saturation match** interpolates relative OKLab chroma `C/L` between input and matched values, retaining matched hue, then restores the chosen luminance. Its adjustment fades with smoothstep for matched C/L between zero and 1e-4, where hue becomes undefined. Nonpositive or very small luminance (<= 1e-7) retains the signed RBF output. Saturation reconstruction requires positive OKLab lightness and usable reconstructed luminance; finite fallbacks retain the weighted RBF output.

Reduced amounts intentionally change fitted patch predictions. The saved parameter IDs remain `biasWeight`, `rbfSat`, and `rbfExposure`; defaults are all 100%. No method selector or removed matching algorithm remains in the render path.

## Persistence and migration

CM7 saves RBF coefficients and fitting space only. The compact reader accepts CM2–CM6 captures while skipping obsolete fields. CM5/CM6 RBF outputs remain frozen, including their saved model space. CM5 linear-DWG fits retain that interpretation until explicitly refitted into DI. CM4 retains its established refit-on-load behavior. Older solved models obtain an RBF from saved captures; migration failure leaves an invalid correction. Rendering does not depend on the session’s named-reference registry.

## Reliability coverage and remaining evidence

The core suite covers Python DI golden outputs, dense signed/HDR ramps, component endpoints, exact bypass, brightness-fold rejection, chart geometry and sampling, reference transfer, checksums, legacy migration, and frozen payload round trips. The Metal suite compares CPU/GPU output across 36 strength/saturation/exposure combinations and includes a real captured highlight fit. The descriptor suite loads the built OFX and checks GPU support, retained capture/geometry parameters, and the three RBF controls’ IDs, labels, defaults, and ranges. Assertions stay enabled in Release test builds.

The captured highlight fixture checks Python-equation probes, exposure-zero luminance, CPU/GPU agreement below 2e-6 DI, and a dense monotonic neutral ramp. At all controls at 100%, this fixture returns the raw RBF result. These checks have not reproduced the reported visible highlight posterization. Its affine extrapolation compresses contrast beyond chart coverage; a same-frame source/output comparison is still needed to identify the affected pixels. Passing numerical tests does not establish visual quality on all footage.

`./build/camera_metal_tests --benchmark` measures a warmed 30-frame 3840×2160 RGBA float batch with 25 centers. It excludes Resolve decoding, other nodes, display transforms, and export overhead, so it is not an in-host playback measurement. For host-only behavior and verification gaps, see [the host checklist](host-test-checklist.md) and [implementation status](implementation-status.md).
