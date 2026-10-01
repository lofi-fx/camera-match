# Matching model

Inputs are DaVinci Wide Gamut / DaVinci Intermediate. Channel decoding uses the published DaVinci Intermediate linear toe and log branch; RGB is then handled in scene-linear DWG. The DWG↔XYZ(D65) matrices are from [Blackmagic’s specification](https://documents.blackmagicdesign.com/InformationNotes/DaVinci_Resolve_17_Wide_Gamut_Intermediate.pdf) and embedded in `src/core/Color.cpp`. OKLab uses [Björn Ottosson's XYZ(D65) matrices](https://bottosson.github.io/posts/oklab/) with signed cube roots and no sRGB clipping. Output is encoded back to Intermediate. Negative and above-one channels are retained.

Each capture stores the per-patch robust RGB mean, dispersion, usable and candidate pixel counts, quality flags, chart geometry, source bounds, render scale, pixel aspect ratio, chart model, name, time, and revision. Reference and target observations are measured from the Source clip before this node's correction. The checksum-validated payload contains both captures and the frozen solution. Rendering never fetches the chart or refits.

**RBF match** fits a Gaussian radial-basis model with an affine term from all selected target/reference patch RGB pairs, including neutrals, plus a black-to-black anchor. It needs at least seven usable chart patches. Its support radius is 0.1 and regularization is 0.2, matching the Python Color Workspace DWG/DI preset. The RBF maps encoded input RGB to encoded output RGB in the plugin's existing input contract and therefore handles neutral balance, exposure, hue, and saturation in one fit.

RBF match evaluates the Gaussian sum and affine term directly in DI coordinates on Metal using 32-bit floats; the CPU reference uses doubles. Every Gaussian is evaluated without a radius cutoff, LUT discretization, or input/output clipping. Metal fast math is disabled. Reference match % blends encoded input and output (0–200%). Negative and above-one values follow the same model. `Bypass` and zero-strength controls use exact copy when source/output formats match. No per-frame fitting occurs.

The checksum-validated `CM7` payload saves only RBF solution data, including fitting space and coefficients. CM5 nodes retain their previous linear-DWG interpretation until explicitly refitted; CM4 retains its existing refit-on-load migration, now into DI. CM2/CM3 remain readable. Capture RGB remains scene-linear, so refitting encodes saved patch means into DI without requiring a recapture. A black anchor is a regularized observation, not an exact zero constraint. Singular chart coverage produces a fit error rather than unconstrained affine extrapolation.

Relative saturation is defined as OKLab chroma divided by positive OKLab lightness. The RBF fit can alter luminance and tone locally; its output should be evaluated on held-out footage as well as chart patches.

## Investigation and verification, version 0.15

The comparison used `color_workspace/rbf_model.py`, `pipeline.py::_fit_and_generate`, and `lut_builder_tab.py::PRESETS["DWG/DI - TIFF"]`. That preset sets support 0.1, regularization 0.2, and a black anchor. The pipeline forces gamma working space and disables OKLab for DWG/DI: native DI patches enter the RBF unchanged. The plugin's 0.14 fit instead used linear patch means and decoded every input before RBF evaluation. Reusing a DI radius in linear light changes correction locality with exposure. This is a plausible source of contouring, not a confirmed reproduction of the user's footage.

The new solver uses the same Gaussian/affine block system, including its zero polynomial constraint block. Unlike Workspace's LUT export path, native rendering does not apply display-oriented clipping or LUT interpolation. This preserves the Resolve scene-referred range. Workspace's configurable per-center radii and optional luminance-extension anchors are not part of this fixed preset. Workspace has a least-squares fallback for singular systems; this plugin asks for more distinct patches instead.

Automated checks cover Python-generated DI reference outputs (absolute tolerance 1e-11), 65,537-point CPU identity and corrected ramps, and a 16,384-point Metal ramp (CPU agreement below 2e-6, no plateaus or reversals for the tested mapping). These fixtures do not guarantee monotonicity for arbitrary captures; an unconstrained RBF can still fold with inconsistent samples or extreme strength.

`./build/camera_metal_tests --benchmark` measures a warmed 30-frame 3840×2160 RGBA float batch with 25 centers, all Gaussian terms, and fast math disabled. On the local Apple M4 Pro it averaged 2.82 ms/frame (~355 frames/s). This excludes Resolve's decode, other nodes, display transforms, and export overhead; it is not an in-host playback measurement. GPU access is needed to run this check.

## Real capture diagnosis, version 0.16

Read-only extraction of the affected project's saved CM6 fit reproduced the failure with its actual 24 patch pairs. Both captures used rotation index 1 (90 degrees), default sample offsets, and the upright landscape Passport panel. The rotation transposed the six-column/four-row sampling grid across the physical patches. Those observations are not trustworthy patch correspondences.

Feeding those same pairs into Color Workspace's Python `fit_rbf_model` reproduces the saved coefficients to 5.5e-14. Scene-linear neutral brightness decreases over DI inputs approximately 0.1655–0.1842. This is a tonal fold, despite the Gaussian function being continuous, and explains why simple synthetic smoothness tests were insufficient. Changing the model coordinate space alone did not solve this capture failure.

Version 0.16 checks 4,097 neutral-ramp points at fit time, from zero through at least DI 1 (extended to the largest captured channel if higher). A decrease in decoded DWG luminance rejects the new fit with a recapture/alignment message; the previous correction is retained. A regression uses the actual numeric patch pairs without project identifiers. This check detects the observed neutral fold; it does not prove that arbitrary chromatic trajectories or strengths above 100% are free of folds. GPU rendering is unchanged and pays no per-frame validation cost.

For this capture, set Rotate chart to 0 degrees on both upright panels, verify sample rectangles sit inside the physical patches, recapture the reference, and apply it again to the target. Previously saved invalid fits are not silently replaced. The revised build still needs this corrected capture and in-host visual verification.


## Passport restoration, version 0.17

At the user's request, active Passport sampling now uses the exact main-branch model 1: four columns, six rows, origins x = 0.025 + column × 0.245 and y = 0.018 + row × 0.164, patch width 0.205 and height 0.13. Labels, roles, and default inclusion (first three columns) match main. The rotation parameter defaults to zero in main and this release; saved rotations remain user-controlled. Model 2 remains readable solely for old captures and frozen corrections. Captures using model 2 require recapture before matching with the restored model 1.

The prior attribution of the sampling problem to a saved 90-degree setting was incomplete: that setting had been used with the original geometry, and the branch had changed the underlying geometry. The demonstrated tonal fold in the saved fit is reproducible; the user reports improvement after correcting alignment. Native DI fitting, direct Metal evaluation, and the fit-time brightness check are retained.


## RBF saturation and exposure amounts, version 0.18

RBF fitting and coefficients are unchanged. First evaluate and weight the RBF in its saved model space. With both new amounts at 100%, return that exact output, without decode/re-encode or OKLab round-trip. Lower amounts decompose the weighted result in scene-linear DWG: saturation interpolates input and matched OKLab C/L while retaining matched hue and then restores luminance; exposure chooses Yout = Yin × (Ymatched / Yin)^amount. Thus zero exposure retains input Y, and full exposure retains matched Y independently of saturation. No secondary matching fit is used.

Relative chroma direction is undefined at neutral, so the saturation change fades with smoothstep over matched C/L from 0 to 1e-4. Nonpositive or very small Y (<= 1e-7) keeps the signed RBF result; OKLab saturation adjustment also requires positive L and a usable reconstructed Y. Finite fallbacks retain the original RBF output. No display clipping is added. These amounts intentionally modify the fitted result when below 100%; they do not preserve exact patch predictions at reduced amounts and do not guarantee arbitrary trajectory monotonicity.

Dedicated `rbfSat` and `rbfExposure` OFX parameters default to 100%, independent of obsolete parameters. Core and Metal use the same decomposition; regression checks cover endpoint luminance/saturation/hue, exact full-output preservation, signed fallback, exact zero-weight bypass, and CPU/GPU agreement for nine amount combinations.


## Sole matching path, version 0.19

The core solver, CPU transform, Metal shader, render parameters, and OFX descriptor now contain only RBF matching. The method dropdown and obsolete adjustment controls are removed. Reference match % is the label for the unchanged `biasWeight` parameter; `rbfSat`, `rbfExposure`, and bypass retain their saved IDs and meanings.

A compact legacy payload reader skips obsolete fields for CM2–CM6 compatibility. CM5/CM6 RBF outputs stay frozen, including their model space. CM4 retains its established refit-on-load behavior. Older non-RBF solved nodes obtain a new RBF from saved captures; the removed algorithms are never executed. Migration failures produce an invalid correction. CM7 writes no obsolete coefficients.

Verification includes the original Python golden outputs, component amounts, dense signed/HDR gradients, brightness-fold rejection, restored chart layout and sampling, captured reference transfer, checksum rejection, CM2–CM7 loading, and exact RBF output preservation across CM6/CM7 round trips. Metal checks 36 combinations of reference strength (0/50/100/200%) and saturation/exposure (0/50/100%). A descriptor test loads the built OFX, confirms GPU support and retained capture/geometry parameters, and verifies that Reference adjustments contains exactly the three visible RBF controls and no method selector.
