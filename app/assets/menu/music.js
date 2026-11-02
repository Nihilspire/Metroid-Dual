/* music.js - launcher music: "Zebes Overture", an original piece written as a tribute to the orchestral Metroid
 * arrangements (D Dorian, a driving eighth-note pulse under a heroic brass line). The melody and harmony are new; no
 * notes are taken from the game's soundtrack. Played by a small WebAudio synth orchestra: string pad, pizzicato
 * ostinato, bass, timpani and a filtered brass lead, all through a warm low-pass filter and a hall reverb.
 * Silent where WebAudio is missing (e.g. the Linux port). */
(function (root) {
  'use strict';
  var AC = root.AudioContext || root.webkitAudioContext;
  var M = { playing: false };
  if (!AC) { M.play = M.stop = M.resume = function () {}; root.Music = M; return; }

  var ctx = null, master = null, noiseBuf = null, timer = null, step = 0, nextT = 0;
  var BPM = 76, STEP = 60 / BPM / 4, LOOK = 0.2;          // 16th grid; the eighths give the 152 pulse
  var hz = function (m) { return 440 * Math.pow(2, (m - 69) / 12); };

  function noise() {
    if (noiseBuf) return noiseBuf;
    var len = ctx.sampleRate, b = ctx.createBuffer(1, len, ctx.sampleRate), d = b.getChannelData(0);
    for (var i = 0; i < len; i++) d[i] = Math.random() * 2 - 1;
    return (noiseBuf = b);
  }
  function env(g, t, len, vol, att, rel) {
    g.gain.value = 0;
    g.gain.setValueAtTime(0, t); g.gain.linearRampToValueAtTime(vol, t + att);
    g.gain.setValueAtTime(vol, Math.max(t + att, t + len - rel)); g.gain.linearRampToValueAtTime(0, t + len);
  }

  // ---- instruments
  function strings(midis, t, len, vol) {        // three detuned saws per note, low-passed, slow bow
    var f = ctx.createBiquadFilter(), g = ctx.createGain();
    f.type = 'lowpass'; f.frequency.value = 1100; f.Q.value = 0.3;
    env(g, t, len, vol, 0.6, 0.7); f.connect(g); g.connect(master);
    midis.forEach(function (m) { [-9, 0, 8].forEach(function (dt) {
      var o = ctx.createOscillator(); o.type = 'sawtooth'; o.frequency.value = hz(m); o.detune.value = dt;
      o.connect(f); o.start(t); o.stop(t + len + 0.05);
    }); });
  }
  function pizz(m, t, vol) {                     // plucked: triangle + sine, quick decay
    var g = ctx.createGain(), o1 = ctx.createOscillator(), o2 = ctx.createOscillator();
    o1.type = 'triangle'; o2.type = 'sine'; o1.frequency.value = hz(m); o2.frequency.value = hz(m) * 2;
    var g2 = ctx.createGain(); g2.gain.value = 0.25;
    g.gain.value = 0; g.gain.setValueAtTime(0, t); g.gain.linearRampToValueAtTime(vol, t + 0.004); g.gain.exponentialRampToValueAtTime(0.0008, t + 0.42);
    o1.connect(g); o2.connect(g2); g2.connect(g); g.connect(master);
    o1.start(t); o2.start(t); o1.stop(t + 0.45); o2.stop(t + 0.45);
  }
  function bass(m, t, len, vol) {
    var o = ctx.createOscillator(), s = ctx.createOscillator(), f = ctx.createBiquadFilter(), g = ctx.createGain(), sg = ctx.createGain();
    o.type = 'triangle'; s.type = 'sawtooth'; o.frequency.value = s.frequency.value = hz(m); sg.gain.value = 0.25;
    f.type = 'lowpass'; f.frequency.value = 420;
    env(g, t, len, vol, 0.01, 0.06);
    o.connect(g); s.connect(sg); sg.connect(f); f.connect(g); g.connect(master);
    o.start(t); s.start(t); o.stop(t + len + 0.02); s.stop(t + len + 0.02);
  }
  function timp(m, t, vol) {                     // timpani: sine with a small pitch drop + soft mallet noise
    var o = ctx.createOscillator(), g = ctx.createGain();
    o.type = 'sine'; o.frequency.setValueAtTime(hz(m) * 1.06, t); o.frequency.exponentialRampToValueAtTime(hz(m), t + 0.08);
    g.gain.value = 0; g.gain.setValueAtTime(0, t); g.gain.linearRampToValueAtTime(vol, t + 0.006); g.gain.exponentialRampToValueAtTime(0.001, t + 1.1);
    o.connect(g); g.connect(master); o.start(t); o.stop(t + 1.15);
    var n = ctx.createBufferSource(), nf = ctx.createBiquadFilter(), ng = ctx.createGain();
    n.buffer = noise(); nf.type = 'lowpass'; nf.frequency.value = 500;
    ng.gain.value = 0; ng.gain.setValueAtTime(vol * 0.5, t); ng.gain.exponentialRampToValueAtTime(0.001, t + 0.12);
    n.connect(nf); nf.connect(ng); ng.connect(master); n.start(t); n.stop(t + 0.15);
  }
  function brass(m, t, len, vol) {               // two saws, filter opens with the swell ("bwah"), delayed vibrato
    var f = ctx.createBiquadFilter(), g = ctx.createGain(), lfo = ctx.createOscillator(), lg = ctx.createGain();
    f.type = 'lowpass'; f.Q.value = 1.2;
    f.frequency.setValueAtTime(350, t); f.frequency.linearRampToValueAtTime(1500, t + 0.12); f.frequency.linearRampToValueAtTime(1000, t + Math.min(len, 0.6));
    env(g, t, len, vol, 0.07, 0.18);
    lfo.frequency.value = 5; lg.gain.setValueAtTime(0, t); lg.gain.linearRampToValueAtTime(hz(m) * 0.01, t + 0.45);
    lfo.connect(lg);
    [-5, 5].forEach(function (dt) {
      var o = ctx.createOscillator(); o.type = 'sawtooth'; o.frequency.value = hz(m); o.detune.value = dt;
      lg.connect(o.frequency); o.connect(f); o.start(t); o.stop(t + len + 0.05);
    });
    lfo.start(t); lfo.stop(t + len + 0.05);
    f.connect(g); g.connect(master);
  }
  function flute(m, t, len, vol) {               // soft sine with breath, for the answering line
    var o = ctx.createOscillator(), g = ctx.createGain(), lfo = ctx.createOscillator(), lg = ctx.createGain();
    o.type = 'sine'; o.frequency.value = hz(m); lfo.frequency.value = 5.2; lg.gain.value = hz(m) * 0.006;
    lfo.connect(lg); lg.connect(o.frequency); env(g, t, len, vol, 0.12, 0.2);
    o.connect(g); g.connect(master); o.start(t); lfo.start(t); o.stop(t + len + 0.05); lfo.stop(t + len + 0.05);
  }

  // ---- the score: 16 bars. A (bars 0-7): strings, ostinato, bass, timpani, a flute answer. B (8-15): the brass theme.
  var D = { Dm: [50, 53, 57, 62], C: [48, 52, 55, 60], Bb: [46, 50, 53, 58], F: [45, 48, 53, 57], G: [43, 47, 50, 55],
            A: [45, 49, 52, 57], Am: [45, 48, 52, 57], Dsus: [50, 55, 57, 62], D: [50, 54, 57, 62] };
  var ROOT = { Dm: 38, C: 36, Bb: 34, F: 41, G: 43, A: 33, Am: 33, Dsus: 38, D: 38 };
  var PROG = ['Dm', 'C', 'Bb', 'C', 'Dm', 'F', 'G', 'A', 'Bb', 'C', 'Dm', 'Am', 'Bb', 'C', 'Dsus', 'D'];
  var OST = [0, 2, 3, 2, 1, 2, 3, 2];          // pizzicato pattern over chord tones, eighths
  var THEME = [  // brass, B section: [bar, step, midi, steps]
    [8, 0, 74, 6], [8, 6, 77, 2], [8, 8, 76, 4], [8, 12, 72, 4],
    [9, 0, 76, 6], [9, 6, 79, 2], [9, 8, 77, 8],
    [10, 0, 81, 4], [10, 4, 79, 4], [10, 8, 77, 4], [10, 12, 76, 4],
    [11, 0, 76, 12], [11, 12, 72, 4],
    [12, 0, 74, 4], [12, 4, 77, 4], [12, 8, 82, 8],
    [13, 0, 81, 4], [13, 4, 79, 4], [13, 8, 76, 8],
    [14, 0, 79, 8], [14, 8, 77, 4], [14, 12, 76, 4],
    [15, 0, 78, 14]];
  var ANSWER = [ // flute, A section
    [1, 8, 69, 4], [1, 12, 67, 4], [2, 0, 65, 8], [3, 8, 64, 4], [3, 12, 67, 4],
    [5, 8, 72, 4], [5, 12, 69, 4], [6, 0, 71, 8], [7, 4, 73, 8], [7, 12, 76, 4]];
  var TOTAL = 16 * 16;

  function schedule(s, t) {
    var bar = Math.floor(s / 16) % 16, st = s % 16, ch = PROG[bar], tones = D[ch], inB = bar >= 8;
    if (st === 0) strings(tones.slice(0, 3).map(function (m) { return m + 12; }), t, STEP * 16.3, inB ? 0.014 : 0.018);
    if (st % 2 === 0) pizz(tones[OST[st >> 1]] + 12, t, inB ? 0.05 : 0.06);
    // bass: driving eighths, root with an octave lift on the off-beats of beat 2 and 4
    if (st % 2 === 0) bass(ROOT[ch] + ((st === 6 || st === 14) ? 12 : 0), t, STEP * 1.7, 0.11);
    if (st === 0) timp(ROOT[ch] + (ROOT[ch] < 36 ? 12 : 0), t, inB ? 0.2 : 0.14);
    if (st === 8 && (bar === 7 || bar === 15)) { timp(ROOT[ch] + 12, t, 0.12); timp(ROOT[ch] + 12, t + STEP * 2, 0.14); timp(ROOT[ch] + 12, t + STEP * 4, 0.17); }
    THEME.forEach(function (n) { if (n[0] === bar && n[1] === st) brass(n[2] - 12, t, n[3] * STEP * 0.97, 0.06); });
    ANSWER.forEach(function (n) { if (n[0] === bar && n[1] === st) flute(n[2] + 12, t, n[3] * STEP * 0.95, 0.045); });
  }
  function pump() {
    while (nextT < ctx.currentTime + LOOK) { schedule(step, nextT); step = (step + 1) % TOTAL; nextT += STEP; }
  }
  // master -> gentle low-pass -> dry + hall reverb
  function bus(c) {
    var input = c.createGain(), lp = c.createBiquadFilter(), dry = c.createGain(), rv = c.createConvolver(), wet = c.createGain();
    var len = Math.round(c.sampleRate * 2.6), ir = c.createBuffer(2, len, c.sampleRate);
    for (var k = 0; k < 2; k++) { var d = ir.getChannelData(k); for (var i = 0; i < len; i++) d[i] = (Math.random() * 2 - 1) * Math.pow(1 - i / len, 2.6); }
    rv.buffer = ir; lp.type = 'lowpass'; lp.frequency.value = 2400; lp.Q.value = 0.4;
    dry.gain.value = 0.85; wet.gain.value = 0.32;
    input.connect(lp); lp.connect(dry); dry.connect(c.destination); lp.connect(rv); rv.connect(wet); wet.connect(c.destination);
    return input;
  }

  M.play = function () {
    if (M.playing) return;
    try {
      if (!ctx) { ctx = new AC(); master = ctx.createGain(); master.connect(bus(ctx)); }
      if (ctx.state === 'suspended') ctx.resume();
      M.playing = true;
      master.gain.cancelScheduledValues(ctx.currentTime);
      master.gain.setValueAtTime(0, ctx.currentTime); master.gain.linearRampToValueAtTime(1, ctx.currentTime + 3);
      step = 0; nextT = ctx.currentTime + 0.08;
      pump(); timer = setInterval(pump, 40);
    } catch (e) { M.playing = false; }
  };
  M.stop = function () {
    if (!M.playing) return;
    M.playing = false;
    clearInterval(timer); timer = null;
    try {
      var t = ctx.currentTime;
      master.gain.cancelScheduledValues(t); master.gain.setValueAtTime(master.gain.value, t); master.gain.linearRampToValueAtTime(0, t + 0.4);
      setTimeout(function () { if (!M.playing && ctx) ctx.suspend(); }, 600);
    } catch (e) {}
  };
  M.resume = function () { if (M.playing && ctx && ctx.state === 'suspended') ctx.resume(); };
  /** Renders the song without playing it (tests / preview): resolves to an AudioBuffer. */
  M.render = function (seconds) {
    var Off = root.OfflineAudioContext; if (!Off) return Promise.reject('no OfflineAudioContext');
    var keep = [ctx, master, noiseBuf];
    ctx = new Off(1, Math.round(44100 * seconds), 44100); master = ctx.createGain(); master.gain.value = 1; master.connect(bus(ctx));
    noiseBuf = null;
    for (var s = 0, t = 0.05; t < seconds; s++, t += STEP) schedule(s % TOTAL, t);
    var p = ctx.startRendering();
    ctx = keep[0]; master = keep[1]; noiseBuf = keep[2];
    return p;
  };
  root.Music = M;
})(this);
