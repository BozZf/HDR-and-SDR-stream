/**
 * Executable design reference for Discord's capture policy, NOT Discord code.
 * Recompute whenever source/display/encoder/transport/viewer capabilities change.
 * These input fields are a proposed app-level contract, not WebRTC SDP fields.
 */
export function chooseOutput({ preference = 'auto', source, encoder = {}, transport = {}, viewers = [] } = {}) {
  if (!['auto', 'sdr', 'hdr'].includes(preference)) throw new TypeError('Invalid preference');
  if (!['sdr709', 'scrgb-linear', 'pq2020', 'unknown'].includes(source)) throw new TypeError('Invalid source encoding');
  if (!Array.isArray(viewers)) throw new TypeError('viewers must be an array');
  if (source === 'unknown') {
    return { mode: 'blocked', reason: 'source-color-unknown', transform: 'none' };
  }
  if (source === 'sdr709') {
    return { mode: 'sdr', reason: 'source-is-sdr', transform: 'sdr709-pass' };
  }
  const sdr = reason => ({ mode: 'sdr', reason, transform: `${source}-to-sdr709` });
  if (preference === 'sdr') return sdr('user-selected-sdr');
  // Start conservatively; never infer that an unobserved receiver supports HDR.
  if (viewers.length === 0) return sdr('no-confirmed-viewer');
  if (encoder.tenBit !== true || encoder.pq2020 !== true) return sdr('encoder-not-hdr-ready');
  if (transport.hdrSignaling !== true) return sdr('transport-not-hdr-ready');
  if (!viewers.every(v => v && v.tenBitDecode === true && v.pq2020 === true && v.hdrDisplayActive === true)) {
    return sdr('receiver-not-hdr-ready');
  }
  return {
    mode: 'hdr', reason: 'all-paths-hdr-ready',
    transform: source === 'scrgb-linear' ? 'scrgb-to-pq2020' : 'pq2020-pass',
    bitDepth: 10, primaries: 'bt2020', transfer: 'pq', matrix: 'bt2020-ncl',
    // Actual encoder/surface code must consistently select and signal the range.
    range: 'limited',
  };
}
