// Applies a BPS patch (https://github.com/Alcaro/Flips/blob/master/bps_spec.md) in the browser.
"use strict";

const CRC_TABLE = (() => {
  const t = new Uint32Array(256);
  for (let n = 0; n < 256; n++) {
    let c = n;
    for (let k = 0; k < 8; k++) c = c & 1 ? 0xedb88320 ^ (c >>> 1) : c >>> 1;
    t[n] = c >>> 0;
  }
  return t;
})();

function crc32(bytes, end = bytes.length) {
  let c = 0xffffffff;
  for (let i = 0; i < end; i++) c = CRC_TABLE[(c ^ bytes[i]) & 0xff] ^ (c >>> 8);
  return (c ^ 0xffffffff) >>> 0;
}

function readU32(bytes, at) {
  return (bytes[at] | (bytes[at + 1] << 8) | (bytes[at + 2] << 16) | (bytes[at + 3] << 24)) >>> 0;
}

// The CRC32 a source file must have for this patch.
function bpsSourceCrc(patch) {
  return readU32(patch, patch.length - 12);
}

function applyBps(source, patch) {
  if (String.fromCharCode(...patch.subarray(0, 4)) !== "BPS1") throw new Error("Not a BPS patch.");
  if (crc32(patch, patch.length - 4) !== readU32(patch, patch.length - 4)) throw new Error("The patch file is damaged.");
  if (crc32(source) !== bpsSourceCrc(patch)) throw new Error("This isn't the right base game file.");

  let p = 4;
  const end = patch.length - 12;
  const number = () => {
    let data = 0, shift = 1;
    for (;;) {
      const x = patch[p++];
      data += (x & 0x7f) * shift;
      if (x & 0x80) return data;
      shift *= 128;
      data += shift;
    }
  };
  const sourceSize = number();
  const targetSize = number();
  const metadataSize = number();
  p += metadataSize;
  if (sourceSize !== source.length) throw new Error("This isn't the right base game file.");

  const target = new Uint8Array(targetSize);
  let out = 0, sourceRel = 0, targetRel = 0;
  while (p < end) {
    const data = number();
    const mode = data % 4;
    let length = Math.floor(data / 4) + 1;
    if (mode === 0) {
      target.set(source.subarray(out, out + length), out);
      out += length;
    } else if (mode === 1) {
      target.set(patch.subarray(p, p + length), out);
      p += length;
      out += length;
    } else {
      const offset = number();
      const delta = (offset & 1 ? -1 : 1) * Math.floor(offset / 2);
      if (mode === 2) {
        sourceRel += delta;
        target.set(source.subarray(sourceRel, sourceRel + length), out);
        sourceRel += length;
        out += length;
      } else {
        // Byte by byte: a target copy may overlap the bytes it is writing.
        targetRel += delta;
        while (length--) target[out++] = target[targetRel++];
      }
    }
  }
  if (crc32(target) !== readU32(patch, patch.length - 8)) throw new Error("Patching failed: the result doesn't match.");
  return target;
}

if (typeof module !== "undefined") module.exports = { applyBps, bpsSourceCrc, crc32 };
