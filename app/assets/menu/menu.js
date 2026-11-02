/* menu.js - launcher and pause menu for Metroid Dual. Talks to MainActivity.Host (window.Host).
 * Drawn at NES resolution in the game's own font, styled after its title and password screens. */
(function () {
  'use strict';
  var G = NesGfx;
  var H0 = window.Host || {   // browser preview stub
    info: function () { return JSON.stringify({ version: 'dev', mode: 'launcher', rom: { present: false }, auto: false, settings: {}, slots: [], screens: {} }); },
    font: function () { return ''; }, binds: function () { return '[]'; }
  };
  var cv = document.getElementById('c'), ctx = cv.getContext('2d');
  var W = 256, H = 240, info = {}, page = 'main', sel = 0, scroll = 0, rows = [], tick = 0, toastMsg = '', toastT = 0;
  var thumbs = {}, binds = [], waiting = -1, confirm = null, devTime = 30;
  var C = { white: G.css(0x30), blue: G.css(0x21), dblue: G.css(0x01), navy: G.css(0x02), orange: G.css(0x27), red: G.css(0x16),
            grey: G.css(0x00), lgrey: G.css(0x10), green: G.css(0x2A), yellow: G.css(0x28) };
  var PALETTES = ['default', 'asqrealc', 'restored-wii-vc', 'wii-vc', 'rgb', 'yuv-v3', 'unsaturated-final', 'sony-cxa2025as-us',
                  'bmf-final2', 'bmf-final3', 'smooth-fbx', 'composite-direct-fbx', 'pvm-style-d93-fbx', 'ntsc-hardware-fbx',
                  'nes-classic-fbx-fs', 'nescap', 'wavebeam', 'digital-prime-fbx'];

  // launcher music (music.js): only on the launcher, never over the game or the pause menu
  var Mu = window.Music || { play: function () {}, stop: function () {}, resume: function () {} };
  function music() { if (info.mode === 'launcher' && (info.settings || {}).music !== false) Mu.play(); else Mu.stop(); }
  window.musicStop = function () { Mu.stop(); };
  window.musicStart = function () { refreshInfo(); music(); };

  function refreshInfo() {
    try { info = JSON.parse(H0.info()); } catch (e) { info = {}; }
    if (!G.hasFont()) { var f = G.b64(H0.font()); if (f.length === 0x400) G.setFont(f); }
  }
  window.refresh = function () { refreshInfo(); build(); draw(); music(); };
  window.show = function (mode) { refreshInfo(); page = 'main'; sel = 0; scroll = 0; confirm = null; thumbs = {}; build(); draw(); music(); };
  window.toast = function (m) { toastMsg = m; toastT = tick + 40; draw(); };
  window.onRebind = function () { waiting = -1; loadBinds(); build(); draw(); };

  function resize() {
    var r = innerWidth / Math.max(1, innerHeight);
    if (r >= 1) { H = 240; W = Math.max(256, Math.round(240 * r)); } else { W = 256; H = Math.round(256 / r); }
    cv.width = W; cv.height = H; ctx.imageSmoothingEnabled = false;
    draw();
  }
  addEventListener('resize', resize);

  // ------------------------------------------------------------------ pages
  function set(k, v) { H0.set(k, String(v)); info.settings[k] = v; }
  function cycle(k, list, dir) {
    var i = list.indexOf(String(info.settings[k] == null ? list[0] : info.settings[k])); i = (i + (dir || 1) + list.length) % list.length; set(k, list[i]);
  }
  function onoff(v) { return v ? 'ON' : 'OFF'; }
  function slotTime(t) {
    if (!t) return '- EMPTY -';
    var d = new Date(t), p = function (n) { return n < 10 ? '0' + n : '' + n; };
    return (d.getMonth() + 1) + '/' + d.getDate() + ' ' + p(d.getHours()) + ':' + p(d.getMinutes());
  }

  function build() {
    var s = info.settings || {}, rom = info.rom || {};
    rows = [];
    var opt = function (label, value, fn) { rows.push({ label: label, value: value, fn: fn }); };
    var sub = function (label, p) { rows.push({ label: label, value: '>', fn: function () { go(p); } }); };
    var backRow = function () { rows.push({ label: 'BACK', fn: back }); };
    if (page === 'main') {
      var pause = info.mode === 'pause';
      if (pause) {
        rows.push({ label: 'RESUME', fn: function () { H0.resume(); } });
        rows.push({ label: 'SAVE STATE', fn: function () { go('save'); } });
        rows.push({ label: 'LOAD STATE', fn: function () { go('load'); } });
        if (!info.linkShown) rows.push({ label: 'LINK SCREEN', fn: function () { H0.showLink(true); } });
        rows.push({ label: 'SETTINGS', fn: function () { go('settings'); } });
        rows.push({ label: 'RESET', fn: function () { ask('RESET THE GAME?', function () { H0.reset(); }); } });
        rows.push({ label: 'QUIT TO TITLE', fn: function () { H0.quit(); } });
      } else {
        if (rom.present && info.auto) rows.push({ label: 'CONTINUE', fn: function () { H0.start(true); } });
        if (rom.present) rows.push({ label: info.auto ? 'NEW GAME' : 'START', fn: function () {
          if (info.auto) ask('START OVER? CONTINUE DATA STAYS', function () { H0.start(false); }); else H0.start(false);
        } });
        if (rom.present) rows.push({ label: 'LOAD STATE', fn: function () { go('load'); } });
        else rows.push({ label: 'SELECT ROM', fn: function () { H0.pickRom(); } });
        rows.push({ label: 'SETTINGS', fn: function () { go('settings'); } });
        rows.push({ label: 'EXIT', fn: function () { H0.exit(); } });
      }
    } else if (page === 'save' || page === 'load') {
      (info.slots || []).forEach(function (sl) {
        rows.push({ label: 'SLOT ' + sl.n, value: slotTime(sl.time), slot: sl.n, time: sl.time, fn: function () {
          if (page === 'save') {
            var doSave = function () { var ok = H0.save(sl.n); delete thumbs[sl.n]; refreshInfo(); build(); toast(ok ? 'SAVED TO SLOT ' + sl.n : 'SAVE FAILED'); };
            if (sl.time) ask('OVERWRITE SLOT ' + sl.n + '?', doSave); else doSave();
          } else if (sl.time) { if (!H0.load(sl.n)) toast('LOAD FAILED'); }
        } });
      });
      backRow();
    } else if (page === 'settings') {             // categories
      sub('DISPLAY', 'display');
      sub('SCREENS', 'screens');
      if (!info.no3d) sub('3D VIEW', '3d');
      sub('CONTROLS', 'controls');
      sub('SOUND', 'sound');
      sub('GAMEPLAY', 'gameplay');
      sub('SYSTEM', 'system');
      backRow();
    } else if (page === 'display') {
      opt('SHAPE', { '4:3': '4:3', pixel: 'SQUARE PIXELS', stretch: 'STRETCH' }[s.aspect] || '4:3', function (d) { cycle('aspect', ['4:3', 'pixel', 'stretch'], d); });
      opt('INTEGER SCALE', onoff(s.integer), function () { set('integer', !s.integer); });
      opt('COLORS', String(s.palette || 'default').toUpperCase(), function (d) { cycle('palette', PALETTES, d); });
      if (!info.linux) opt('16:9 BORDERS', onoff(s.bezels), function () { set('bezels', !s.bezels); });
      opt('SHOW FPS', onoff(s.stats), function () { set('stats', !s.stats); });
      backRow();
    } else if (page === 'screens') {
      if (!info.linux) opt('SPLIT', { auto: 'AUTO', off: 'OFF', lr: 'SIDE BY SIDE', tb: 'TOP + BOTTOM' }[s.split] || 'AUTO', function (d) { cycle('split', ['auto', 'off', 'lr', 'tb'], d); });
      if (!info.linux && (s.split === 'lr' || s.split === 'tb')) opt('HINGE GAP', (s.gap || 0) + 'DP', function (d) { cycle('gap', ['0', '16', '26', '34', '40'], d); });
      opt('SWAP SCREENS', onoff(s.swap), function () { set('swap', !s.swap); });
      if (!info.linux) opt('LINK SIDE PANEL', onoff(s.side), function () { set('side', !s.side); });
      if (!info.linux) opt('SECOND DISPLAY', onoff(s.useExternal), function () { set('useExternal', !s.useExternal); });
      backRow();
    } else if (page === '3d') {
      opt('3D VIEW', onoff(s.dev3d), function () { set('dev3d', !s.dev3d); });
      opt('CAMERA', String(s.cam3d || 'tilt').toUpperCase(), function (d) { cycle('cam3d', ['tilt', 'diorama', 'side', 'low', 'far'], d); });
      opt('BLOCK DEPTH', String(s.depth3d || 16), function (d) {
        var v = (s.depth3d || 16) + 4 * (d || 1); if (v > 48) v = 4; if (v < 4) v = 48; set('depth3d', v); });
      opt('BACK WALL', { near: 'NEAR', far: 'DEEP', off: 'OFF' }[s.back3d] || 'NEAR', function (d) { cycle('back3d', ['near', 'far', 'off'], d); });
      opt('BACKDROP', onoff(s.cave3d), function () { set('cave3d', !s.cave3d); });
      opt('LIGHT ON SAMUS', onoff(s.light3d), function () { set('light3d', !s.light3d); });
      if (!info.linux) opt('3D BORDERS', onoff(s.border3d !== false), function () { set('border3d', !(s.border3d !== false)); });
      backRow();
    } else if (page === 'controls') {
      if (!info.linux) opt('TOUCH PAD', String(s.touch || 'auto').toUpperCase(), function (d) { cycle('touch', ['auto', 'on', 'off'], d); });
      if (!info.linux) opt('PAD OPACITY', (s.padOpacity || 70) + '%', function (d) {
        var v = (s.padOpacity || 70) + 10 * (d || 1); if (v > 100) v = 20; if (v < 20) v = 100; set('padOpacity', v); });
      rows.push({ label: 'CONTROLLER BUTTONS', value: '>', fn: function () { loadBinds(); go('buttons'); } });
      backRow();
    } else if (page === 'sound') {
      opt('GAME SOUND', onoff(s.audio), function () { set('audio', !s.audio); });
      if (window.Music && window.AudioContext) opt('MENU MUSIC', onoff(s.music !== false), function () { set('music', !(s.music !== false)); music(); });
      backRow();
    } else if (page === 'gameplay') {
      opt('NO SLOWDOWN', onoff(s.overclock !== false), function () { set('overclock', !(s.overclock !== false)); });
      if (s.devUnlock) sub('PLAYTEST CODES', 'dev');
      backRow();
    } else if (page === 'system') {
      rows.push({ label: rom.present ? 'CHANGE ROM' : 'SELECT ROM', value: '>', fn: function () { H0.pickRom(); } });
      opt('SKIP LAUNCHER', onoff(s.skipLauncher), function () { set('skipLauncher', !s.skipLauncher); });
      rows.push({ head: 'METROID DUAL ' + (info.version || ''), note: true });
      backRow();
    } else if (page === 'dev') {
      rows.push({ head: 'CHEATS FOR TESTING', note: true });
      opt('INFINITE ENERGY', onoff(s.codeEnergy), function () { set('codeEnergy', !s.codeEnergy); });
      opt('INFINITE MISSILES', onoff(s.codeMissiles), function () { set('codeMissiles', !s.codeMissiles); });
      var act = function (label, what) { rows.push({ label: label, value: 'GO', fn: function () { toast(H0.dev(what)); } }); };
      act('GIVE ALL ITEMS', 'items');
      act('ICE / WAVE BEAM', 'ice');
      act('REFILL ENERGY + MISSILES', 'refill');
      act('KRAID DEFEATED', 'kraid');
      act('RIDLEY DEFEATED', 'ridley');
      opt('SET PLAY TIME', (devTime / 60 | 0) + ':' + ('0' + devTime % 60).slice(-2), function (d) {
        var l = [30, 58, 60, 147, 149, 324, 326, 1799, 1803], i = l.indexOf(devTime); devTime = l[(i + (d < 0 ? l.length - 1 : 1)) % l.length];
        toast(H0.dev('time:' + devTime)); });
      act('START ESCAPE TIMER', 'escape');
      act('MISSION REPORT ON/OFF', 'report');
      backRow();
    } else if (page === 'buttons') {
      binds.forEach(function (b, i) {
        rows.push({ label: b.name, value: waiting === i ? 'PRESS A BUTTON' : (b.keys.length ? b.keys.join(' ') : '-'), fn: function () {
          waiting = i; H0.rebind(i); build();
        } });
      });
      rows.push({ label: 'RESET TO DEFAULT', fn: function () { H0.resetBinds(); loadBinds(); build(); toast('DEFAULTS RESTORED'); } });
      rows.push({ label: 'BACK', fn: back });
    }
    var first = firstSelectable(); if (sel < first || sel >= rows.length || rows[sel].head) sel = first;
  }
  function loadBinds() { try { binds = JSON.parse(H0.binds()); } catch (e) { binds = []; } }
  function firstSelectable() { for (var i = 0; i < rows.length; i++) if (!rows[i].head) return i; return 0; }
  var stack = [];
  function go(p) { stack.push([page, sel]); page = p; sel = 0; scroll = 0; refreshInfo(); build(); }
  function back() {
    if (waiting >= 0) { waiting = -1; H0.cancelRebind(); build(); return; }
    if (confirm) { confirm = null; return; }
    if (!stack.length) { if (info.mode === 'pause') H0.resume(); return; }
    var s = stack.pop(); page = s[0]; sel = s[1]; refreshInfo(); build();
  }
  function ask(q, fn) { confirm = { q: q, fn: fn, sel: 1 }; }

  // ------------------------------------------------------------------ input
  // Konami code on the launcher's main screen (up up down down left right left right B A) shows or hides the playtest
  // codes. Hidden, they are also switched off.
  var KONAMI = ['up', 'up', 'down', 'down', 'left', 'right', 'left', 'right', 'back', 'ok'], kon = 0;
  function konami(k) {
    if (page !== 'main' || info.mode === 'pause' || confirm) { kon = 0; return false; }
    if (k === KONAMI[kon]) kon++; else kon = k === 'up' ? (kon === 2 ? 2 : 1) : 0;
    if (kon < KONAMI.length) return false;
    kon = 0;
    var on = !(info.settings || {}).devUnlock;
    set('devUnlock', on);
    if (!on) { set('codeEnergy', false); set('codeMissiles', false); }
    toast(on ? 'PLAYTEST CODES UNLOCKED' : 'PLAYTEST CODES HIDDEN');
    return true;
  }
  window.pad = function (k) {
    Mu.resume();
    if (konami(k)) { refreshInfo(); build(); draw(); return; }      // the final A doesn't also pick a menu item
    if (confirm) {
      if (k === 'left' || k === 'right' || k === 'up' || k === 'down') confirm.sel ^= 1;
      else if (k === 'ok') { var c = confirm; confirm = null; if (c.sel === 0) c.fn(); }
      else if (k === 'back') confirm = null;
      draw(); return;
    }
    if (waiting >= 0) { if (k === 'back') back(); draw(); return; }
    if ((page === 'save' || page === 'load') && (k === 'up' || k === 'down' || k === 'left' || k === 'right')) {
      var n = rows.length - 1;   // slots, then BACK
      if (sel >= n) { if (k === 'up') sel = n - 1; else if (k === 'down') sel = 0; }
      else if (k === 'left' || k === 'right') sel = (sel & ~1) | ((sel & 1) ^ 1);
      else if (k === 'down') sel = sel + 2 >= n ? n : sel + 2;
      else sel = sel - 2 < 0 ? n : sel - 2;
    } else if (k === 'up' || k === 'down') {
      var d = k === 'up' ? -1 : 1, i = sel;
      do { i = (i + d + rows.length) % rows.length; } while (rows[i].head && i !== sel);
      sel = i;
    } else if (k === 'ok') { var r = rows[sel]; if (r && r.fn) r.fn(1); }
    else if (k === 'left' || k === 'right') { var r2 = rows[sel]; if (r2 && r2.value && r2.value !== '>' && r2.value !== 'GO' && r2.fn && page !== 'main') r2.fn(k === 'left' ? -1 : 1); }
    else if (k === 'back') back();
    refreshInfo(); build(); draw();
  };

  var hit = [];
  cv.addEventListener('pointerdown', function (e) {
    Mu.resume();
    var x = e.clientX * W / innerWidth, y = e.clientY * H / innerHeight;
    for (var i = hit.length - 1; i >= 0; i--) {
      var b = hit[i];
      if (x >= b.x && x < b.x + b.w && y >= b.y && y < b.y + b.h) { b.fn(x - b.x < b.w / 3 ? -1 : 1); refreshInfo(); build(); draw(); return; }
    }
  });
  var startY = null;
  cv.addEventListener('touchstart', function (e) { startY = e.touches[0].clientY; }, { passive: true });
  cv.addEventListener('touchmove', function (e) {
    if (startY == null) return;
    var dy = (e.touches[0].clientY - startY) * H / innerHeight;
    if (Math.abs(dy) > 12) { scroll = Math.max(0, scroll - Math.sign(dy)); startY = e.touches[0].clientY; draw(); }
  }, { passive: true });

  // ------------------------------------------------------------------ drawing
  var stars = [];
  for (var s = 0; s < 80; s++) stars.push([Math.random(), Math.random(), Math.floor(Math.random() * 4), Math.floor(Math.random() * 40)]);

  function draw() {
    hit = [];
    ctx.fillStyle = '#000'; ctx.fillRect(0, 0, W, H);
    stars.forEach(function (st) {
      if ((tick + st[3]) % 40 < 30) { ctx.fillStyle = [C.blue, C.white, C.orange, C.dblue][st[2]]; ctx.fillRect(Math.floor(st[0] * W), Math.floor(st[1] * H), 1, 1); }
    });
    var cx = W / 2, y = 14;
    if (page === 'main' && info.mode !== 'pause') {
      G.textC(ctx, 'METROID', cx, y + 4, C.orange, 3);
      G.textC(ctx, 'DUAL', cx, y + 32, C.blue, 2);
      y += 56;
      var rom = info.rom || {};
      var romLine = !rom.present ? 'NO ROM SELECTED' : rom.ok ? 'METROID (USA) - OK' : 'UNKNOWN ROM ' + (rom.crc || '').toUpperCase();
      G.textC(ctx, romLine, cx, y, !rom.present ? C.red : rom.ok ? C.green : C.yellow);
      y += 16;
    } else {
      var title = { main: 'PAUSE', save: 'SAVE STATE', load: 'LOAD STATE', settings: 'SETTINGS', display: 'DISPLAY', screens: 'SCREENS',
                    '3d': '3D VIEW', controls: 'CONTROLS', buttons: 'CONTROLLER', sound: 'SOUND', gameplay: 'GAMEPLAY', system: 'SYSTEM',
                    dev: 'PLAYTEST CODES' }[page] || '';
      G.textC(ctx, title, cx, y, C.orange, 2);
      y += 26;
    }
    if (page === 'save' || page === 'load') { slotGrid(y); finish(); return; }
    var centered = page === 'main', rowH = centered ? 16 : 12;
    if (page === 'settings') rowH = 15;
    var visible = Math.max(1, Math.floor((H - y - 16) / rowH));
    if (centered || page === 'settings') {        // short lists: centre the block in the space left (footer below)
      var foot = page === 'main' && info.mode !== 'pause' ? 26 : 10;
      y = Math.max(y, Math.round(y + (H - foot - y - rows.length * rowH) / 2));
    }
    if (sel < scroll) scroll = sel;
    if (sel >= scroll + visible) scroll = sel - visible + 1;
    scroll = Math.max(0, Math.min(scroll, Math.max(0, rows.length - visible)));
    var listW = Math.min(W - 16, 232), x0 = Math.round(cx - listW / 2);
    for (var i = scroll; i < rows.length && i < scroll + visible; i++) {
      var r = rows[i], yy = y + (i - scroll) * rowH, on = i === sel;
      if (r.note) { G.textC(ctx, r.head, cx, yy + 2, C.grey); continue; }
      if (r.head) { G.text(ctx, r.head, x0, yy + 2, C.blue); ctx.fillStyle = C.navy; ctx.fillRect(x0 + r.head.length * 8 + 4, yy + 5, listW - r.head.length * 8 - 4, 1); continue; }
      if (centered) {
        if (on) { var bw = r.label.length * 8 + 32; ctx.fillStyle = '#0c1450'; ctx.fillRect(Math.round(cx - bw / 2), yy - 2, bw, rowH - 3); }
        G.textC(ctx, r.label, cx, yy + 2, on ? C.white : C.lgrey);
        if (on && (tick >> 3) % 2 === 0) G.text(ctx, '>', Math.round(cx - r.label.length * 4) - 12, yy + 2, C.orange);
      } else {
        if (on) { ctx.fillStyle = '#0c1450'; ctx.fillRect(x0 - 2, yy, listW + 4, rowH - 1); }
        G.text(ctx, r.label, x0 + 8, yy + 2, on ? C.white : C.lgrey);
        if (on) G.text(ctx, '>', x0, yy + 2, C.orange);
        if (r.value) {
          var v = r.value, maxc = Math.floor((listW - r.label.length * 8 - 24) / 8);
          if (v.length > maxc) v = v.substr(0, Math.max(1, maxc));
          G.textR(ctx, v, x0 + listW, yy + 2, waiting === i ? ((tick >> 3) % 2 ? C.orange : C.white) : C.blue);
        }
      }
      (function (idx, rr) { hit.push({ x: x0 - 4, y: yy, w: listW + 8, h: rowH, fn: function (d) { sel = idx; if (rr.fn) rr.fn(d); } }); })(i, r);
    }
    if (scroll > 0) G.textC(ctx, '-', cx, y - 10, C.grey);
    if (scroll + visible < rows.length) G.textC(ctx, '-', cx, H - 14, C.grey);
    finish();
  }
  function finish() {
    var cx = W / 2;
    if (page === 'main' && info.mode !== 'pause') {
      var sc = info.screens || {};
      G.text(ctx, 'V' + (info.version || ''), 4, H - 10, C.grey);
      var how = { hinge: 'DUAL SCREEN', split: 'SPLIT', external: '2ND DISPLAY', side: 'SIDE PANEL', single: 'ONE SCREEN' }[sc.how] || '';
      G.textR(ctx, how + (sc.source ? ' (' + String(sc.source).toUpperCase() + ')' : ''), W - 4, H - 10, C.grey);
      if (sc.size) G.textR(ctx, String(sc.model || '').toUpperCase().substr(0, 14) + ' ' + sc.size, W - 4, H - 20, C.grey);
    }
    if (confirm) drawConfirm();
    if (toastT > tick) {
      var tw = toastMsg.length * 8 + 12;
      ctx.fillStyle = C.navy; ctx.fillRect(cx - tw / 2, H - 30, tw, 14);
      G.textC(ctx, toastMsg, cx, H - 27, C.white);
    }
  }

  // SAVE / LOAD: 2x2 cards with the slot's screenshot, then BACK. Every card is one big tap target.
  function slotGrid(y) {
    var n = rows.length - 1, gap = 6, backH = 18;
    var gw = Math.min(W - 16, 320), cw = Math.floor((gw - gap) / 2);
    var ch = Math.floor((H - y - backH - 10 - gap) / 2);
    var th = Math.min(ch - 16, Math.round((cw - 6) * 0.75)), tw = Math.round(th * 4 / 3);
    ch = th + 16;
    var x0 = Math.round((W - (cw * 2 + gap)) / 2);
    for (var i = 0; i < n; i++) {
      var r = rows[i], on = i === sel, x = x0 + (i % 2) * (cw + gap), yy = y + Math.floor(i / 2) * (ch + gap);
      ctx.fillStyle = on ? '#0c1450' : '#000'; ctx.fillRect(x, yy, cw, ch);
      ctx.fillStyle = on ? C.orange : C.navy;
      ctx.fillRect(x, yy, cw, 1); ctx.fillRect(x, yy + ch - 1, cw, 1); ctx.fillRect(x, yy, 1, ch); ctx.fillRect(x + cw - 1, yy, 1, ch);
      if (on) { ctx.fillRect(x + 1, yy + 1, cw - 2, 1); ctx.fillRect(x + 1, yy + ch - 2, cw - 2, 1); ctx.fillRect(x + 1, yy, 1, ch); ctx.fillRect(x + cw - 2, yy, 1, ch); }
      var tx = x + Math.round((cw - tw) / 2), ty = yy + 3;
      var im = r.time ? thumbs[r.slot] : null;
      if (r.time && im === undefined) {
        thumbs[r.slot] = null;
        var url = H0.thumb ? H0.thumb(r.slot) : '';
        if (url) (function (k) { var img = new Image(); img.onload = function () { thumbs[k] = img; draw(); }; img.src = url; })(r.slot);
      }
      if (im) ctx.drawImage(im, tx, ty, tw, th);
      else { ctx.fillStyle = '#111'; ctx.fillRect(tx, ty, tw, th); G.textC(ctx, r.time ? '...' : 'EMPTY', x + cw / 2, ty + th / 2 - 4, C.grey); }
      G.text(ctx, String(r.slot), x + 4, yy + ch - 11, on ? C.orange : C.lgrey);
      if (r.time) G.textR(ctx, r.value, x + cw - 4, yy + ch - 11, on ? C.white : C.blue);
      (function (idx, rr) { hit.push({ x: x, y: yy, w: cw, h: ch, fn: function () { sel = idx; if (rr.fn) rr.fn(1); } }); })(i, r);
    }
    var by = y + 2 * (ch + gap) + 2, bw = 96, bx = Math.round((W - bw) / 2), bon = sel === n;
    ctx.fillStyle = bon ? '#0c1450' : '#000'; ctx.fillRect(bx, by, bw, backH);
    ctx.fillStyle = bon ? C.orange : C.navy; ctx.fillRect(bx, by, bw, 1); ctx.fillRect(bx, by + backH - 1, bw, 1);
    G.textC(ctx, 'BACK', W / 2, by + 6, bon ? C.white : C.lgrey);
    hit.push({ x: bx, y: by, w: bw, h: backH, fn: function () { sel = n; back(); } });
  }

  function drawConfirm() {
    hit.push({ x: 0, y: 0, w: W, h: H, fn: function () {} });
    var w = Math.min(W - 16, Math.max(confirm.q.length * 8 + 16, 120)), h = 44, x = Math.round((W - w) / 2), y = Math.round((H - h) / 2);
    ctx.fillStyle = '#000'; ctx.fillRect(x, y, w, h);
    ctx.fillStyle = C.orange; ctx.fillRect(x, y, w, 1); ctx.fillRect(x, y + h - 1, w, 1); ctx.fillRect(x, y, 1, h); ctx.fillRect(x + w - 1, y, 1, h);
    var q = confirm.q; if (q.length * 8 > w - 8) q = q.substr(0, Math.floor((w - 8) / 8));
    G.textC(ctx, q, W / 2, y + 8, C.white);
    var yes = confirm.sel === 0;
    G.text(ctx, (yes ? '>' : ' ') + 'YES', x + w / 2 - 52, y + 26, yes ? C.orange : C.lgrey);
    G.text(ctx, (!yes ? '>' : ' ') + 'NO', x + w / 2 + 12, y + 26, !yes ? C.orange : C.lgrey);
    hit.push({ x: x, y: y + 20, w: w / 2, h: 20, fn: function () { var c = confirm; confirm = null; c.fn(); } });
    hit.push({ x: x + w / 2, y: y + 20, w: w / 2, h: 20, fn: function () { confirm = null; } });
  }

  refreshInfo(); build(); resize(); music();
  setInterval(function () { tick++; if (tick % 4 === 0 || toastT >= tick) draw(); }, 50);
})();
