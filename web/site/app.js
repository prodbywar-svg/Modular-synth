// The browser version's screens. The page keeps its own copy of the project
// inside a second copy of the engine (for readouts and exports) and sends
// every change to the audio worklet, like the desktop editor does with the
// audio thread.

import { instantiate, Cmd, Voice, Character } from './engine.js';
import { decodeWavMono, encodeWav24 } from './wav.js';

const $ = (s) => document.querySelector(s);
const el = (tag, cls, text) => {
  const e = document.createElement(tag);
  if (cls) e.className = cls;
  if (text !== undefined) e.textContent = text;
  return e;
};
const pad2 = (n) => String(n).padStart(2, '0');
const pad3 = (n) => String(n).padStart(3, '0');
const clamp = (v, lo, hi) => Math.min(hi, Math.max(lo, v));
const velLabel = (v) => pad3(Math.round(v * 127));

const NT = 8, NS = 16, NP = 4;
const SCOPE = 2048;
const PAD_KEYS = ['1a', '2s', '3d', '4f', '5g', '6h', '7j', '8k'];

let E, x;              // the page's engine
let wasmBytes;
let ctx, node;
let hr16Files = [];

const ui = {
  selected: 0,          // track on screen, -1 = master
  lastTrack: 0,         // the drum shown on SEQ while the master is selected
  view: 'seq',
  section: 0,
  snap: true,
  playing: false, recording: false, recordSeconds: 0,
  step: 0,
  peaks: new Float32Array(NT), masterPeak: 0, masterHold: 0,
  hits: new Int32Array(NT),
  scope: null,
  clipboard: null,
  undo: null,
  sampleNames: {},      // sample id -> name
  busy: false,
  status: '', statusTime: 0,
};

// ============================================================================
// Talking to the engines

let outbox = [];
function post(list) {
  if (!node) return;
  if (outbox.length === 0) queueMicrotask(() => { node.port.postMessage({ type: 'cmd', list: outbox }); outbox = []; });
  outbox.push(...list);
}

const transport = new Set([Cmd.Play, Cmd.Stop, Cmd.RecordStart, Cmd.RecordStop, Cmd.Trigger]);

function send(type, track = 0, index = 0, value = 0, sampleId = 0) {
  if (!transport.has(type)) {
    x.ms_ui_apply(type, track, index, value, sampleId);
    scheduleSave();
  }
  post([[type, track, index, value, sampleId]]);
  scheduleRefresh();
}

// ---- reading the page's copy of the project
const S = {
  bpm: () => x.ms_ui_bpm(),
  swing: () => x.ms_ui_swing(),
  pattern: () => x.ms_ui_pattern(),
  queued: () => x.ms_ui_queued_pattern(),
  viewed: () => x.ms_ui_viewed_pattern(),
  voice: (t) => x.ms_ui_voice(t),
  character: (t) => x.ms_ui_character(t),
  step: (t, p, s) => x.ms_ui_step(t, p, s),
  steps: (t, p = S.viewed()) => Array.from({ length: NS }, (_, s) => x.ms_ui_step(t, p, s)),
  mute: (t) => !!x.ms_ui_mute(t),
  solo: (t) => !!x.ms_ui_solo(t),
  level: (t) => (t < 0 ? x.ms_ui_master_level() : x.ms_ui_level(t)),
  pan: (t) => x.ms_ui_pan(t),
  fx: (t, i) => x.ms_ui_fx(t, i),
  voiceParam: (t, i) => x.ms_ui_voice_param(t, i),
  sampleId: (t) => x.ms_ui_sample_id(t),
};

const characterLabel = (c) => ['LIVE', 'DIGITAL', 'HR-16'][c] || '';

function trackName(t) {
  if (S.voice(t) === Voice.Sample) {
    const id = S.sampleId(t);
    return id ? ui.sampleNames[id] || 'SAMPLE' : 'SAMPLE';
  }
  return E.str(x.ms_drum_name(S.voice(t), S.character(t)));
}

function trackInfo(t) {
  if (S.voice(t) === Voice.Sample) return S.sampleId(t) ? 'SAMPLE' : 'EMPTY / LOAD IN SOUND';
  const v = S.voiceParam(t, 0);
  return characterLabel(S.character(t)) + ' / ' + E.str(x.ms_format_voice_param(t, 0, v));
}

function setStatus(text) {
  ui.status = text;
  ui.statusTime = performance.now();
  scheduleRefresh();
}

// ============================================================================
// Building the screens

const tracksEl = $('#tracks'), tagsEl = $('#tags'), gridEl = $('#grid'), padsEl = $('#pads');
const rows = [], tags = [], gridRows = [], pads = [], padStrip = [];
let masterRow;

function buildSeq() {
  for (let t = 0; t < NT; t++) {
    const row = el('div', 'track');
    const num = el('button', 'num', pad2(t + 1));
    num.setAttribute('aria-label', `Hit drum ${t + 1}`);
    num.addEventListener('pointerdown', (e) => { e.preventDefault(); hitPad(t, padVelocity(e, num)); select(t); });
    const who = el('button', 'who');
    who.append(el('b'), el('i'));
    who.addEventListener('click', () => select(t));
    const strip = el('button', 'strip');
    strip.setAttribute('aria-label', `Select drum ${t + 1}`);
    for (let s = 0; s < NS; s++) strip.append(el('span'));
    strip.addEventListener('click', () => select(t));
    const ms = el('div', 'ms');
    const m = el('button', '', 'M'), so = el('button', '', 'S');
    m.setAttribute('aria-label', `Mute drum ${t + 1}`);
    so.setAttribute('aria-label', `Solo drum ${t + 1}`);
    m.addEventListener('click', () => send(Cmd.SetMute, t, 0, S.mute(t) ? 0 : 1));
    so.addEventListener('click', () => send(Cmd.SetSolo, t, 0, S.solo(t) ? 0 : 1));
    ms.append(m, so);
    row.append(num, who, strip, ms);
    tracksEl.append(row);
    rows.push({ row, num, name: who.firstChild, info: who.lastChild, cells: [...strip.children], m, so });
  }

  masterRow = el('div', 'track master');
  const num = el('button', 'num', 'MS');
  const who = el('button', 'who');
  who.append(el('b', '', 'MASTER'), el('i'));
  const meter = el('div', 'meter');
  meter.append(el('div', 'line'), el('div', 'fill'), el('div', 'zero'));
  const goMaster = () => { select(-1); showView('sound'); };
  num.addEventListener('click', goMaster);
  who.addEventListener('click', goMaster);
  masterRow.append(num, who, meter);
  tracksEl.append(masterRow);
  masterRow.refs = { num, info: who.lastChild, fill: meter.children[1] };

  for (let s = 0; s < NS; s++) {
    const b = el('button', 'tag');
    b.setAttribute('aria-label', `Step ${s + 1}`);
    stepGesture(b, () => seqTrack(), s);
    tagsEl.append(b);
    tags.push(b);
  }

  for (let t = 0; t < NT; t++) {
    const row = el('div', 'row');
    const num = el('button', 'num', pad2(t + 1));
    num.addEventListener('pointerdown', (e) => { e.preventDefault(); hitPad(t, padVelocity(e, num)); select(t); });
    const nm = el('div', 'nm');
    const cells = el('div', 'cells');
    for (let s = 0; s < NS; s++) {
      const c = el('button', 'cell');
      stepGesture(c, () => t, s, () => select(t));
      cells.append(c);
    }
    row.append(num, nm, cells);
    gridEl.append(row);
    gridRows.push({ row, num, nm, cells: [...cells.children] });
  }
}

// Tap an empty step to switch it on, tap a lit one to switch it off, drag a
// lit one up or down to set its velocity (as on the desktop).
function stepGesture(button, trackOf, s, onDown) {
  let mode = null, startY = 0, startV = 0, t = 0;
  button.addEventListener('pointerdown', (e) => {
    e.preventDefault();
    if (onDown) onDown();
    t = trackOf();
    if (t < 0) return;
    button.setPointerCapture(e.pointerId);
    startY = e.clientY;
    startV = S.step(t, S.viewed(), s);
    if (startV > 0) mode = 'pendingOff';
    else { mode = 'on'; send(Cmd.SetStep, t, S.viewed() * NS + s, 1); }
  });
  button.addEventListener('pointermove', (e) => {
    if (!mode) return;
    const dy = e.clientY - startY;
    if (mode === 'pendingOff' && Math.abs(dy) > 4) mode = 'velocity';
    if (mode === 'velocity') {
      const v = clamp(startV - dy / 90, 0.05, 1);
      send(Cmd.SetStep, t, S.viewed() * NS + s, v);
      setStatus(`STEP ${pad2(s + 1)} VELOCITY ${velLabel(v)}`);
    }
  });
  const end = () => {
    if (mode === 'pendingOff') send(Cmd.SetStep, t, S.viewed() * NS + s, 0);
    mode = null;
  };
  button.addEventListener('pointerup', end);
  button.addEventListener('pointercancel', () => { mode = null; });
}

function buildPads() {
  for (let s = 0; s < NS; s++) padStrip.push($('#padStrip').appendChild(el('span')));
  for (let t = 0; t < NT; t++) {
    const p = el('button', 'pad');
    const bars = el('span', 'bars');
    [2, 1, 4, 1, 2, 6, 1, 3].forEach((w) => { const b = el('span'); b.style.width = w + 'px'; bars.append(b); });
    p.append(el('span', 'n', pad2(t + 1)), bars, el('span', 'nm'));
    p.addEventListener('pointerdown', (e) => { e.preventDefault(); hitPad(t, padVelocity(e, p)); });
    padsEl.append(p);
    pads.push(p);
  }
}

// Higher on the pad hits harder, like clicking the number boxes on the desktop.
function padVelocity(e, target) {
  const r = target.getBoundingClientRect();
  const rel = clamp((e.clientY - r.top) / Math.max(1, r.height), 0, 1);
  return clamp(1.15 - rel * 0.8, 0.3, 1);
}

function hitPad(t, velocity) {
  send(Cmd.Trigger, t, 0, velocity);
  flash(t);
}

const flashUntil = new Float64Array(NT);
function flash(t) {
  flashUntil[t] = performance.now() + 110;
  scheduleRefresh();
  setTimeout(scheduleRefresh, 120);
}

// ---- SOUND -------------------------------------------------------------------

const FX = (i) => ({ kind: 'fx', i });
const SECTIONS = [
  { name: 'VOICE + MIX', items: [{ kind: 'voice', i: 0 }, { kind: 'voice', i: 1 }, { kind: 'voice', i: 2 }, { kind: 'voice', i: 3 }, { kind: 'level' }, { kind: 'pan' }] },
  { name: 'CRUSH>DIST>FILTER', items: [0, 1, 2, 3, 4, 5, 6, 7].map(FX) },
  { name: 'DELAY>REVERB', items: [8, 9, 10, 11, 19, 20, 21, 22].map(FX) },
  { name: 'GRAIN', items: [12, 13, 14, 15, 16, 17, 18].map(FX) },
  { name: 'PSYCHOSIS', items: [23, 24, 25, 26, 27].map(FX) },
];

const soundChips = [], characterButtons = [], sectionButtons = [];
let sliders = [];
let shownSliders = '';

function buildSound() {
  const chips = $('#soundTracks');
  for (let t = -1; t < NT; t++) {
    const b = el('button', 'btn', t < 0 ? 'MS' : pad2(t + 1));
    b.addEventListener('click', () => select(t));
    chips.append(b);
    soundChips.push(b);
  }
  const chars = $('#characters');
  for (let c = 0; c < 3; c++) {
    const b = el('button', 'btn', characterLabel(c));
    b.addEventListener('click', () => { if (ui.selected >= 0) send(Cmd.SetCharacter, ui.selected, c, 0); });
    chars.append(b);
    characterButtons.push(b);
  }
  const secs = $('#sections');
  SECTIONS.forEach((sec, i) => {
    const b = el('button', '', sec.name);
    b.addEventListener('click', () => { ui.section = i; shownSliders = ''; scheduleRefresh(); });
    secs.append(b);
    sectionButtons.push(b);
  });
  $('#voiceType').addEventListener('change', (e) => {
    const t = ui.selected;
    if (t < 0) return;
    const v = parseInt(e.target.value, 10);
    if (v === Voice.Sample) { loadSampleInto(t); refresh(); return; }
    send(Cmd.SetVoiceType, t, v, 0);
  });
  $('#mute').addEventListener('click', () => ui.selected >= 0 && send(Cmd.SetMute, ui.selected, 0, S.mute(ui.selected) ? 0 : 1));
  $('#solo').addEventListener('click', () => ui.selected >= 0 && send(Cmd.SetSolo, ui.selected, 0, S.solo(ui.selected) ? 0 : 1));
  $('#loadSample').addEventListener('click', () => ui.selected >= 0 && loadSampleInto(ui.selected));
}

// What a slider shows and changes.
function sliderModel(item, t) {
  if (item.kind === 'voice') {
    if (t < 0) return null;
    const name = E.str(x.ms_voice_param_name(t, item.i));
    if (!name) return null;
    return {
      name,
      get: () => S.voiceParam(t, item.i),
      set: (v) => send(Cmd.SetVoiceParam, t, item.i, v),
      format: (v) => E.str(x.ms_format_voice_param(t, item.i, v)),
      def: () => x.ms_default_voice_param(t, item.i),
      steps: x.ms_voice_param_steps(t, item.i),
      pitch: !!x.ms_voice_param_is_pitch(t, item.i),
      snap: (v) => x.ms_snap_voice_param(t, item.i, v),
    };
  }
  if (item.kind === 'level') {
    return {
      name: t < 0 ? 'MASTER' : 'LEVEL',
      get: () => S.level(t),
      set: (v) => (t < 0 ? send(Cmd.SetMasterLevel, 0, 0, v) : send(Cmd.SetLevel, t, 0, v)),
      format: (v) => { const db = x.ms_level_db(v); return db <= -99 ? '-INF dB' : db.toFixed(1) + ' dB'; },
      def: () => (t < 0 ? 0.5 : 0.7071),
      steps: 0,
    };
  }
  if (item.kind === 'pan') {
    if (t < 0) return null;
    return {
      name: 'PAN',
      get: () => S.pan(t),
      set: (v) => send(Cmd.SetPan, t, 0, v),
      format: (v) => { const p = Math.round((v * 2 - 1) * 100); return p === 0 ? 'C' : (p < 0 ? 'L ' : 'R ') + Math.abs(p); },
      def: () => 0.5,
      steps: 0,
    };
  }
  const i = item.i;
  return {
    name: E.str(x.ms_fx_param_name(i)),
    get: () => S.fx(t, i),
    set: (v) => send(Cmd.SetFxParam, t, i, v),
    format: (v) => E.str(x.ms_format_fx_param(t, i, v)),
    def: () => x.ms_default_fx_param(i),
    steps: x.ms_fx_param_steps(i),
    toggle: !!x.ms_fx_param_is_toggle(i),
  };
}

function buildSliders() {
  const t = ui.selected;
  const key = `${t}:${ui.section}:${t >= 0 ? S.voice(t) + '/' + S.character(t) : ''}`;
  if (key === shownSliders) return;
  shownSliders = key;
  const box = $('#sliders');
  box.textContent = '';
  sliders = [];
  for (const item of SECTIONS[ui.section].items) {
    const m = sliderModel(item, t);
    if (!m) continue;
    const s = el('div', 'slider' + (m.toggle ? ' toggle' : ''));
    const row1 = el('div', 'row1');
    const nm = el('span', 'nm', m.name), val = el('span', 'val');
    row1.append(nm, val);
    s.append(row1);
    if (m.toggle) {
      const seg = el('div', 'seg');
      seg.append(el('span', '', 'MS'), el('span', '', 'NOTE'));
      s.append(seg);
      s.addEventListener('click', () => m.set(m.get() > 0.5 ? 0 : 1));
    } else {
      const rail = el('div', 'rail');
      rail.append(el('div', 'base'), el('div', 'fill'), el('div', 'knob'));
      s.append(rail);
      sliderGesture(s, rail, m);
    }
    box.append(s);
    sliders.push({ s, m, val });
  }
  if (sliders.length === 0) box.append(el('div', 'hint', 'NOTHING TO SET HERE'));
  const hint = el('div', 'hint');
  hint.style.display = 'flex';
  hint.style.justifyContent = 'space-between';
  hint.style.alignItems = 'center';
  hint.style.minHeight = '36px';
  hint.append(el('span', '', 'DRAG SIDEWAYS / DOUBLE-TAP RESETS'));
  if (ui.section === 0 && t >= 0) {
    const snap = el('button', 'btn small', '');
    snap.style.padding = '0 8px';
    snap.addEventListener('click', () => { ui.snap = !ui.snap; refresh(); });
    snap.dataset.role = 'snap';
    hint.append(snap);
  }
  box.append(hint);
}

function quantise(m, v) {
  v = clamp(v, 0, 1);
  if (m.steps > 1) v = Math.round(v * (m.steps - 1)) / (m.steps - 1);
  else if (m.pitch && ui.snap) v = m.snap(v);
  return v;
}

// Drag anywhere on the row: sideways moves the value (the full width of the
// row is the full range). Vertical moves scroll the page.
function sliderGesture(row, rail, m) {
  let id = null, startX = 0, startV = 0, moved = false, lastTap = 0;
  row.addEventListener('pointerdown', (e) => {
    id = e.pointerId; startX = e.clientX; startV = m.get(); moved = false;
  });
  row.addEventListener('pointermove', (e) => {
    if (e.pointerId !== id) return;
    const dx = e.clientX - startX;
    if (!moved && Math.abs(dx) < 6) return;
    if (!moved) { moved = true; row.setPointerCapture(id); row.classList.add('active'); }
    const w = rail.getBoundingClientRect().width || 300;
    m.set(quantise(m, startV + dx / w));
  });
  const end = (e) => {
    if (e.pointerId !== id) return;
    id = null;
    row.classList.remove('active');
    if (!moved) {
      const now = performance.now();
      if (now - lastTap < 350) { m.set(m.def()); lastTap = 0; } else lastTap = now;
    }
  };
  row.addEventListener('pointerup', end);
  row.addEventListener('pointercancel', (e) => { if (e.pointerId === id) { id = null; row.classList.remove('active'); } });
}

// ============================================================================
// Actions (the same as the desktop editor's)

function select(t) {
  ui.selected = t;
  if (t >= 0) ui.lastTrack = t;
  shownSliders = '';
  scheduleRefresh();
}

const seqTrack = () => (ui.selected >= 0 ? ui.selected : ui.lastTrack);

function selectPattern(p) {
  // While playing, the new pattern waits for the current one to finish.
  const queue = ui.playing;
  send(Cmd.SelectPattern, 0, p, queue ? 1 : 0);
  if (queue && S.queued() >= 0) setStatus(`P${p + 1} QUEUED / STARTS WHEN P${S.pattern() + 1} ENDS`);
}

function copyPatternTo(p) {
  const from = S.viewed();
  if (from === p) return;
  for (let t = 0; t < NT; t++) {
    const src = S.steps(t, from);
    for (let s = 0; s < NS; s++) send(Cmd.SetStep, t, p * NS + s, src[s]);
  }
  setStatus(`COPIED P${from + 1} TO P${p + 1}`);
}

function snapshot(pattern, withFx) {
  const snap = { pattern, withFx, steps: [], fx: [], masterFx: [] };
  for (let t = 0; t < NT; t++) {
    snap.steps.push(S.steps(t, pattern));
    snap.fx.push(Array.from({ length: E.numFxParams }, (_, i) => S.fx(t, i)));
  }
  snap.masterFx = Array.from({ length: E.numFxParams }, (_, i) => S.fx(-1, i));
  return snap;
}

function clearPattern() {
  // Empties the pattern on screen and puts every effects rack back to its defaults.
  const pattern = S.viewed();
  const before = snapshot(pattern, true);
  const def = (i) => x.ms_default_fx_param(i);
  let changed = before.masterFx.some((v, i) => v !== def(i));
  for (let t = 0; t < NT; t++) changed ||= before.fx[t].some((v, i) => v !== def(i)) || before.steps[t].some((v) => v > 0);
  if (!changed) return;
  for (let t = 0; t < NT; t++) {
    before.steps[t].forEach((v, s) => v > 0 && send(Cmd.SetStep, t, pattern * NS + s, 0));
    before.fx[t].forEach((v, i) => v !== def(i) && send(Cmd.SetFxParam, t, i, def(i)));
  }
  before.masterFx.forEach((v, i) => v !== def(i) && send(Cmd.SetFxParam, -1, i, def(i)));
  ui.undo = before;
  setStatus(`CLEARED P${pattern + 1} + RESET ALL EFFECTS / UNDO BRINGS IT BACK`);
}

function undo() {
  const snap = ui.undo;
  if (!snap) { setStatus('NOTHING TO UNDO'); return; }
  for (let t = 0; t < NT; t++) {
    const now = S.steps(t, snap.pattern);
    snap.steps[t].forEach((v, s) => v !== now[s] && send(Cmd.SetStep, t, snap.pattern * NS + s, v));
    if (snap.withFx) snap.fx[t].forEach((v, i) => send(Cmd.SetFxParam, t, i, v));
  }
  if (snap.withFx) snap.masterFx.forEach((v, i) => send(Cmd.SetFxParam, -1, i, v));
  setStatus(`UNDONE / P${snap.pattern + 1}${snap.withFx ? ' AND THE EFFECTS RESTORED' : ' RESTORED'}`);
  ui.undo = null;
}

function copySequence(whole) {
  const pattern = S.viewed();
  const t = seqTrack();
  if (whole) {
    ui.clipboard = { whole: true, steps: Array.from({ length: NT }, (_, i) => S.steps(i, pattern)), source: `P${pattern + 1}` };
  } else {
    ui.clipboard = { whole: false, steps: [S.steps(t, pattern)], source: `${pad2(t + 1)} ${trackName(t)} P${pattern + 1}` };
  }
  setStatus(`COPIED ${ui.clipboard.source}${whole ? ' (ALL TRACKS) / PICK A PATTERN AND PASTE' : ' / PICK A DRUM OR PATTERN AND PASTE'}`);
}

function pasteSequence() {
  const clip = ui.clipboard;
  if (!clip) { setStatus('NOTHING COPIED YET / PICK A DRUM AND PRESS COPY'); return; }
  const pattern = S.viewed();
  ui.undo = snapshot(pattern, false);
  const write = (t, steps) => {
    const now = S.steps(t, pattern);
    steps.forEach((v, s) => v !== now[s] && send(Cmd.SetStep, t, pattern * NS + s, v));
  };
  if (clip.whole) {
    for (let t = 0; t < NT; t++) write(t, clip.steps[t]);
    setStatus(`PASTED ${clip.source} INTO P${pattern + 1} / UNDO TO TAKE IT BACK`);
  } else {
    const t = seqTrack();
    write(t, clip.steps[0]);
    setStatus(`PASTED ${clip.source} INTO ${pad2(t + 1)} ${trackName(t)} P${pattern + 1}`);
  }
}

function randomizeSequence(t) {
  const pattern = S.viewed();
  const voice = S.voice(t);
  ui.undo = snapshot(pattern, false);
  // Chance of a hit on each step, shaped by what the drum is: kicks lean on
  // the beat, hats run, snares find the backbeat, everything else is sparse.
  const chance = (s) => {
    const beat = s % 4 === 0, eighth = s % 2 === 0;
    switch (voice) {
      case Voice.Kick: return s === 0 ? 1 : beat ? 0.55 : eighth ? 0.2 : 0.12;
      case Voice.ClickHat: return eighth ? 0.8 : 0.45;
      case Voice.NoiseSnare: return (s === 4 || s === 12) ? 0.8 : beat ? 0.1 : 0.14;
      default: return beat ? 0.3 : 0.2;
    }
  };
  let steps, hits = 0;
  while (hits === 0) {
    steps = Array.from({ length: NS }, (_, s) => {
      if (Math.random() >= chance(s)) return 0;
      hits++;
      return clamp(0.45 + (s % 4 === 0 ? 0.25 : 0) + Math.random() * 0.45, 0.3, 1);
    });
  }
  steps.forEach((v, s) => send(Cmd.SetStep, t, pattern * NS + s, v));
  setStatus(`RANDOM ${pad2(t + 1)} ${trackName(t)} P${pattern + 1} / PRESS AGAIN FOR ANOTHER`);
}

function togglePlay() { send(ui.playing ? Cmd.Stop : Cmd.Play); ui.playing = !ui.playing; scheduleRefresh(); }
function stop() { send(Cmd.Stop); ui.playing = false; scheduleRefresh(); }

function toggleRecord() {
  if (ui.recording) {
    send(Cmd.RecordStop);
    ui.recording = false;
  } else {
    // The take starts from the project as it is now.
    x.ms_rec_begin(ctx.sampleRate);
    send(Cmd.RecordStart);
    ui.recording = true;
    ui.playing = true;
    setStatus('RECORDING / PLAY PADS, MUTE, MOVE ANYTHING / REC AGAIN TO END');
  }
  scheduleRefresh();
}

function receiveTake(m) {
  const ev = m.events;
  for (let i = 0; i + 5 < ev.length; i += 6) x.ms_rec_add_event(ev[i], ev[i + 1], ev[i + 2], ev[i + 3], ev[i + 4], ev[i + 5]);
  x.ms_rec_set_length(m.length);
  setStatus(`TAKE READY / ${(m.length / m.sampleRate).toFixed(1)} s / EXPORT IT IN FILE`);
  scheduleRefresh();
}

// ---- samples -------------------------------------------------------------------

let sampleTarget = 0;
function loadSampleInto(t) {
  sampleTarget = t;
  $('#sampleInput').value = '';
  $('#sampleInput').click();
}

async function onSampleFile(file) {
  if (!file) return;
  const t = sampleTarget;
  const bytes = await file.arrayBuffer();
  let decoded = decodeWavMono(bytes);
  if (!decoded) {
    try {
      const buf = await ctx.decodeAudioData(bytes.slice(0));
      const mono = new Float32Array(buf.length);
      for (let c = 0; c < buf.numberOfChannels; c++) {
        const d = buf.getChannelData(c);
        for (let i = 0; i < d.length; i++) mono[i] += d[i] / buf.numberOfChannels;
      }
      decoded = { samples: mono, sampleRate: buf.sampleRate };
    } catch {
      setStatus('COULD NOT READ ' + file.name.toUpperCase());
      return;
    }
  }
  const id = E.addSample(decoded.samples, decoded.sampleRate);
  node.port.postMessage({ type: 'sample', samples: decoded.samples, sampleRate: decoded.sampleRate }, [decoded.samples.buffer]);
  ui.sampleNames[id] = file.name.replace(/\.[^.]+$/, '').toUpperCase();
  send(Cmd.SetVoiceType, t, Voice.Sample, 0);
  send(Cmd.SetSample, t, 0, 0, id);
  select(t);
  setStatus(`LOADED ${file.name.toUpperCase()} INTO TRACK ${pad2(t + 1)}`);
}

// ---- projects: the desktop's .msynth format (version 2) -----------------------

function projectToJson() {
  const fxObj = (t) => Object.fromEntries(Array.from({ length: E.numFxParams }, (_, i) => [E.str(x.ms_fx_param_name(i)), S.fx(t, i)]));
  return {
    format: 'modular-synth', version: 2,
    bpm: S.bpm(), swing: S.swing(), pattern: S.pattern(), masterLevel: S.level(-1), masterFx: fxObj(-1),
    tracks: Array.from({ length: NT }, (_, t) => ({
      voice: E.str(x.ms_voice_type_name(S.voice(t))),
      character: E.str(x.ms_character_name(S.character(t))),
      voiceParams: Array.from({ length: E.numVoiceParams }, (_, i) => S.voiceParam(t, i)),
      level: S.level(t), pan: S.pan(t), mute: S.mute(t), solo: S.solo(t),
      fx: fxObj(t),
      patterns: Array.from({ length: NP }, (_, p) => S.steps(t, p)),
      sample: S.sampleId(t) ? ui.sampleNames[S.sampleId(t)] || '' : '',
    })),
  };
}

function nameIndex(count, nameOf, name, fallback) {
  for (let i = 0; i < count; i++) if (E.str(nameOf(i)) === name) return i;
  return fallback;
}

// Replays a saved project as commands on top of the starter project.
function loadProject(p) {
  node.port.postMessage({ type: 'loadDefault' });
  x.ms_ui_reset();
  ui.playing = false;
  ui.recording = false;
  if (!p || p.format !== 'modular-synth') { scheduleRefresh(); scheduleSave(); return; }

  const num = (v, d) => (typeof v === 'number' && isFinite(v) ? v : d);
  const fxIndex = (name) => nameIndex(E.numFxParams, x.ms_fx_param_name, name, -1);
  const setFx = (t, fx) => {
    if (fx && !Array.isArray(fx)) for (const [k, v] of Object.entries(fx)) { const i = fxIndex(k); if (i >= 0) send(Cmd.SetFxParam, t, i, clamp(num(v, 0), 0, 1)); }
  };

  send(Cmd.SetBpm, 0, 0, num(p.bpm, 132));
  send(Cmd.SetSwing, 0, 0, num(p.swing, 0));
  send(Cmd.SetMasterLevel, 0, 0, num(p.masterLevel, 0.5));
  setFx(-1, p.masterFx);
  (p.tracks || []).slice(0, NT).forEach((tr, t) => {
    send(Cmd.SetVoiceType, t, nameIndex(6, x.ms_voice_type_name, tr.voice, 0), 0);
    send(Cmd.SetCharacter, t, nameIndex(3, x.ms_character_name, tr.character, Character.Digital), 0);
    (tr.voiceParams || []).slice(0, E.numVoiceParams).forEach((v, i) => send(Cmd.SetVoiceParam, t, i, clamp(num(v, 0), 0, 1)));
    send(Cmd.SetLevel, t, 0, num(tr.level, 0.7071));
    send(Cmd.SetPan, t, 0, num(tr.pan, 0.5));
    send(Cmd.SetMute, t, 0, tr.mute ? 1 : 0);
    send(Cmd.SetSolo, t, 0, tr.solo ? 1 : 0);
    setFx(t, tr.fx);
    const patterns = Array.isArray(tr.patterns) ? tr.patterns : [tr.steps || []];
    for (let pi = 0; pi < NP; pi++)
      for (let s = 0; s < NS; s++) send(Cmd.SetStep, t, pi * NS + s, clamp(num((patterns[pi] || [])[s], 0), 0, 1));
    if (tr.sample) {
      const id = Object.keys(ui.sampleNames).find((k) => ui.sampleNames[k] === tr.sample);
      if (id) send(Cmd.SetSample, t, 0, 0, +id);
    }
  });
  send(Cmd.SelectPattern, 0, clamp(num(p.pattern, 0) | 0, 0, NP - 1), 0);
  scheduleRefresh();
}

let saveTimer = 0;
function scheduleSave() {
  clearTimeout(saveTimer);
  saveTimer = setTimeout(() => {
    try { localStorage.setItem('modular-synth-project', JSON.stringify(projectToJson())); } catch { /* private mode */ }
  }, 400);
}

function restoreProject() {
  try {
    const saved = localStorage.getItem('modular-synth-project');
    if (saved) loadProject(JSON.parse(saved));
  } catch { /* fall back to the starter project */ }
}

// ---- files out ---------------------------------------------------------------

function stamp() {
  const d = new Date();
  return `${d.getFullYear()}${pad2(d.getMonth() + 1)}${pad2(d.getDate())}-${pad2(d.getHours())}${pad2(d.getMinutes())}${pad2(d.getSeconds())}`;
}

function offerFiles(files, label) {
  const box = $('#exportResult');
  box.textContent = '';
  if (navigator.canShare && navigator.canShare({ files })) {
    const b = el('button', 'btn wide on', `${label} / SHARE OR SAVE ${files.length} FILE${files.length > 1 ? 'S' : ''}`);
    b.addEventListener('click', () => navigator.share({ files }).catch(() => {}));
    box.append(b);
  }
  for (const f of files) {
    const a = el('a', '', `${f.name}  ${(f.size / 1048576).toFixed(1)} MB`);
    a.href = URL.createObjectURL(f);
    a.download = f.name;
    box.append(a);
  }
}

const safeName = (s) => (s.replace(/[^A-Za-z0-9\-_ ]/g, '').trim().replace(/ /g, '-') || 'track');

// Writes <base>_Master.wav plus one <base>_<NN>-<NAME>.wav per drum that makes
// sound, all the same length so they line up in a DAW (as the desktop does).
async function exportWav(loops) {
  if (ui.busy) return;
  const sr = ctx.sampleRate;
  let base;
  if (loops === 0) {
    if (!x.ms_take_use_recording()) { setStatus('NOTHING RECORDED YET / PRESS REC, PLAY, PRESS REC AGAIN'); return; }
    base = 'take-' + stamp();
  } else {
    x.ms_take_pattern(sr, loops, -1);
    base = `P${S.viewed() + 1}x${loops}-${stamp()}`;
  }
  ui.busy = true;
  const bar = $('#exportProgress');
  bar.classList.add('on');
  $('#exportResult').textContent = '';
  const progress = (f) => { bar.firstChild.style.width = (f * 100).toFixed(1) + '%'; };
  let lastYield = performance.now();
  const breathe = async () => {
    if (performance.now() - lastYield > 30) { await new Promise((r) => setTimeout(r, 0)); lastYield = performance.now(); }
  };

  const length = x.ms_take_length();
  const maxLength = length + 10 * sr, silentNeeded = 0.3 * sr;
  const passes = 1 + NT;
  const names = Array.from({ length: NT }, (_, t) => trackName(t));

  // pass 1: master, which also decides where the tail ends
  x.ms_render_start(-1);
  const chunksL = [], chunksR = [];
  let written = 0, silentRun = 0;
  while (written < maxLength) {
    const n = x.ms_render_next(4096);
    const l = E.left(n).slice(), r = E.right(n).slice();
    chunksL.push(l); chunksR.push(r);
    written += n;
    if (written > length) {
      let peak = 0;
      for (let i = 0; i < n; i++) peak = Math.max(peak, Math.abs(l[i]), Math.abs(r[i]));
      silentRun = peak < 0.00003 ? silentRun + n : 0;
      if (silentRun >= silentNeeded) break;
    }
    progress(Math.min(1, written / Math.max(1, length)) / passes);
    await breathe();
  }
  const join = (chunks) => { const out = new Float32Array(written); let o = 0; for (const c of chunks) { out.set(c, o); o += c.length; } return out; };
  const files = [new File([encodeWav24(join(chunksL), join(chunksR), sr)], `${base}_Master.wav`, { type: 'audio/wav' })];

  // passes 2..9: one stem per drum, same length as the master
  for (let t = 0; t < NT; t++) {
    x.ms_render_start(t);
    const L = new Float32Array(written), R = new Float32Array(written);
    let done = 0, peak = 0;
    while (done < written) {
      const n = x.ms_render_next(Math.min(4096, written - done));
      const l = E.left(n), r = E.right(n);
      L.set(l, done); R.set(r, done);
      for (let i = 0; i < n; i++) peak = Math.max(peak, Math.abs(l[i]), Math.abs(r[i]));
      done += n;
      progress((1 + t + done / written) / passes);
      await breathe();
    }
    // A drum that never made a sound gets no file.
    if (peak > 0) files.push(new File([encodeWav24(L, R, sr)], `${base}_${pad2(t + 1)}-${safeName(names[t])}.wav`, { type: 'audio/wav' }));
  }

  progress(1);
  bar.classList.remove('on');
  ui.busy = false;
  offerFiles(files, `${(written / sr).toFixed(1)} s`);
  setStatus(`EXPORTED ${files.length} FILES / ${(written / sr).toFixed(1)} s`);
}

function saveProjectFile() {
  const file = new File([JSON.stringify(projectToJson(), null, 1)], `project-${stamp()}.msynth`, { type: 'application/json' });
  showView('file');
  offerFiles([file], 'PROJECT');
}

async function onProjectFile(file) {
  if (!file) return;
  try {
    const p = JSON.parse(await file.text());
    if (p.format !== 'modular-synth') throw new Error('not a project');
    loadProject(p);
    setStatus('OPENED ' + file.name.toUpperCase());
  } catch {
    setStatus('COULD NOT OPEN ' + file.name.toUpperCase());
  }
}

// ============================================================================
// Drawing

let refreshQueued = false;
function scheduleRefresh() {
  if (refreshQueued) return;
  refreshQueued = true;
  requestAnimationFrame(() => { refreshQueued = false; refresh(); });
}

function refresh() {
  if (!x) return;
  const now = performance.now();
  const viewed = S.viewed();
  const playStep = ui.playing && S.pattern() === viewed ? ui.step : -1;
  const sel = ui.selected;
  const st = seqTrack();

  // header
  $('#bpmValue').textContent = S.bpm().toFixed(1);
  $('#swingValue').textContent = pad2(Math.round(S.swing() * 100)) + ' %';
  $('#stepValue').textContent = ui.recording
    ? `REC ${ui.recordSeconds.toFixed(1)} s`
    : `P${S.pattern() + 1} ${pad2((ui.playing ? ui.step : 0) + 1)}/16`;
  $('#stepValue').style.color = ui.recording ? 'var(--red)' : '';
  const status = $('#status');
  const fresh = ui.status && now - ui.statusTime < 6000;
  status.textContent = fresh ? ui.status : '8 TRACKS x 16 STEPS x 4 PATTERNS';
  status.classList.toggle('alert', !!fresh);

  // transport
  $('#play').classList.toggle('on', ui.playing);
  $('#rec').classList.toggle('on', ui.recording);
  $('#rec').textContent = ui.recording ? 'REC ON' : 'REC';
  document.querySelectorAll('.pat').forEach((b, p) => {
    b.classList.toggle('on', p === S.pattern());
    b.classList.toggle('queued', p === S.queued());
  });

  // SEQ rows
  for (let t = 0; t < NT; t++) {
    const r = rows[t];
    const steps = S.steps(t, viewed);
    r.row.classList.toggle('sel', t === sel);
    r.num.classList.toggle('hit', flashUntil[t] > now);
    r.name.textContent = trackName(t);
    r.info.textContent = trackInfo(t);
    steps.forEach((v, s) => {
      const c = r.cells[s];
      c.style.height = v > 0 ? Math.max(6, Math.round(v * 24)) + 'px' : '1px';
      c.className = s === playStep ? 'play' : v > 0 ? 'on' : '';
    });
    r.m.classList.toggle('on', S.mute(t));
    r.so.classList.toggle('on', S.solo(t));

    const g = gridRows[t];
    g.row.classList.toggle('sel', t === sel);
    g.num.classList.toggle('hit', flashUntil[t] > now);
    g.nm.textContent = trackName(t);
    steps.forEach((v, s) => {
      const c = g.cells[s];
      c.textContent = v > 0 ? velLabel(v) : '';
      c.className = 'cell' + (v > 0 ? ' on' : '') + (s === playStep ? ' play' : '');
    });

    const p = pads[t];
    p.lastChild.textContent = trackName(t);
    p.classList.toggle('hit', flashUntil[t] > now);
    p.classList.toggle('empty', S.voice(t) === Voice.Sample && !S.sampleId(t));
  }
  masterRow.classList.toggle('sel', sel < 0);
  masterRow.refs.num.style.background = sel < 0 ? 'var(--ink)' : '';
  masterRow.refs.num.style.color = sel < 0 ? 'var(--bg)' : '';
  const mdb = x.ms_level_db(S.level(-1));
  masterRow.refs.info.textContent = `SUM + RACK ${mdb <= -99 ? '-INF' : mdb.toFixed(1)} dB`;
  const fill = masterRow.refs.fill;
  fill.style.width = (clamp(Math.sqrt(ui.masterPeak) / Math.sqrt(1.4), 0, 1) * 100).toFixed(1) + '%';
  fill.classList.toggle('clip', ui.masterPeak > 1);

  const steps = S.steps(st, viewed);
  steps.forEach((v, s) => {
    const b = tags[s];
    b.textContent = v > 0 ? velLabel(v) : pad2(s + 1);
    b.className = 'tag' + (v > 0 ? ' on' : '') + (s === playStep ? ' play' : '');
  });
  padStrip.forEach((c, s) => { c.className = s === playStep ? 'play' : S.step(st, viewed, s) > 0 ? 'on' : ''; });

  // SOUND
  soundChips.forEach((b, i) => b.classList.toggle('on', i - 1 === sel));
  $('.sound-head').classList.toggle('master', sel < 0);
  $('#trackButtons').style.visibility = sel < 0 ? 'hidden' : '';
  if (sel >= 0) {
    const vt = $('#voiceType');
    const want = `${S.voice(sel)}:${S.character(sel)}`;
    if (vt.dataset.shown !== want) {
      vt.textContent = '';
      for (let v = 0; v < 6; v++) {
        const o = el('option', '', v === Voice.Sample ? 'SAMPLE…' : 'TYPE: ' + E.str(x.ms_drum_name(v, S.character(sel))));
        o.value = v;
        vt.append(o);
      }
      vt.dataset.shown = want;
    }
    vt.value = String(S.voice(sel));
    characterButtons.forEach((b, c) => {
      b.classList.toggle('on', c === S.character(sel));
      b.disabled = S.voice(sel) === Voice.Sample;
    });
    $('#mute').classList.toggle('on', S.mute(sel));
    $('#solo').classList.toggle('on', S.solo(sel));
  }
  sectionButtons.forEach((b, i) => b.classList.toggle('on', i === ui.section));
  buildSliders();
  for (const { s, m, val } of sliders) {
    const v = m.get();
    val.textContent = m.format(v);
    if (m.toggle) {
      s.querySelectorAll('.seg span').forEach((sp, i) => sp.classList.toggle('on', (v > 0.5) === (i === 1)));
    } else {
      s.querySelector('.fill').style.width = (v * 100).toFixed(2) + '%';
      s.querySelector('.knob').style.left = (v * 100).toFixed(2) + '%';
    }
  }
  const snapBtn = document.querySelector('[data-role=snap]');
  if (snapBtn) snapBtn.textContent = ui.snap ? 'PITCH: SNAP TO NOTES' : 'PITCH: FREE';

  $('#exportTake').disabled = x.ms_rec_length() <= 0 || ui.recording;
  $('#exportTake').textContent = x.ms_rec_length() > 0
    ? `LAST RECORDING (${(x.ms_rec_length() / ctx.sampleRate).toFixed(1)} s)`
    : 'LAST RECORDING (NONE YET)';
}

// ---- signal box: annotated harmonic lattice (as on the desktop) ----------------

const fftRe = new Float32Array(SCOPE), fftIm = new Float32Array(SCOPE), mags = new Float32Array(SCOPE / 2);
const hann = Float32Array.from({ length: SCOPE }, (_, i) => 0.5 - 0.5 * Math.cos(2 * Math.PI * i / SCOPE));

function fft(re, im, n) {
  for (let i = 1, j = 0; i < n; i++) {
    let bit = n >> 1;
    for (; j & bit; bit >>= 1) j ^= bit;
    j ^= bit;
    if (i < j) { [re[i], re[j]] = [re[j], re[i]]; [im[i], im[j]] = [im[j], im[i]]; }
  }
  for (let len = 2; len <= n; len <<= 1) {
    const ang = -2 * Math.PI / len, wr = Math.cos(ang), wi = Math.sin(ang);
    for (let i = 0; i < n; i += len) {
      let cr = 1, ci = 0;
      for (let k = 0; k < len / 2; k++) {
        const a = i + k, b = a + len / 2;
        const vr = re[b] * cr - im[b] * ci, vi = re[b] * ci + im[b] * cr;
        re[b] = re[a] - vr; im[b] = im[a] - vi; re[a] += vr; im[a] += vi;
        const nx = cr * wr - ci * wi; ci = cr * wi + ci * wr; cr = nx;
      }
    }
  }
}

const canvas = $('#signal');
const g = canvas.getContext('2d');

function drawSignal() {
  if (ui.view !== 'seq' || !ui.scope || canvas.offsetParent === null) return;
  const dpr = window.devicePixelRatio || 1;
  const W = canvas.clientWidth, H = canvas.clientHeight;
  if (canvas.width !== Math.round(W * dpr) || canvas.height !== Math.round(H * dpr)) {
    canvas.width = Math.round(W * dpr); canvas.height = Math.round(H * dpr);
  }
  g.setTransform(dpr, 0, 0, dpr, 0, 0);
  g.clearRect(0, 0, W, H);

  const t = seqTrack();
  const sr = ctx.sampleRate;
  const buf = ui.scope.subarray(t * SCOPE, (t + 1) * SCOPE);
  const css = getComputedStyle(document.documentElement);
  const INK = '#fff', RED = '#ff2b2b', BLUE = '#2f6bff', DIM = '#8c8c8c', FAINT = '#262626';

  let peak = 0, sum = 0;
  for (let i = SCOPE - 256; i < SCOPE; i++) { peak = Math.max(peak, Math.abs(buf[i])); sum += buf[i] * buf[i]; }
  const rms = Math.sqrt(sum / 256);
  const live = rms > 0.002;

  for (let i = 0; i < SCOPE; i++) { fftRe[i] = buf[i] * hann[i]; fftIm[i] = 0; }
  fft(fftRe, fftIm, SCOPE);
  let magSum = 0, freqSum = 0;
  for (let b = 1; b < SCOPE / 2; b++) {
    const m = Math.hypot(fftRe[b], fftIm[b]) / (SCOPE / 4);
    mags[b] = m; magSum += m; freqSum += m * b * sr / SCOPE;
  }
  const centroid = magSum > 1e-4 ? freqSum / magSum : 0;

  const x0 = 1, w = W - 2, top = 7, base = H - 9;
  const xOf = (hz) => x0 + (Math.log(hz / 20) / Math.log(1000)) * w;
  const hzAt = (px) => 20 * Math.pow(1000, (px - x0) / w);
  const levelAt = (lo, hi) => {
    const a = clamp(Math.floor(lo * SCOPE / sr), 1, SCOPE / 2 - 1), b = clamp(Math.ceil(hi * SCOPE / sr), a, SCOPE / 2 - 1);
    let m = 0;
    for (let i = a; i <= b; i++) m = Math.max(m, mags[i]);
    return clamp((20 * Math.log10(m + 1e-9) + 72) / 72, 0, 1);
  };

  g.font = '7px ' + css.getPropertyValue('--mono');
  g.textBaseline = 'top';
  for (let oct = 1; oct <= 9; oct++) {
    const px = xOf(16.352 * Math.pow(2, oct));
    g.fillStyle = FAINT; g.fillRect(px, top, 1, H - top);
    g.fillStyle = DIM; g.fillText('C' + oct, px + 2, base + 1);
  }
  if (live) {
    for (let px = x0; px < x0 + w; px++) {
      const v = levelAt(hzAt(px), hzAt(px + 1));
      if (v < 0.15) continue;
      g.fillStyle = `rgba(255,255,255,${0.2 + v * 0.55})`;
      const bh = v * (base - top - 6);
      g.fillRect(px, base - bh, 1, bh);
    }
  }
  const f0 = x.ms_fundamental_hz(t);
  if (f0 > 0) {
    for (let n = 1; n <= 32 && f0 * n < 20000; n++) {
      const f = f0 * n, px = xOf(f);
      const lit = live && levelAt(f * 0.985, f * 1.015) > 0.55;
      g.fillStyle = n === 1 ? RED : lit ? INK : '#444';
      for (let y = top; y < base; y += 2) g.fillRect(px, y, 1, 1);
    }
  }
  if (live && centroid > 20) { g.fillStyle = BLUE; g.fillRect(xOf(centroid), top, 1, base - top); }

  // the current sample's 16 bits along the top
  const bits = Math.round(clamp(buf[SCOPE - 1], -1, 1) * 32767) & 0xffff;
  const cw = W / 16;
  for (let b = 0; b < 16; b++) {
    const on = (bits >> (15 - b)) & 1;
    g.fillStyle = on ? (b === 0 ? RED : INK) : FAINT;
    g.fillRect(b * cw, 3, cw - 1, 2);
  }

  const tag = (text, tx, ty, fill) => {
    g.font = 'bold 8px ' + css.getPropertyValue('--mono');
    const tw = g.measureText(text).width + 5;
    g.fillStyle = fill; g.fillRect(tx, ty, tw, 10);
    g.fillStyle = '#000'; g.fillText(text, tx + 2.5, ty + 1);
    return tw;
  };

  if (f0 > 0) {
    let pitchText = '';
    for (let i = 0; i < E.numVoiceParams; i++) if (x.ms_voice_param_is_pitch(t, i)) { pitchText = E.str(x.ms_format_voice_param(t, i, S.voiceParam(t, i))); break; }
    const bin = clamp(Math.round(f0 * SCOPE / sr), 1, SCOPE / 2 - 1);
    const phase = live ? Math.round((Math.atan2(fftIm[bin], fftRe[bin]) + Math.PI) * 180 / Math.PI) + '°' : '--';
    const text = `${pitchText}  ${phase}`;
    g.font = 'bold 8px ' + css.getPropertyValue('--mono');
    const tw = g.measureText(text).width + 5;
    const fx = xOf(f0);
    const tx = fx + 2 + tw < W - 60 ? fx + 2 : Math.max(0, fx - 2 - tw);
    tag(text, tx, top + 1, RED);
    // phase portrait: each sample against the one a quarter cycle earlier
    const pr = { x: tx, y: top + 13, s: 24 };
    if (pr.y + pr.s < H) {
      g.fillStyle = '#000'; g.fillRect(pr.x, pr.y, pr.s, pr.s);
      g.strokeStyle = '#444'; g.strokeRect(pr.x + 0.5, pr.y + 0.5, pr.s - 1, pr.s - 1);
      const tau = clamp(Math.round(sr / f0 / 4), 1, 1000);
      let maxAbs = 1e-4;
      for (let i = SCOPE - 1200; i < SCOPE; i++) maxAbs = Math.max(maxAbs, Math.abs(buf[i]));
      const sc = pr.s * 0.45 / maxAbs;
      g.fillStyle = INK;
      for (let i = SCOPE - 400; i < SCOPE; i++) {
        const a = buf[i], b = buf[i - tau];
        if (Math.abs(a) + Math.abs(b) < 0.003) continue;
        g.fillRect(pr.x + pr.s / 2 + a * sc, pr.y + pr.s / 2 - b * sc, 1, 1);
      }
    }
  }
  if (live && centroid > 20) tag(`CENT ${Math.round(centroid)} Hz`, Math.min(W - 150, xOf(centroid) + 2), top + 24, BLUE);

  const dbText = (v) => (v < 1e-5 ? '-INF' : x.ms_gain_db(v).toFixed(1));
  const readouts = [`PK ${dbText(peak)}`, `RMS ${dbText(rms)}`, `CRST ${rms > 1e-4 ? (peak / rms).toFixed(1) : '--'}`];
  g.font = 'bold 8px ' + css.getPropertyValue('--mono');
  readouts.forEach((r, i) => {
    const tw = g.measureText(r).width + 5;
    tag(r, W - tw - 1, top + 1 + i * 12, i === 0 && peak > 1 ? RED : INK);
  });

  g.strokeStyle = '#5a5a5a';
  g.strokeRect(0.5, 0.5, W - 1, H - 1);
  g.fillStyle = INK;
  g.fillRect(0, 0, W, 3);
}

// ============================================================================
// Worklet status

function onStatus(m) {
  ui.playing = !!m.playing;
  ui.recording = !!m.recording;
  ui.recordSeconds = m.recordSeconds;
  ui.step = m.step;
  ui.masterPeak = m.masterPeak;
  ui.scope = m.scope;
  for (let t = 0; t < NT; t++) {
    if (m.hits[t] !== ui.hits[t]) flash(t);
    ui.hits[t] = m.hits[t];
  }
  // The engine switches to a queued pattern by itself at the end of a bar.
  const queued = S.queued();
  if (queued >= 0 && m.queued < 0 && m.pattern === queued) x.ms_ui_sync_pattern(queued, -1);
  refresh();
  drawSignal();
}

// ============================================================================
// Wiring

function showView(v) {
  ui.view = v;
  document.querySelectorAll('.view').forEach((s) => s.classList.toggle('on', s.id === 'view-' + v));
  document.querySelectorAll('.tabs button').forEach((b) => b.classList.toggle('on', b.dataset.view === v));
  scheduleRefresh();
}

function dragValue(target, get, set, perPixel, onDouble) {
  let id = null, startY = 0, startV = 0, lastTap = 0, moved = false;
  target.addEventListener('pointerdown', (e) => { id = e.pointerId; startY = e.clientY; startV = get(); moved = false; target.setPointerCapture(id); });
  target.addEventListener('pointermove', (e) => {
    if (e.pointerId !== id) return;
    const dy = startY - e.clientY;
    if (Math.abs(dy) > 3) moved = true;
    if (moved) set(startV + dy * perPixel);
  });
  target.addEventListener('pointerup', (e) => {
    if (e.pointerId !== id) return;
    id = null;
    if (!moved) {
      const now = performance.now();
      if (now - lastTap < 350) { onDouble(); lastTap = 0; } else lastTap = now;
    }
  });
}

function wire() {
  $('#play').addEventListener('click', togglePlay);
  $('#stop').addEventListener('click', stop);
  $('#rec').addEventListener('click', toggleRecord);

  document.querySelectorAll('.pat').forEach((b) => {
    const p = +b.dataset.pattern;
    let timer = 0, long = false;
    b.addEventListener('pointerdown', () => { long = false; timer = setTimeout(() => { long = true; copyPatternTo(p); }, 550); });
    b.addEventListener('pointerup', () => { clearTimeout(timer); if (!long) selectPattern(p); });
    b.addEventListener('pointerleave', () => clearTimeout(timer));
    b.addEventListener('contextmenu', (e) => e.preventDefault());
  });

  dragValue($('#bpm'), S.bpm, (v) => send(Cmd.SetBpm, 0, 0, Math.round(clamp(v, 40, 300) * 10) / 10), 0.25, () => {
    const v = parseFloat(prompt('Tempo (BPM)', S.bpm().toFixed(1)));
    if (isFinite(v)) send(Cmd.SetBpm, 0, 0, clamp(v, 40, 300));
  });
  dragValue($('#swing'), S.swing, (v) => send(Cmd.SetSwing, 0, 0, Math.round(clamp(v, 0, 1) * 100) / 100), 0.005, () => send(Cmd.SetSwing, 0, 0, 0));

  $('#copyDrum').addEventListener('click', () => copySequence(false));
  $('#copyAll').addEventListener('click', () => copySequence(true));
  $('#paste').addEventListener('click', pasteSequence);
  $('#random').addEventListener('click', () => randomizeSequence(seqTrack()));
  $('#clear').addEventListener('click', clearPattern);
  $('#undo').addEventListener('click', undo);

  document.querySelectorAll('.tabs button').forEach((b) => b.addEventListener('click', () => showView(b.dataset.view)));

  $('#exportTake').addEventListener('click', () => exportWav(0));
  document.querySelectorAll('[data-loops]').forEach((b) => b.addEventListener('click', () => exportWav(+b.dataset.loops)));
  $('#save').addEventListener('click', saveProjectFile);
  $('#open').addEventListener('click', () => { $('#projectInput').value = ''; $('#projectInput').click(); });
  $('#newProject').addEventListener('click', () => {
    if (confirm('Start again from the starter project? The current one is replaced (save it first if you want to keep it).')) {
      loadProject(null);
      setStatus('NEW PROJECT');
    }
  });
  $('#sampleInput').addEventListener('change', (e) => onSampleFile(e.target.files[0]));
  $('#projectInput').addEventListener('change', (e) => onProjectFile(e.target.files[0]));

  // Computer keyboard, when the page is used on a laptop.
  window.addEventListener('keydown', (e) => {
    if (e.target.tagName === 'SELECT' || e.target.tagName === 'INPUT' || e.repeat) return;
    const k = e.key.toLowerCase();
    if ((e.ctrlKey || e.metaKey) && k === 'z') { undo(); e.preventDefault(); return; }
    if ((e.ctrlKey || e.metaKey) && k === 'c') { copySequence(e.shiftKey); e.preventDefault(); return; }
    if ((e.ctrlKey || e.metaKey) && k === 'v') { pasteSequence(); e.preventDefault(); return; }
    if (e.ctrlKey || e.metaKey || e.altKey) return;
    if (k === ' ') { togglePlay(); e.preventDefault(); return; }
    if (k === 'escape') { stop(); return; }
    if (k === 'r') { toggleRecord(); return; }
    const t = PAD_KEYS.findIndex((p) => p.includes(k));
    if (t >= 0) { hitPad(t, 1); select(t); }
  });

  document.addEventListener('visibilitychange', () => { if (!document.hidden && ctx) ctx.resume(); });
  window.addEventListener('resize', scheduleRefresh);
}

// ============================================================================
// Start-up

async function loadHr16() {
  const jobs = [];
  for (let n = 1; n <= 56; n++) {
    jobs.push(fetch(`hr16/Alesis_HR16A_${pad2(n)}.wav`).then((r) => (r.ok ? r.arrayBuffer() : null)).then((b) => {
      const w = b && decodeWavMono(b);
      return w ? { n, samples: w.samples, sampleRate: w.sampleRate } : null;
    }).catch(() => null));
  }
  hr16Files = (await Promise.all(jobs)).filter(Boolean);
  for (const f of hr16Files) E.setHr16(f.n, f.samples, f.sampleRate);
}

async function prepare() {
  const startText = $('#startText');
  try {
    wasmBytes = await (await fetch('engine.wasm')).arrayBuffer();
    E = instantiate(new WebAssembly.Module(wasmBytes));
    x = E.x;
    buildSeq();
    buildPads();
    buildSound();
    wire();
    showView('seq');
    await loadHr16();
    startText.textContent = 'TAP TO START';
  } catch (err) {
    startText.textContent = 'COULD NOT LOAD: ' + err.message;
    throw err;
  }
}

async function start() {
  const AC = window.AudioContext || window.webkitAudioContext;
  ctx = new AC({ latencyHint: 'interactive' });
  // Play through the iPhone's silent switch, like a music app.
  try { if (navigator.audioSession) navigator.audioSession.type = 'playback'; } catch { /* older browsers */ }
  await ctx.audioWorklet.addModule('worklet.js');
  node = new AudioWorkletNode(ctx, 'modular-synth', {
    numberOfInputs: 0, numberOfOutputs: 1, outputChannelCount: [2],
    processorOptions: { wasm: wasmBytes },
  });
  node.connect(ctx.destination);
  node.port.onmessage = (e) => {
    if (e.data.type === 'status') onStatus(e.data);
    else if (e.data.type === 'take') receiveTake(e.data);
  };
  node.port.postMessage({ type: 'hr16', files: hr16Files });
  restoreProject();
  await ctx.resume();
  $('#start').classList.add('gone');
  scheduleRefresh();
}

const ready = prepare();
$('#startButton').addEventListener('click', async () => {
  await ready;
  if (ctx) return;
  $('#startText').textContent = 'STARTING…';
  try { await start(); } catch (err) { $('#startText').textContent = 'AUDIO FAILED: ' + err.message; ctx = null; }
});

if ('serviceWorker' in navigator && location.protocol === 'https:') navigator.serviceWorker.register('sw.js').catch(() => {});
