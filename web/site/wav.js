// Reading and writing WAV files.

// Decodes a PCM or float WAV into one mono channel (channels averaged, like
// the desktop app). Returns null for anything it can't read.
export function decodeWavMono(arrayBuffer) {
  const v = new DataView(arrayBuffer);
  const tag = (o) => String.fromCharCode(v.getUint8(o), v.getUint8(o + 1), v.getUint8(o + 2), v.getUint8(o + 3));
  if (v.byteLength < 12 || tag(0) !== 'RIFF' || tag(8) !== 'WAVE') return null;

  let format = 0, channels = 0, sampleRate = 0, bits = 0, dataOffset = -1, dataLength = 0;
  for (let o = 12; o + 8 <= v.byteLength;) {
    const id = tag(o), size = v.getUint32(o + 4, true), body = o + 8;
    if (id === 'fmt ') {
      format = v.getUint16(body, true);
      channels = v.getUint16(body + 2, true);
      sampleRate = v.getUint32(body + 4, true);
      bits = v.getUint16(body + 14, true);
      if (format === 0xfffe && size >= 26) format = v.getUint16(body + 24, true); // extensible
    } else if (id === 'data') {
      dataOffset = body;
      dataLength = Math.min(size, v.byteLength - body);
    }
    o = body + size + (size & 1);
  }
  if (dataOffset < 0 || channels < 1 || !sampleRate) return null;

  const bytes = bits / 8;
  const frames = Math.floor(dataLength / (bytes * channels));
  const out = new Float32Array(frames);
  const read = {
    '1:8': (o) => (v.getUint8(o) - 128) / 128,
    '1:16': (o) => v.getInt16(o, true) / 32768,
    '1:24': (o) => ((v.getUint8(o) | (v.getUint8(o + 1) << 8) | (v.getInt8(o + 2) << 16))) / 8388608,
    '1:32': (o) => v.getInt32(o, true) / 2147483648,
    '3:32': (o) => v.getFloat32(o, true),
    '3:64': (o) => v.getFloat64(o, true),
  }[`${format}:${bits}`];
  if (!read) return null;

  const scale = 1 / channels;
  for (let i = 0; i < frames; i++) {
    let sum = 0;
    for (let c = 0; c < channels; c++) sum += read(dataOffset + (i * channels + c) * bytes);
    out[i] = sum * scale;
  }
  return { samples: out, sampleRate };
}

// Stereo 24-bit WAV, the desktop export format.
export function encodeWav24(left, right, sampleRate) {
  const frames = left.length;
  const dataSize = frames * 2 * 3;
  const buf = new ArrayBuffer(44 + dataSize);
  const v = new DataView(buf);
  const str = (o, s) => { for (let i = 0; i < s.length; i++) v.setUint8(o + i, s.charCodeAt(i)); };
  str(0, 'RIFF'); v.setUint32(4, 36 + dataSize, true); str(8, 'WAVE');
  str(12, 'fmt '); v.setUint32(16, 16, true); v.setUint16(20, 1, true); v.setUint16(22, 2, true);
  v.setUint32(24, sampleRate, true); v.setUint32(28, sampleRate * 6, true);
  v.setUint16(32, 6, true); v.setUint16(34, 24, true);
  str(36, 'data'); v.setUint32(40, dataSize, true);

  let o = 44;
  for (let i = 0; i < frames; i++) {
    for (const ch of [left, right]) {
      const s = Math.max(-1, Math.min(1, ch[i]));
      const x = Math.round(s < 0 ? s * 8388608 : s * 8388607);
      v.setUint8(o, x & 0xff); v.setUint8(o + 1, (x >> 8) & 0xff); v.setUint8(o + 2, (x >> 16) & 0xff);
      o += 3;
    }
  }
  return buf;
}
