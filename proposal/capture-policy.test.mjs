import { test } from 'node:test';
import assert from 'node:assert/strict';
import { chooseOutput } from './capture-policy.mjs';

const viewer = { tenBitDecode: true, pq2020: true, hdrDisplayActive: true };
const ready = {
  source: 'scrgb-linear', encoder: { tenBit: true, pq2020: true },
  transport: { hdrSignaling: true }, viewers: [viewer],
};
test('HDR source and explicitly confirmed end-to-end capabilities select HDR', () => {
  assert.deepEqual(chooseOutput(ready), {
    mode: 'hdr', reason: 'all-paths-hdr-ready', transform: 'scrgb-to-pq2020',
    bitDepth: 10, primaries: 'bt2020', transfer: 'pq', matrix: 'bt2020-ncl', range: 'limited',
  });
});
test('SDR selection overrides HDR capabilities', () => {
  assert.equal(chooseOutput({ ...ready, preference: 'sdr' }).reason, 'user-selected-sdr');
});
test('HDR preference falls back safely on mixed SDR/HDR audience', () => {
  const result=chooseOutput({ ...ready, preference:'hdr', viewers:[viewer,{ ...viewer,hdrDisplayActive:false }] });
  assert.equal(result.mode,'sdr');
  assert.equal(result.reason,'receiver-not-hdr-ready');
  assert.equal(result.transform,'scrgb-linear-to-sdr709');
});
test('SDR source is not artificially expanded to HDR', () => {
  assert.equal(chooseOutput({ ...ready,source:'sdr709',preference:'hdr' }).transform,'sdr709-pass');
});
test('Unknown source metadata is blocked instead of guessing gamma', () => {
  assert.equal(chooseOutput({ ...ready,source:'unknown' }).mode,'blocked');
});
test('No audience starts in SDR', () => {
  assert.equal(chooseOutput({ ...ready,viewers:[] }).reason,'no-confirmed-viewer');
});
for (const field of ['tenBit','pq2020']) {
  test(`Missing encoder ${field} forces tone-mapped SDR`, () => {
    assert.equal(chooseOutput({ ...ready,encoder:{ ...ready.encoder,[field]:undefined } }).reason,'encoder-not-hdr-ready');
  });
}
test('Missing transport HDR signaling forces SDR', () => {
  assert.equal(chooseOutput({ ...ready,transport:{} }).reason,'transport-not-hdr-ready');
});
for (const field of ['tenBitDecode','pq2020','hdrDisplayActive']) {
  test(`Unknown viewer ${field} forces SDR`, () => {
    assert.equal(chooseOutput({ ...ready,viewers:[{ ...viewer,[field]:undefined }] }).mode,'sdr');
  });
}
test('A new SDR viewer changes an existing HDR session to SDR', () => {
  assert.equal(chooseOutput(ready).mode,'hdr');
  assert.equal(chooseOutput({ ...ready,viewers:[viewer,{}] }).mode,'sdr');
});
test('Turning off receiver HDR requests SDR on next capability update', () => {
  assert.equal(chooseOutput({ ...ready,viewers:[{ ...viewer,hdrDisplayActive:false }] }).mode,'sdr');
});
test('PQ source passes through HDR and gets the correct SDR conversion label', () => {
  assert.equal(chooseOutput({ ...ready,source:'pq2020' }).transform,'pq2020-pass');
  assert.equal(chooseOutput({ ...ready,source:'pq2020',preference:'sdr' }).transform,'pq2020-to-sdr709');
});
test('Capabilities require explicit booleans, not truthy strings', () => {
  assert.equal(chooseOutput({ ...ready,encoder:{ tenBit:'true',pq2020:true } }).mode,'sdr');
});
test('Invalid user preference and source encoding are rejected', () => {
  assert.throws(() => chooseOutput({ ...ready,preference:'magic' }),TypeError);
  assert.throws(() => chooseOutput({ ...ready,source:'guess' }),TypeError);
});
