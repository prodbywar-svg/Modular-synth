// Loads engine.wasm (the desktop app's engine compiled to WebAssembly) and
// wraps its C functions. Used by the audio worklet, the page and the tests.

export const Cmd = {
  Trigger: 0, SetVoiceType: 1, SetCharacter: 2, SetVoiceParam: 3, SetLevel: 4, SetPan: 5,
  SetMute: 6, SetSolo: 7, SetFxParam: 8, SetStep: 9, SelectPattern: 10, SetBpm: 11,
  SetSwing: 12, SetMasterLevel: 13, SetSample: 14,
  Play: 15, Stop: 16, RecordStart: 17, RecordStop: 18,
};

export const Voice = { Kick: 0, ClickHat: 1, NoiseSnare: 2, SineTone: 3, Glitch: 4, Sample: 5 };
export const Character = { Live: 0, Digital: 1, Hr16: 2 };

// The engine writes nothing to files or the console, so the few system calls
// the C library links in only need to exist.
const wasiStubs = {
  fd_close: () => 0,
  fd_seek: () => 0,
  fd_write: () => 0,
};

export function instantiate(module) {
  const instance = new WebAssembly.Instance(module, { wasi_snapshot_preview1: wasiStubs });
  return new Engine(instance);
}

export class Engine {
  constructor(instance) {
    this.x = instance.exports;
    if (this.x._initialize) this.x._initialize();
    this.numTracks = this.x.ms_num_tracks();
    this.numSteps = this.x.ms_num_steps();
    this.numPatterns = this.x.ms_num_patterns();
    this.numVoiceParams = this.x.ms_num_voice_params();
    this.numFxParams = this.x.ms_num_fx_params();
  }

  // Memory can grow, so views are made fresh each time.
  floats(ptr, count) { return new Float32Array(this.x.memory.buffer, ptr, count); }

  str(ptr) {
    const bytes = new Uint8Array(this.x.memory.buffer, ptr);
    let end = 0;
    while (bytes[end] !== 0) end++;
    return new TextDecoder().decode(bytes.slice(0, end));
  }

  // Copies a Float32Array into the module, calls fn(ptr, length), frees it.
  withFloats(data, fn) {
    const ptr = this.x.ms_alloc_floats(data.length);
    this.floats(ptr, data.length).set(data);
    try { return fn(ptr, data.length); } finally { this.x.ms_free_floats(ptr); }
  }

  setHr16(fileNumber, samples, sampleRate) {
    this.withFloats(samples, (p, n) => this.x.ms_set_hr16(fileNumber, p, n, sampleRate));
  }

  addSample(samples, sampleRate) {
    return this.withFloats(samples, (p, n) => this.x.ms_add_sample(p, n, sampleRate));
  }

  left(n) { return this.floats(this.x.ms_left(), n); }
  right(n) { return this.floats(this.x.ms_right(), n); }
}
