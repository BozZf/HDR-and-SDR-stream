# Feature request: HDR sharing controls with reliable SDR tone mapping

When I share HDR content from my Windows PC, viewers can see an overexposed or
washed-out image. I would like to keep HDR enabled on my own monitor while
choosing the output format of the stream.

Please add an Automatic / SDR compatibility / Prefer HDR setting to Go Live:

Keep this alongside explicit 720p/1080p, 30/60 fps, and monitor/application source
controls. For multiple monitors, show which display is being selected. Clearly
distinguish the chosen quality from any effective limits of the service.

- SDR compatibility should tone-map HDR sources before encoding, preserving
  highlight detail and producing correctly tagged SDR video.
- HDR should be enabled only when the encoder, transport and each recipient's
  decoder and active display support the negotiated HDR format.
- If HDR is unavailable, fall back to SDR and clearly show the effective mode.

I have included a small native Windows capture prototype, a technical proposal,
and an executable reference for the fallback policy. The prototype demonstrates
the proposed processing approach; it is not a patch against Discord's client.

Before triage I can provide: Discord version, Windows build, GPU/driver, monitor
model, content/application, whether HDR is enabled at both ends, and a comparison
between the source and the received stream. These details are not yet filled in.

See `DISCORD_RFC.md` for architecture, negotiation rules and validation criteria.
