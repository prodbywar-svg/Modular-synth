// Runs on the browser's audio thread: the engine makes the sound here, and
// about 30 times a second the page gets the playhead, meters and the newest
// samples of each track for the signal boxes.

import { instantiate } from './engine.js';

const SCOPE = 2048;

class ModularSynthProcessor extends AudioWorkletProcessor {
  constructor(options) {
    super();
    this.engine = instantiate(new WebAssembly.Module(options.processorOptions.wasm));
    this.x = this.engine.x;
    this.tracks = this.engine.numTracks;
    this.x.ms_prepare(sampleRate);

    this.scope = new Float32Array(this.tracks * SCOPE);
    this.scopePos = 0;
    this.hits = new Int32Array(this.tracks);
    this.peaks = new Float32Array(this.tracks);
    this.masterPeak = 0;
    this.sinceReport = 0;
    this.reportEvery = Math.round(sampleRate / 30);

    this.port.onmessage = (e) => this.handle(e.data);
  }

  handle(m) {
    const x = this.x;
    if (m.type === 'cmd') {
      for (const c of m.list) {
        const wasRecording = x.ms_recording();
        x.ms_command(c[0], c[1], c[2], c[3], c[4] || 0);
        if (wasRecording && !x.ms_recording()) this.sendTake();
      }
    } else if (m.type === 'hr16') {
      for (const f of m.files) this.engine.setHr16(f.n, f.samples, f.sampleRate);
    } else if (m.type === 'sample') {
      this.engine.addSample(m.samples, m.sampleRate);
    } else if (m.type === 'loadDefault') {
      x.ms_engine_load_default();
    }
  }

  // A finished recording goes to the page, which renders the export.
  sendTake() {
    const x = this.x;
    const count = x.ms_recorded_count();
    const ptr = x.ms_recorded_events();
    const events = new Float64Array(x.memory.buffer, ptr, count * 6).slice();
    this.port.postMessage({ type: 'take', events, length: x.ms_record_position(), sampleRate }, [events.buffer]);
  }

  process(inputs, outputs) {
    const out = outputs[0];
    const n = out[0].length;
    const x = this.x;
    x.ms_process(n);
    out[0].set(this.engine.left(n));
    if (out[1]) out[1].set(this.engine.right(n));

    for (let t = 0; t < this.tracks; t++) {
      const src = this.engine.floats(x.ms_track_output(t), n);
      const base = t * SCOPE;
      for (let i = 0; i < n; i++) this.scope[base + ((this.scopePos + i) % SCOPE)] = src[i];
      this.peaks[t] = Math.max(this.peaks[t], x.ms_track_peak(t));
      if (x.ms_track_triggered(t)) this.hits[t]++;
    }
    x.ms_clear_triggered();
    this.scopePos = (this.scopePos + n) % SCOPE;
    this.masterPeak = Math.max(this.masterPeak, x.ms_master_peak());

    this.sinceReport += n;
    if (this.sinceReport >= this.reportEvery) {
      this.sinceReport = 0;
      // newest SCOPE samples per track, oldest first
      const scope = new Float32Array(this.tracks * SCOPE);
      for (let t = 0; t < this.tracks; t++) {
        const base = t * SCOPE;
        const head = this.scope.subarray(base + this.scopePos, base + SCOPE);
        scope.set(head, base);
        scope.set(this.scope.subarray(base, base + this.scopePos), base + head.length);
      }
      this.port.postMessage({
        type: 'status',
        playing: x.ms_playing(), recording: x.ms_recording(), recordSeconds: x.ms_record_position() / sampleRate,
        recordedEvents: x.ms_recorded_count(),
        step: x.ms_step(), pattern: x.ms_pattern(), queued: x.ms_queued_pattern(),
        peaks: this.peaks.slice(), masterPeak: this.masterPeak, hits: this.hits.slice(), scope,
      }, [scope.buffer]);
      this.peaks.fill(0);
      this.masterPeak = 0;
    }
    return true;
  }
}

registerProcessor('modular-synth', ModularSynthProcessor);
