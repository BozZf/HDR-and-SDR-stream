# Proposal: explicit HDR capture and reliable SDR fallback in Discord Go Live

Status: unsolicited engineering proposal and executable policy reference.
Not a patch against Discord's proprietary source, an official roadmap, or a
claim about the internals of the current client. Prepared 2026-11-17.

## Problem and intended behavior

A user with Windows HDR enabled reports overexposed/washed-out screen sharing.
The local image is acceptable, while the remote image loses highlight detail.
The request is to keep the user's display in HDR and choose how shared content
is delivered: compatible SDR or HDR when the entire path supports it.

Reproduce on the current stable client before categorizing as a defect. Record
client version, Windows build, capture backend, GPU/driver, active display mode,
content type and receiver capabilities. Community reports do not establish the
behavior of the current release or the root cause of an individual machine.

Acceptance behavior: the user can share HDR content to an SDR recipient without
disabling local HDR, and explicitly see the effective output mode. HDR is offered
only when capture, encoder, signaling, decoder and active receiver display have
been verified. Unknown capabilities use tone-mapped SDR.

## Proposed UI

In the screen-sharing setup and live stream settings:

| Control | Behavior |
| --- | --- |
| Automatic (default) | SDR for unknown/mixed audiences; HDR only after positive end-to-end negotiation. |
| SDR compatibility | Always tone-map HDR sources to SDR; do not change local Windows HDR. |
| Prefer HDR | Use HDR when supported; otherwise show SDR fallback and its reason. |
| Effective output badge | “SDR · HDR source converted” or “HDR · 10-bit”, reflecting the active path. |
| Resolution | 720p (1280×720) or 1080p (1920×1080), subject to service/account capabilities. |
| Frame rate | 30 or 60 fps target; show the effective rate separately when constrained. |
| Capture source | Monitor list with display identity/resolution/primary status, or application window list with title and process identity. |

Source selection must be explicit: selecting a different item should not replace
the active stream until the user starts/confirms the change. Refresh the lists
after display changes, and reject stale window handles or disconnected displays.
Scale to the chosen output size with preserved aspect ratio and letterboxing.
Resolution and frame-rate settings must feed the encoder configuration in the
native integration; the standalone app only configures its intermediate output.

### Compact preview independent of output resolution

The standalone demonstrator now processes a full-resolution 720p/1080p GPU
texture before showing a 320×180 preview embedded in its controller. The preview
is a display copy of already-processed pixels, not a second HDR-to-SDR conversion.
Starting capture does not automatically open a large desktop window.

In a native Discord integration, feed the full-resolution texture to the encoder
and independently downscale the local preview. Never derive encoded resolution
from preview size. The standalone app still needs its optional full-size share
window because it has no direct encoder integration or virtual camera; sharing
a miniature window does not preserve full-resolution pixels at the receiver.

Changing this setting restarts/reconfigures the media path cleanly at a keyframe
boundary; do not briefly expose incorrectly tagged HDR frames to SDR receivers.
Explain fallback in plain language (“A viewer's display is in SDR”). Do not
expose internal texture formats or transfer-function names in the main UI.

## Capture and processing

1. For Windows Graphics Capture, obtain FP16 scRGB frames throughout capture.
   Preserve the source color description, content size and timestamps. Recreate
   frame pools after size changes; invalidate stale source/display metadata.
2. For SDR output, normalize scRGB luminance against the selected reference white,
   tone-map luminance and compress gamut on the GPU, then convert into the SDR
   encoder surface with consistent primaries, transfer, matrix and range.
3. For HDR output, convert linear scRGB Rec.709 to the encoder's supported
   10-bit HDR representation (for this proposal, BT.2020/PQ in P010). scRGB is
   **not** PQ; merely casting floats to 10-bit values is invalid. Preserve and
   signal luminance/color metadata only when known; do not invent MaxCLL values.
4. Keep frames GPU-resident. Integrate before Discord's encoder instead of
   capturing a preview window. Retain the existing audio path and A/V timestamps.

The standalone shader in this repository is a simple demonstrator, with manual
reference white and a smooth highlight shoulder. Production integration should
evaluate an established tone mapper and perceptual gamut mapping, respect the
OS SDR reference-white setting, and validate both HDR and SDR source windows.
The capture's source mode should determine whether to tone-map; avoid processing
an SDR source twice. HDR output of the standalone app is local FP16 presentation,
not the proposed PQ video encoder path.

## Negotiation and fallback

`capture-policy.mjs` is an executable reference for the decision rules. Its
capability keys are **proposed internal fields**, not standardized SDP attributes
or existing Discord APIs. The implementation must connect them to real media and
display capability checks and define reliable propagation to receivers.

Initial implementation: one SDR rendition whenever any recipient lacks a
confirmed HDR path. When an SDR recipient joins an HDR session, reconfigure the
sender and receivers together before delivering frames in the new format. Until
the transition is acknowledged, pause video or keep a known-safe rendition;
switching metadata alone is insufficient. Reevaluate when the receiver moves
the stream to an SDR display or disables HDR.

Later optimization: separate HDR and SDR renditions where infrastructure and
hardware budgets permit, allowing mixed audiences without downgrading everyone.
This may require another encode or a server-side conversion; quantify its cost.
Do not promise universal codec, platform or hardware support based solely on
the codec's family name. Validate the negotiated profile/bit depth and color
signaling along the actual path.

## Performance and privacy

Use bounded pools, avoid CPU readback, cap processing to the effective stream
rate, and discard stale frames instead of accumulating latency. Recover from
device removal and display changes. Do not change the user's system HDR mode.
Preserve the Windows capture picker/indicator and existing consent boundaries.

Benchmark baseline and proposed paths for frame time, video engine/GPU/CPU usage,
memory, encode quality and A/V latency on Intel, AMD and NVIDIA, including hybrid
GPUs. Targets should be set from measured baselines; this proposal supplies no
unmeasured latency, CPU or memory claims.

## Validation matrix

| Source | Audience | Expected |
| --- | --- | --- |
| HDR game/window | SDR display | Tone-mapped SDR, stable neutral UI, retained highlight separation. |
| HDR desktop | Mixed SDR/HDR | Single SDR rendition initially; accurate effective-mode badge. |
| HDR game/window | Fully negotiated HDR | HDR with valid 10-bit profile and consistent PQ/BT.2020 signaling. |
| SDR window on HDR desktop | Any audience | Correct SDR interpretation; no double tone mapping. |
| Unknown color metadata | Any audience | Block/retry capture with clear diagnostic; do not guess a transfer function. |
| HDR session → SDR viewer joins | Mixed | Coordinated transition to SDR without an overexposed frame interval. |
| HDR viewer moves playback to SDR monitor | SDR destination | Capability update and SDR fallback. |
| Resize, reconnect display, driver reset | Any audience | Safe reinitialization, no stale frames; audio/video resynchronized. |

Include reference patches and PQ/scRGB ramps, SDR UI white, dark gradients,
saturated wide-gamut colors and bright highlights. Compare decoded frames
numerically in an appropriate linear-light space and review on calibrated HDR
and SDR displays. A phone photo alone is not a color measurement.

Run the decision-policy suite (Node 20 or newer, no dependencies):

```sh
node --test proposal/capture-policy.test.mjs
```

The policy tests do not implement or verify video capture, encoding, transport,
receiver playback or the transition state machine. Those require integration
tests inside the service.

## Rollout

Start with corrected SDR conversion under an opt-in, versioned experiment with
quick rollback. Add capability-gated HDR after SDR conversion is stable. Collect
aggregate fallback reasons, conversion failures and timing; avoid collecting
screen content. Compare with the baseline on the same hardware/content.

## Primary references

- Microsoft: [Screen capture — HDR FP16 guidance](https://learn.microsoft.com/en-us/windows/apps/develop/media-authoring-processing/screen-capture)
- Microsoft: [Advanced Color and linear scRGB](https://learn.microsoft.com/en-us/windows/win32/direct3darticles/high-dynamic-range)
- Microsoft: [HDR tone-map effect](https://learn.microsoft.com/en-us/windows/win32/direct2d/hdr-tone-map-effect)
- Microsoft: [P010 and other 10-bit YUV formats](https://learn.microsoft.com/en-us/windows/win32/medfound/10-bit-and-16-bit-yuv-video-formats)
- W3C: [WebCodecs color-space definitions](https://www.w3.org/TR/webcodecs/)
- Discord: [Go Live and Screen Share](https://support.discord.com/hc/en-us/articles/360040816151-Go-Live-and-Screen-Share)

This branch is suitable as an attachment to a feature request. It has not been
sent to Discord and contains no modified Discord client or undocumented toggles.
