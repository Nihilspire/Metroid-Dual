/* link.js - Metroid Link screen: status, auto-map, live password. Reads RAM through window.Native
 * (MainActivity.LinkBridge). Draws at NES resolution and lets CSS scale it up with nearest-neighbour. */
(function () {
  'use strict';
  var G = NesGfx, M = Metroid;
  var DUAL = !!(window.Native && Native.dual);
  var RA = !!(window.Native && Native.raStatus);     // Metroid Link fork: the game runs in RetroArch (network commands)
  var cv = document.getElementById('c'), ctx = cv.getContext('2d');
  var W = 256, H = 240, layout = {}, tabs = [], tab = 'map', buttons = [];
  var BEAM_BIT = { 2: 0x04, 6: 0x40, 7: 0x80 };   // long, wave, ice beam: SamusGear ($6878) bits
  var beams = [];
  var rom = null, items = null, worldMap = null, spr = null, liveChr = null, totals = { tanks: 0, missiles: 0 };
  var ram = null, inGame = false, tickN = 0, lastChr = 0, pal = null;
  var store = window.localStorage || { getItem: function () { return null; }, setItem: function () {} };
  var opts = load('md.opts', { reveal: false, markers: 'explored', guide: true });
  if (opts.guide === undefined) opts.guide = true;
  var visited = (store.getItem('md.visited') || '').split('');
  if (visited.length !== 1024) { visited = []; for (var i = 0; i < 1024; i++) visited.push('.'); }
  var saved = load('md.passwords', []);
  var shift = 1 + Math.floor(Math.random() * 15), pwCache = '', confirmClear = 0;

  // Area colours (NES palette indices): fill, edge
  var AREA_COL = [[0x12, 0x21], [0x16, 0x27], [0x1A, 0x2A], [0x1C, 0x2C], [0x14, 0x24]];
  var C = { white: G.css(0x30), blue: G.css(0x21), dblue: G.css(0x01), navy: G.css(0x02), orange: G.css(0x27), red: G.css(0x16),
            grey: G.css(0x00), lgrey: G.css(0x10), green: G.css(0x2A), yellow: G.css(0x28), pink: G.css(0x25), black: '#000' };

  function load(k, d) { try { var v = JSON.parse(store.getItem(k)); return v == null ? d : v; } catch (e) { return d; } }
  function save(k, v) { try { store.setItem(k, JSON.stringify(v)); } catch (e) {} }

  // ------------------------------------------------------------------ ROM data
  function loadRom() {
    if (!window.Native) return;
    var font = G.b64(Native.rom(M.ROM.font, 0x400));
    if (font.length === 0x400) G.setFont(font);
    var head = G.b64(Native.rom(0, 0x20010));                  // whole ROM: the map reads room data from every area
    if (head.length < 0x8010) head = G.b64(Native.rom(0, 0x8010));
    if (head.length < 0x8010) return;
    rom = head; shapes = {}; areaGuess = null; strictMap = null;
    items = M.items(rom);
    worldMap = M.worldMap(rom);
    spr = G.b64(Native.rom(M.ROM.sprites, 0x1000));
    totals = { tanks: 0, missiles: 0 };
    cleanVisited();
    items.slice(0, 59).forEach(function (it) { if (it.type === 8) totals.tanks++; if (it.type === 9) totals.missiles++; });
    // Beams aren't in the item table (they can be picked up again), so take them from each area's special items
    beams = [];
    if (window.MRoom) for (var a = 0; a < 5; a++) try {
      MRoom.spec(rom, a).forEach(function (e) { e.powerups.forEach(function (pu) { if (BEAM_BIT[pu.type]) beams.push({ type: pu.type, x: e.x, y: e.y }); }); });
    } catch (err) {}
  }
  window.onRom = function () { loadRom(); draw(); };
  window.onSession = function () { shift = 1 + Math.floor(Math.random() * 15); };
  window.onLayout = function () { resize(); };
  window.onDev = function () { draw(); };

  // ------------------------------------------------------------------ layout
  function resize() {
    var r = innerWidth / Math.max(1, innerHeight);
    if (r >= 1) { H = 240; W = Math.max(256, Math.round(240 * r)); } else { W = 256; H = Math.round(256 / r); }
    cv.width = W; cv.height = H;
    ctx.imageSmoothingEnabled = false;
    var tabH = 14;
    layout = { tabY: H - tabH };
    if (W >= 300) {                       // landscape: status column on the left
      layout.a = { x: 4, y: 4 }; layout.b = { x: 4, y: 88 };
      layout.pane = { x: 136, y: 2, w: W - 138, h: H - tabH - 4 };
      tabs = ['map', 'options'];
    } else if (H >= 300) {                // portrait: status across the top
      layout.a = { x: 0, y: 4 }; layout.b = { x: 130, y: 4 };
      layout.pane = { x: 0, y: 88, w: W, h: H - tabH - 90 };
      tabs = ['map', 'options'];
    } else {                              // small: everything behind tabs
      layout.a = { x: 64, y: 8 }; layout.b = { x: 64, y: 96 };
      layout.pane = { x: 0, y: 2, w: W, h: H - tabH - 4 };
      tabs = ['status', 'map', 'options'];
      layout.small = true;
    }
    if (tabs.indexOf(tab) < 0) tab = tabs[0];
    draw();
  }
  addEventListener('resize', resize);

  // ------------------------------------------------------------------ polling
  function tick() {
    tickN++;
    if (!rom && window.Native && tickN % 15 === 1) loadRom();
    var hex = window.Native && Native.running() ? Native.mem(M.specString()) : '';
    ram = hex ? M.parse(hex) : null;
    inGame = !!ram && ram.mode[0] === 0 && ram.mode[1] >= 3 && ram.mode[1] <= 5;
    if (ram && inGame && ram.mode[1] === 3) {
      var idx = (ram.map[0] & 31) * 32 + (ram.map[1] & 31), a = String(ram.area[0] & 15);
      // Only mark a cell for the area it really belongs to: when an elevator switches area, Samus is still in the old
      // area's shaft for a moment, and marking that cell with the new area made a phantom room on the map.
      var sure = strictArea(idx);
      if (visited[idx] !== a && (ram.area[0] & 15) < 5 && (sure < 0 || String(sure) === a)) { visited[idx] = a; store.setItem('md.visited', visited.join('')); }
    }
    if (ram && tickN - lastChr > 20) {
      lastChr = tickN;
      var p = Native.pal(); if (p && p.length === 64) { pal = []; for (var i = 0; i < 32; i++) pal.push(parseInt(p.substr(i * 2, 2), 16)); }
      var c = Native.chr(); if (c) liveChr = G.b64(c);
    }
    draw();
  }

  // ------------------------------------------------------------------ drawing
  function draw() {
    ctx.fillStyle = '#000'; ctx.fillRect(0, 0, W, H);
    buttons = []; mapBox = null;
    if (!ram || !rom) { idle(); return; }
    if (ram.ending[0] === 1) { missionReport(); return; }
    if (ram.mode[0] !== 0) { idle('TITLE SCREEN', 'PRESS START'); return; }     // RAM holds no game yet
    report = null;
    if (!layout.small || tab === 'status') { blockA(layout.a.x, layout.a.y); blockB(layout.b.x, layout.b.y); }
    if (tab !== 'status') {
      var p = layout.pane;
      if (!layout.small) frameBox(p.x, p.y, p.w, p.h);
      if (tab === 'map') drawMap(p);
      else if (tab === 'password') drawPassword(p);
      else if (tab === 'options') drawOptions(p);
    }
    drawTabs();
  }

  var stars = [];
  for (var s = 0; s < 60; s++) stars.push([Math.random(), Math.random(), Math.floor(Math.random() * 4)]);
  function idle(msgOverride, subOverride) {
    stars.forEach(function (st, i) {
      var tw = (tickN + i * 7) % 40 < 20;
      ctx.fillStyle = [C.blue, C.white, C.orange, C.pink][st[2]];
      if (tw || i % 3) ctx.fillRect(Math.floor(st[0] * W), Math.floor(st[1] * H), 1, 1);
    });
    var cy = Math.floor(H / 2) - 24;
    G.textC(ctx, 'METROID', W / 2, cy, C.orange, 2);
    G.textC(ctx, 'LINK', W / 2, cy + 20, C.blue);
    var msg = msgOverride || (!rom ? 'INSERT GAME PAK' : 'NO SIGNAL');
    if ((tickN >> 3) % 2 === 0) G.textC(ctx, msg, W / 2, cy + 44, C.white);
    var sub = subOverride || (!rom ? (RA ? 'TAP TO CHOOSE YOUR ROM' : 'CHOOSE THE ROM IN THE MENU') : 'START THE GAME');
    if (!rom && RA && Native.pickRom) buttons.push({ x: 0, y: 0, w: W, h: H, fn: function () { Native.pickRom(); } });
    if (!subOverride && rom && RA) { var st = Native.raStatus().split('|'); sub = st[0]; if (st[1]) G.textC(ctx, st[1], W / 2, cy + 76, C.grey); }
    G.textC(ctx, sub, W / 2, cy + 60, C.lgrey);
  }

  function frameBox(x, y, w, h) {
    var a = ram ? (ram.area[0] & 15) % 5 : 0, col = G.css(AREA_COL[a][1]), dk = G.css(AREA_COL[a][0]);
    ctx.fillStyle = dk; ctx.fillRect(x, y, w, 1); ctx.fillRect(x, y + h - 1, w, 1); ctx.fillRect(x, y, 1, h); ctx.fillRect(x + w - 1, y, 1, h);
    ctx.fillStyle = col;
    [[x, y], [x + w - 3, y], [x, y + h - 3], [x + w - 3, y + h - 3]].forEach(function (c) { ctx.fillRect(c[0], c[1], 3, 3); });
  }

  function sprPal(n) {
    var p = pal || [0x0F, 0, 0, 0, 0x0F, 0, 0, 0, 0x0F, 0, 0, 0, 0x0F, 0, 0, 0, 0x0F, 0x16, 0x1A, 0x27, 0x0F, 0x12, 0x30, 0x21, 0x0F, 0x27, 0x2A, 0x3C, 0x0F, 0x15, 0x21, 0x38];
    return [null, G.css(p[16 + n * 4 + 1]), G.css(p[16 + n * 4 + 2]), G.css(p[16 + n * 4 + 3])];
  }
  var DIM = [null, '#121a52', '#121a52', '#121a52'];

  function pad(n, w) { n = String(n); while (n.length < w) n = '0' + n; return n; }
  function hms(sec) { sec = Math.max(0, Math.floor(sec)); return Math.floor(sec / 3600) + ':' + pad(Math.floor(sec / 60) % 60, 2) + ':' + pad(sec % 60, 2); }

  function blockA(x, y) {
    var st = M.status(ram), en = M.energy(ram), a = (ram.area[0] & 15);
    var areaName = a < 5 ? M.AREAS[a] : '';
    var picked = nameOfLast(st);
    if (picked) G.text(ctx, picked, x + 2, y, (tickN >> 2) % 2 ? C.yellow : C.white);
    else if (inGame) G.text(ctx, areaName, x + 2, y, G.css(AREA_COL[a % 5][1]));
    if (inGame && ram.mode[1] === 5 && (tickN >> 3) % 2) G.textR(ctx, 'PAUSE', x + 126, y, C.orange);
    // Samus, facing forward (frame 7), from live sprite memory so suit/suitless match the game
    var chr = liveChr && liveChr.length >= 0x1000 ? liveChr : spr, fr = M.frame(rom, 7), sp = sprPal(0);
    fr.forEach(function (t) { G.drawTile(ctx, G.decode(chr, t.tile * 16), x + 12 + t.x, y + 30 + t.y, sp, t.hflip, t.vflip); });
    // Tanks + EN like the in-game HUD
    for (var i = 0; i < en.tanks; i++) {
      ctx.fillStyle = i < en.full ? C.white : C.dblue;
      ctx.fillRect(x + 30 + i * 8, y + 13, 7, 6);
      if (i >= en.full) { ctx.fillStyle = '#000'; ctx.fillRect(x + 31 + i * 8, y + 14, 5, 4); }
    }
    G.text(ctx, 'EN', x + 30, y + 22, C.blue);
    G.text(ctx, pad(en.en, 2), x + 50, y + 22, en.en < 10 && en.full === 0 && (tickN >> 2) % 2 ? C.red : C.white);
    // Missiles
    var ip = sprPal(1);
    if (st.maxMissiles > 0) {
      G.meta(ctx, spr, M.MISSILE_TILES, x + 28, y + 34, ip);
      G.text(ctx, pad(st.missiles, 3) + '/' + pad(st.maxMissiles, 3), x + 46, y + 38, C.white);
    } else G.text(ctx, 'NO MISSILES', x + 30, y + 38, C.grey);
    // Gear row
    M.GEAR.forEach(function (g, k) {
      var own = (st.gear & g.bit) !== 0;
      G.meta(ctx, spr, g.tiles, x + k * 16, y + 58, own ? ip : DIM);
    });
  }
  var lastGear = -1, lastName = '', lastAt = 0, clockHold = null;
  function nameOfLast(st) {
    if (lastGear >= 0 && st.gear !== lastGear) {
      var gained = st.gear & ~lastGear;
      M.GEAR.forEach(function (g) { if (gained & g.bit) { lastName = g.name; lastAt = tickN; } });
    }
    lastGear = st.gear;
    return tickN - lastAt < 150 ? lastName : '';
  }

  function blockB(x, y) {
    var st = M.status(ram), got = M.collected(ram, items);
    var mb = !!got[58], kraid = M.kraidDown(st), ridley = M.ridleyDown(st);
    function row(name, dead, statue, yy) {
      G.text(ctx, name, x, yy, C.white);
      G.textR(ctx, dead ? 'DOWN' : 'ALIVE', x + 124, yy, dead ? C.green : C.red);
      if (statue !== undefined && dead) G.textR(ctx, statue ? 'S' : '', x + 84, yy, C.yellow);
    }
    row('KRAID', kraid, (st.kraid & 0x80) !== 0, y);
    row('RIDLEY', ridley, (st.ridley & 0x80) !== 0, y + 10);
    row('M.BRAIN', mb, undefined, y + 20);
    var zeb = 0; for (var z = 53; z <= 57; z++) if (got[z]) zeb++;
    if (zeb && !mb) { G.text(ctx, 'ZEBETITE', x, y + 30, C.lgrey); G.textR(ctx, zeb + '/5', x + 124, y + 30, C.orange); }
    var tanks = 0, ms = 0;
    for (var k in got) { var it = items[k]; if (it && it.type === 8) tanks++; if (it && it.type === 9) ms++; }
    G.text(ctx, 'E-TANKS', x, y + 42, C.blue); G.textR(ctx, tanks + '/' + totals.tanks, x + 124, y + 42, C.white);
    G.text(ctx, 'MISSILES', x, y + 52, C.orange); G.textR(ctx, ms + '/' + totals.missiles, x + 124, y + 52, C.white);
    var esc = ram.escape[1];
    if (esc !== 0xFF) {
      var t = (esc >> 4) * 100 + (esc & 15) * 10 + (ram.escape[0] >> 4);
      var col = (tickN >> 2) % 2 ? C.red : C.white;
      G.text(ctx, 'ESCAPE', x, y + 66, col); G.textR(ctx, pad(t, 3), x + 124, y + 66, col);
    } else {
      // The clock runs from the age counter + FrameCount; it holds still while the game is paused or not running
      // (FrameCount keeps counting there but the age doesn't).
      var running = inGame && ram.mode[1] === 3;
      var secs = M.playSeconds(st.age, ram.frame[0]), end = M.ending(st.age, ram.frame[0]);
      if (running || clockHold == null) clockHold = { secs: secs, end: end }; else { secs = clockHold.secs; end = clockHold.end; }
      G.text(ctx, 'TIME', x, y + 64, C.lgrey); G.textR(ctx, hms(secs), x + 124, y + 64, C.white);
      G.text(ctx, 'ENDING ' + end.rank, x, y + 74, end.rank >= 4 ? C.yellow : C.lgrey);
      if (end.secondsLeft != null) G.textR(ctx, hms(end.secondsLeft), x + 124, y + 74, C.grey);
    }
  }


  // ------------------------------------------------------------------ mission report (ending)
  // Shown while the game's ending plays ($6883 AtEnding = 1). The date/time and stats are captured once when the
  // ending starts. Below the report is a signature field the player draws their name in with a finger; strokes are
  // kept at the page's logical resolution so they look like NES pixels. SAVE stores the run with the signature
  // (a small PNG) in localStorage (md.hall).
  var report = null, hall = load('md.hall', []), sigBox = null, drawing = null;
  function missionReport() {
    if (!report) {
      var st = M.status(ram), now = new Date();
      report = { date: now.getFullYear() + '-' + pad(now.getMonth() + 1, 2) + '-' + pad(now.getDate(), 2),
                 clock: pad(now.getHours(), 2) + ':' + pad(now.getMinutes(), 2),
                 t: now.getTime(), secs: M.playSeconds(st.age, 0), ending: ram.endType[0] || M.ending(st.age).rank,
                 col: M.collection(ram, items), saved: false, sig: null, sw: 0, sh: 0 };
    }
    var r = report, cx = W / 2, y = 6;
    stars.forEach(function (st, i) {
      if ((tickN + i * 7) % 40 < 26) { ctx.fillStyle = [C.blue, C.white, C.orange, C.pink][st[2]]; ctx.fillRect(Math.floor(st[0] * W), Math.floor(st[1] * H), 1, 1); }
    });
    G.textC(ctx, 'MISSION REPORT', cx, y, C.orange); y += 12;
    G.textC(ctx, r.date + '  ' + r.clock, cx, y, C.white); y += 14;
    // stats: two columns when the screen is wide enough (4:3 landscape), one otherwise
    var rows = [['TIME', hms(r.secs), C.white], ['ENDING', r.ending + '/5', r.ending >= 4 ? C.yellow : C.white],
                ['ITEMS', r.col.percent + '%', C.yellow], ['E-TANKS', r.col.tanks + '/' + r.col.tanksTotal, C.blue],
                ['MISSILES', r.col.missiles + '/' + r.col.missilesTotal, C.orange], ['GEAR', r.col.gear + '/' + r.col.gearTotal, C.green]];
    var cols = W >= 300 ? 2 : 1, colW = Math.floor((W - 16) / cols), per = Math.ceil(rows.length / cols);
    rows.forEach(function (row, i) {
      var c = Math.floor(i / per), rr = i % per, x0 = 8 + c * colW + (c ? 6 : 0), x1 = 8 + (c + 1) * colW - (c ? 0 : 6);
      G.text(ctx, row[0], x0, y + rr * 11, C.lgrey); G.textR(ctx, row[1], x1, y + rr * 11, row[2]);
    });
    y += per * 11 + 2;
    if (window.Native && Native.devUsed && Native.devUsed()) { G.textC(ctx, 'DEV CODES USED', cx, y, C.red); y += 10; r.dev = true; }
    y += 4;
    // signature field
    G.text(ctx, r.saved ? 'SIGNED' : 'SIGN YOUR NAME', 8, y, r.saved ? C.green : C.orange);
    y += 11;
    var bx = 8, bw = W - 16, by = y, bh = Math.max(40, H - y - 22);
    if (!r.sig || r.sw !== bw || r.sh !== bh) { r.sig = new Uint8Array(bw * bh); r.sw = bw; r.sh = bh; }
    sigBox = r.saved ? null : { x: bx, y: by, w: bw, h: bh };
    ctx.fillStyle = '#05082a'; ctx.fillRect(bx, by, bw, bh);
    ctx.fillStyle = C.navy; ctx.fillRect(bx, by, bw, 1); ctx.fillRect(bx, by + bh - 1, bw, 1); ctx.fillRect(bx, by, 1, bh); ctx.fillRect(bx + bw - 1, by, 1, bh);
    ctx.fillStyle = '#1c2460'; ctx.fillRect(bx + 8, by + bh - 10, bw - 16, 1);          // signing line
    ctx.fillStyle = C.white;
    for (var i = 0; i < r.sig.length; i++) if (r.sig[i]) ctx.fillRect(bx + (i % bw), by + Math.floor(i / bw), 1, 1);
    if (!r.saved) {
      var ty = by + bh + 7;
      button('CLEAR', W - 120, ty, 48, function () { r.sig.fill(0); });
      button('SAVE', W - 64, ty, 48, function () {
        if (!r.sig.some(function (v) { return v; })) return;
        var oc = document.createElement('canvas'); oc.width = bw; oc.height = bh;
        var ox = oc.getContext('2d'); ox.fillStyle = '#fff';
        for (var k = 0; k < r.sig.length; k++) if (r.sig[k]) ox.fillRect(k % bw, Math.floor(k / bw), 1, 1);
        hall.push({ dev: !!r.dev, sig: oc.toDataURL('image/png'), date: r.date, clock: r.clock, secs: r.secs, ending: r.ending, percent: r.col.percent, t: r.t });
        hall = hall.slice(-20); save('md.hall', hall); r.saved = true;
      });
    }
  }
  function sigPlot(x0, y0, x1, y1) {           // Bresenham line in logical pixels, 1 px pen
    var r = report, b = sigBox; if (!r || !b) return;
    var dx = Math.abs(x1 - x0), dy = -Math.abs(y1 - y0), sx = x0 < x1 ? 1 : -1, sy = y0 < y1 ? 1 : -1, err = dx + dy;
    for (var n = 0; n < 2000; n++) {
      var px = x0 - b.x, py = y0 - b.y;
      if (px >= 1 && py >= 1 && px < b.w - 1 && py < b.h - 1) r.sig[py * b.w + px] = 1;
      if (x0 === x1 && y0 === y1) break;
      var e2 = 2 * err;
      if (e2 >= dy) { err += dy; x0 += sx; }
      if (e2 <= dx) { err += dx; y0 += sy; }
    }
  }
  function logical(e) { return [Math.floor(e.clientX * W / innerWidth), Math.floor(e.clientY * H / innerHeight)]; }
  cv.addEventListener('pointerdown', function (e) {
    var p = logical(e), b = sigBox;
    if (report && b && p[0] >= b.x && p[0] < b.x + b.w && p[1] >= b.y && p[1] < b.y + b.h) {
      drawing = { id: e.pointerId, x: p[0], y: p[1] }; sigPlot(p[0], p[1], p[0], p[1]);
      try { cv.setPointerCapture(e.pointerId); } catch (err) {}
      e.stopImmediatePropagation(); draw();
    }
  }, true);
  cv.addEventListener('pointermove', function (e) {
    if (!drawing || e.pointerId !== drawing.id) return;
    var p = logical(e); sigPlot(drawing.x, drawing.y, p[0], p[1]); drawing.x = p[0]; drawing.y = p[1]; draw();
  });
  ['pointerup', 'pointercancel'].forEach(function (ev) { cv.addEventListener(ev, function () { drawing = null; }); });
  cv.addEventListener('pointercancel', function () { mapDrag = null; });

  // ------------------------------------------------------------------ map
  // Zero Mission style: every room is a filled cell, walls are white lines wherever two cells don't connect, doors
  // are coloured bars in the wall. Connections come from the room data in the ROM (MRoom): side by side when
  // there's no door between them and both edges are open, above/below when the floor of one meets an open ceiling.
  var shapes = {}, areaGuess = null;
  function roomShape(a, n) {
    var k = a * 256 + n;
    if (shapes[k] !== undefined) return shapes[k];
    var sh = null;
    try {
      if (window.MRoom && rom && rom.length >= 0x18010 && n < MRoom.area(rom, a).roomCount) {
        var r = MRoom.room(rom, a, n), nt = r.nt, open = function (row, col) { var t = nt[row * 32 + col]; return t === 0xFF || t >= 0xA0; };
        sh = { top: [], bottom: [], left: [], right: [], doorL: -1, doorR: -1 };
        for (var c = 0; c < 32; c++) { sh.top.push(open(0, c) && open(1, c)); sh.bottom.push(open(28, c) && open(29, c)); }
        for (var rr = 0; rr < 30; rr++) { sh.left.push(open(rr, 0)); sh.right.push(open(rr, 31)); }
        r.doors.forEach(function (d) { if (d.left) sh.doorL = d.kind; else sh.doorR = d.kind; });
      }
    } catch (e) { sh = null; }
    return (shapes[k] = sh);
  }
  var strictMap = null;
  /** The area a cell surely belongs to (rooms reached through real connections), or -1. */
  function strictArea(i) {
    if (!strictMap && window.MRoom && rom && rom.length >= 0x18010) { try { strictMap = MRoom.areaMap(rom, true); } catch (e) { strictMap = new Int8Array(1024).fill(-1); } }
    return strictMap ? strictMap[i] : -1;
  }
  /** Clears map marks a phantom could have left (cells marked for an area they don't belong to, empty cells). */
  function cleanVisited() {
    var changed = false;
    for (var i = 0; i < 1024; i++) {
      if (visited[i] === '.') continue;
      var sa = strictArea(i);
      if (worldMap[i] === 0xFF || (sa >= 0 && String(sa) !== visited[i])) { visited[i] = '.'; changed = true; }
    }
    if (changed) store.setItem('md.visited', visited.join(''));
  }
  function cellArea(i) {
    if (visited[i] !== '.') return +visited[i];
    if (!areaGuess && window.MRoom && rom && rom.length >= 0x18010) { try { areaGuess = MRoom.areaMap(rom); } catch (e) { areaGuess = new Int8Array(1024).fill(-1); } }
    return areaGuess ? areaGuess[i] : -1;
  }
  function cellShape(i) { var a = cellArea(i); return a >= 0 && worldMap[i] !== 0xFF ? roomShape(a, worldMap[i]) : null; }
  function overlap(a, b) { for (var i = 0; i < a.length; i++) if (a[i] && b[i]) return true; return false; }
  /** do cells i and j (j right of / below i) connect without a wall? */
  function linked(i, j, down) {
    var A = cellShape(i), B = cellShape(j);
    if (!A || !B || cellArea(i) !== cellArea(j)) return false;
    if (down) return overlap(A.bottom, B.top);
    return A.doorR < 0 && B.doorL < 0 && overlap(A.right, B.left);
  }
  var DOOR_COL = [0x16, 0x21, 0x27, 0x21];
  var ZM = { bg: '#04210f', grid: '#0b3a1e', fill: '#1830d8', fillHere: '#3858f8', known: '#2a3038', wall: '#fcfcfc', dim: '#9cb4c8' };
  // Close-up map: drag to look around (panX/Y in canvas pixels, kept while you stay in close-up); a tap still
  // switches to the whole map, and coming back from it re-centres on Samus.
  var mapBox = null, mapPan = { x: 0, y: 0 }, mapDrag = null;
  function toggleZoom() {
    opts.zoom = opts.zoom === 'whole' ? 'near' : 'whole'; opts.tapped = true; save('md.opts', opts);
    mapPan.x = mapPan.y = 0;
  }
  function drawMap(p) {
    var textH = 34, areaH = p.h - textH - 4, gd0 = opts.guide ? M.guide(ram, items) : null;
    var near = opts.zoom !== 'whole' && inGame;
    var x0 = 31, y0 = 31, x1 = 0, y1 = 0, any = false, cell, mx, my;
    var addCell = function (x, y) { any = true; if (x < x0) x0 = x; if (y < y0) y0 = y; if (x > x1) x1 = x; if (y > y1) y1 = y; };
    if (near) {
      // zoomed in around Samus: about 11 rooms across
      cell = Math.max(8, Math.min(18, Math.floor((p.w - 6) / 11)));
      var sx0 = (ram.map[1] & 31) * cell + cell / 2, sy0 = (ram.map[0] & 31) * cell + cell / 2;
      mx = Math.round(p.x + p.w / 2 - sx0); my = Math.round(p.y + 1 + areaH / 2 - sy0);
      // pan, kept so the map's edge can't go past the middle of the view
      var bx = mx, by = my;
      mx = Math.max(p.x + p.w / 2 - 32 * cell, Math.min(p.x + p.w / 2, mx + Math.round(mapPan.x)));
      my = Math.max(p.y + areaH / 2 - 32 * cell, Math.min(p.y + areaH / 2, my + Math.round(mapPan.y)));
      mapPan.x = mx - bx; mapPan.y = my - by;
      x0 = Math.max(0, Math.floor((p.x - mx) / cell)); x1 = Math.min(31, Math.ceil((p.x + p.w - mx) / cell));
      y0 = Math.max(0, Math.floor((p.y - my) / cell)); y1 = Math.min(31, Math.ceil((p.y + areaH - my) / cell));
    } else {
      // whole map: everything explored, Samus and the guide target (or all 32x32 with ALL ROOMS)
      if (opts.reveal) { x0 = 0; y0 = 0; x1 = 31; y1 = 31; any = true; }
      else {
        for (var i = 0; i < 1024; i++) if (visited[i] !== '.') addCell(i % 32, i >> 5);
        if (inGame) addCell(ram.map[1] & 31, ram.map[0] & 31);
        if (gd0 && gd0.target) addCell(gd0.target.x, gd0.target.y);
        if (!any) { x0 = 0; y0 = 0; x1 = 31; y1 = 31; }
        x0 = Math.max(0, x0 - 1); y0 = Math.max(0, y0 - 1); x1 = Math.min(31, x1 + 1); y1 = Math.min(31, y1 + 1);
      }
      var bw = x1 - x0 + 1, bh = y1 - y0 + 1;
      cell = Math.max(3, Math.min(14, Math.floor(Math.min((p.w - 6) / bw, areaH / bh))));
      mx = p.x + Math.floor((p.w - bw * cell) / 2) - x0 * cell; my = p.y + 3 + Math.floor((areaH - bh * cell) / 2) - y0 * cell;
    }
    ctx.save(); ctx.beginPath(); ctx.rect(p.x + 1, p.y + 1, p.w - 2, areaH + 2); ctx.clip();
    ctx.fillStyle = ZM.bg; ctx.fillRect(p.x + 1, p.y + 1, p.w - 2, areaH + 2);
    if (cell >= 6) { ctx.fillStyle = ZM.grid;
      for (var gx = x0; gx <= x1 + 1; gx++) ctx.fillRect(mx + gx * cell, p.y + 1, 1, areaH + 2);
      for (var gy = y0; gy <= y1 + 1; gy++) ctx.fillRect(p.x + 1, my + gy * cell, p.w - 2, 1); }
    var got = M.collected(ram, items), here = inGame ? (ram.map[0] & 31) * 32 + (ram.map[1] & 31) : -1;
    var shown = function (i) { return i >= 0 && i < 1024 && worldMap[i] !== 0xFF && (visited[i] !== '.' || opts.reveal); };
    var wt = cell >= 12 ? 2 : 1;
    // fills
    for (var y = y0; y <= y1; y++) for (var x = x0; x <= x1; x++) {
      var ci = y * 32 + x; if (!shown(ci)) continue;
      var ar = cellArea(ci);   // area colours as before: darker fill, the room Samus is in in the area's light colour
      ctx.fillStyle = visited[ci] === '.' ? ZM.known : ar >= 0 ? G.css(AREA_COL[ar % 5][ci === here ? 1 : 0]) : ZM.fill;
      ctx.fillRect(mx + x * cell, my + y * cell, cell, cell);
    }
    // walls + doors
    var doors = [];
    for (y = y0; y <= y1; y++) for (x = x0; x <= x1; x++) {
      ci = y * 32 + x; if (!shown(ci)) continue;
      var px = mx + x * cell, py = my + y * cell, sh = cellShape(ci);
      ctx.fillStyle = ZM.wall;
      if (!(y > 0 && shown(ci - 32) && linked(ci - 32, ci, true))) ctx.fillRect(px, py, cell, wt);
      if (!(y < 31 && shown(ci + 32) && linked(ci, ci + 32, true))) ctx.fillRect(px, py + cell - wt, cell, wt);
      if (!(x > 0 && shown(ci - 1) && linked(ci - 1, ci, false))) ctx.fillRect(px, py, wt, cell);
      if (!(x < 31 && shown(ci + 1) && linked(ci, ci + 1, false))) ctx.fillRect(px + cell - wt, py, wt, cell);
      if (sh && cell >= 5) {
        // a door is one solid bar set into the wall line between the two rooms (drawn after all the walls)
        var dh = Math.max(2, Math.round(cell * 0.45)), dy = py + Math.floor((cell - dh) / 2);
        if (sh.doorL >= 0) doors.push([px - wt, dy, wt * 2, dh, sh.doorL]);
        if (sh.doorR >= 0) doors.push([px + cell - wt, dy, wt * 2, dh, sh.doorR]);
      }
    }
    doors.forEach(function (d) { ctx.fillStyle = G.css(DOOR_COL[d[4] & 3]); ctx.fillRect(d[0], d[1], d[2], d[3]); });
    // items
    if (opts.markers !== 'off') items.slice(0, 59).forEach(function (it) {
      if (!(it.x || it.y) || it.type === 10) return;
      var v = visited[it.y * 32 + it.x];
      if (opts.markers === 'explored' && v === '.') return;
      var have = !!got[it.i], c = Math.max(1, Math.floor(cell / 3)), px = mx + it.x * cell + Math.floor((cell - c) / 2), py = my + it.y * cell + Math.floor((cell - c) / 2);
      var col = it.type === 8 ? C.white : it.type === 9 ? C.orange : C.yellow;
      if (have) { ctx.fillStyle = '#000'; ctx.fillRect(px, py, c, c); }
      else if ((tickN >> 3) % 2 || cell < 5) { ctx.fillStyle = col; ctx.fillRect(px, py, c, c); }
      else { ctx.fillStyle = col; ctx.fillRect(px, py, c, c); ctx.fillStyle = '#000'; if (c > 2) ctx.fillRect(px + 1, py + 1, c - 2, c - 2); }
    });
    // beams: light blue, shown as taken while Samus has that beam (the game doesn't record beam pickups)
    if (opts.markers !== 'off') beams.forEach(function (b) {
      var v = visited[b.y * 32 + b.x];
      if (opts.markers === 'explored' && v === '.') return;
      var have = !!(ram && ram.stats[1] & BEAM_BIT[b.type]), c = Math.max(1, Math.floor(cell / 3));
      var px = mx + b.x * cell + Math.floor((cell - c) / 2), py = my + b.y * cell + Math.floor((cell - c) / 2);
      if (have) { ctx.fillStyle = '#000'; ctx.fillRect(px, py, c, c); }
      else if ((tickN >> 3) % 2 || cell < 5) { ctx.fillStyle = C.blue; ctx.fillRect(px, py, c, c); }
      else { ctx.fillStyle = C.blue; ctx.fillRect(px, py, c, c); ctx.fillStyle = '#000'; if (c > 2) ctx.fillRect(px + 1, py + 1, c - 2, c - 2); }
    });
    // Samus
    if (inGame) {
      var sx = mx + (ram.map[1] & 31) * cell, sy = my + (ram.map[0] & 31) * cell, m = Math.max(2, Math.floor(cell / 3));
      if ((tickN >> 2) % 2) { ctx.fillStyle = C.orange; ctx.fillRect(sx + (cell - m) / 2 | 0, sy + (cell - m) / 2 | 0, m, m); }
    }
    // next-location guide: target room blinks, dotted line from Samus
    var gd = gd0;
    if (gd && gd.target) {
      var gx2 = mx + gd.target.x * cell, gy2 = my + gd.target.y * cell;
      if ((tickN >> 3) % 2 === 0) {
        ctx.fillStyle = C.yellow;
        ctx.fillRect(gx2 - 1, gy2 - 1, cell + 2, 1); ctx.fillRect(gx2 - 1, gy2 + cell, cell + 2, 1);
        ctx.fillRect(gx2 - 1, gy2 - 1, 1, cell + 2); ctx.fillRect(gx2 + cell, gy2 - 1, 1, cell + 2);
      }
      if (inGame) {
        var ax = mx + (ram.map[1] & 31) * cell + cell / 2, ay = my + (ram.map[0] & 31) * cell + cell / 2;
        var bx2 = gx2 + cell / 2, by2 = gy2 + cell / 2, dist = Math.hypot(bx2 - ax, by2 - ay), steps = Math.floor(dist / 3);
        ctx.fillStyle = C.yellow;
        for (var k = 1; k < steps; k++) if ((k + (tickN >> 2)) % 3 === 0) ctx.fillRect(Math.round(ax + (bx2 - ax) * k / steps), Math.round(ay + (by2 - ay) * k / steps), 1, 1);
      }
    }
    ctx.restore();
    // tap the map to switch between the close-up and the whole map
    mapBox = { x: p.x, y: p.y, w: p.w, h: areaH + 2, near: near };   // taps and drags here: see the pointer handlers
    if (inGame && !opts.tapped) G.textR(ctx, 'TAP: WHOLE MAP', p.x + p.w - 4, p.y + areaH - 8, ZM.dim);   // until the first tap
    var ly = p.y + p.h - textH;
    if (gd && ly + 8 <= p.y + p.h) {
      G.text(ctx, 'NEXT', p.x + 4, ly, C.orange);
      G.text(ctx, gd.name, p.x + 40, ly, C.yellow);
      var dir = '';
      if (gd.target && inGame) {
        var dx = gd.target.x - (ram.map[1] & 31), ddy = gd.target.y - (ram.map[0] & 31);
        dir = (dx ? (dx > 0 ? '>' : '<') + Math.abs(dx) : '') + (dx && ddy ? ' ' : '') + (ddy ? (ddy > 0 ? 'v' : '^') + Math.abs(ddy) : '');
        if (!dx && !ddy) dir = 'HERE';
      }
      if (dir) G.textR(ctx, dir, p.x + p.w - 4, ly, C.white);
      ly += 10;
      var hint = gd.hint;
      if (inGame && gd.area >= 0 && gd.area !== ((ram.area[0] & 15) % 5)) hint = 'ELEVATOR TO ' + M.AREAS[gd.area];
      if (hint && ly + 8 <= p.y + p.h) { G.text(ctx, hint.substr(0, Math.floor((p.w - 8) / 8)), p.x + 4, ly, C.lgrey); ly += 10; }
    }
    if (ly + 8 <= p.y + p.h) {
      var n = 0; for (i = 0; i < 1024; i++) if (visited[i] !== '.') n++;
      G.text(ctx, 'ROOMS ' + n, p.x + 4, ly, C.lgrey);
      if (inGame) G.textR(ctx, pad((ram.map[1] & 31).toString(16).toUpperCase(), 2) + ',' + pad((ram.map[0] & 31).toString(16).toUpperCase(), 2), p.x + p.w - 4, ly, C.grey);
    }
  }
  function isOpen(x, y) { return x >= 0 && y >= 0 && x < 32 && y < 32 && visited[y * 32 + x] !== '.'; }

  // ------------------------------------------------------------------ password
  function drawPassword(p) {
    var cx = p.x + p.w / 2, y = p.y + 8;
    G.textC(ctx, 'PASSWORD', cx, y, C.orange);
    var pw = M.password(ram, items, shift), txt = M.pwText(pw.chars);
    pwCache = txt;
    var sc = p.w >= 230 ? 2 : 1;
    var rowW = 13 * 8 * sc, x0 = Math.round(cx - rowW / 2);
    for (var r = 0; r < 2; r++) {
      G.text(ctx, txt.substr(r * 12, 6), x0, y + 16 + r * 14 * sc, C.green, sc);
      G.text(ctx, txt.substr(r * 12 + 6, 6), x0 + 7 * 8 * sc, y + 16 + r * 14 * sc, C.green, sc);
    }
    var a = ram.area[0] & 15;
    var yy = y + 18 + 28 * sc;
    G.textC(ctx, 'RESUMES IN ' + (M.AREAS[a] || '?'), cx, yy, C.lgrey);
    if (got58()) G.textC(ctx, 'TOURIAN RESETS', cx, yy + 10, C.red);
    var by = yy + 22;
    button('KEEP', cx - 36, by, 32, function () {
      saved.unshift({ pw: pwCache, area: M.AREAS[a] || '', t: Date.now() });
      saved = saved.slice(0, 8); save('md.passwords', saved);
    });
    button('NEW', cx + 4, by, 32, function () { shift = shift % 15 + 1; });
    var ly = by + 16;
    saved.forEach(function (s, i) {
      if (ly + 8 > p.y + p.h - 2) return;
      G.text(ctx, s.pw.substr(0, 6) + ' ' + s.pw.substr(6, 6), p.x + 4, ly, C.white);
      G.text(ctx, s.pw.substr(12, 6) + ' ' + s.pw.substr(18, 6), p.x + 4, ly + 9, C.white);
      G.textR(ctx, s.area.substr(0, 4), p.x + p.w - 4, ly, C.lgrey);
      var d = new Date(s.t); G.textR(ctx, pad(d.getHours(), 2) + ':' + pad(d.getMinutes(), 2), p.x + p.w - 4, ly + 9, C.grey);
      ly += 22;
    });
  }
  function got58() { return !!M.collected(ram, items)[58]; }

  // ------------------------------------------------------------------ options
  // OPTIONS: save states, map, 3D. Longer than the pane on small screens, so it scrolls (drag); taps run on release.
  var optScroll = 0, optContentH = 0, optPane = null, slotArm = null, optMsg = '', optMsgT = 0;
  function drawOptions(p) {
    optPane = p;
    ctx.save(); ctx.beginPath(); ctx.rect(p.x + 1, p.y + 1, p.w - 2, p.h - 2); ctx.clip();
    var x = p.x + 8, y0 = p.y + 8 - optScroll, y = y0;
    var vis = function (yy) { return yy > p.y && yy + 8 < p.y + p.h; };
    // --- save states
    G.text(ctx, 'SAVE STATES', x, y, C.orange);
    if (optMsgT > tickN) G.textR(ctx, optMsg, x + p.w - 16, y, C.green);
    y += 14;
    var slots = [];
    if (DUAL && Native.slots) { try { slots = JSON.parse(Native.slots()); } catch (e) {} }
    // 2x2 cards: screenshot, slot number + time, then big SAVE / LOAD buttons (one column on narrow panes)
    var cols = p.w >= 170 ? 2 : 1, gap = 6, cw = Math.floor((p.w - 16 - gap * (cols - 1)) / cols);
    var tw = cw - 4, th = Math.round(tw * 0.75), bh = 18, ch = th + 14 + bh + 8;
    slots.forEach(function (sl, i) {
      var cx = x - 4 + (i % cols) * (cw + gap), cy = y + Math.floor(i / cols) * (ch + gap);
      var d = sl.time ? new Date(sl.time) : null;
      var today = d && d.toDateString() === new Date().toDateString();
      var when = !d ? 'EMPTY' : today ? pad(d.getHours(), 2) + ':' + pad(d.getMinutes(), 2) : (d.getMonth() + 1) + '/' + d.getDate();
      if (d && cw >= 116) when = (d.getMonth() + 1) + '/' + d.getDate() + ' ' + pad(d.getHours(), 2) + ':' + pad(d.getMinutes(), 2);
      var armed = function (act) { return slotArm && slotArm.n === sl.n && slotArm.act === act && slotArm.until > tickN; };
      var hot = slotArm && slotArm.n === sl.n && slotArm.until > tickN;
      ctx.fillStyle = '#060a28'; ctx.fillRect(cx, cy, cw, ch);
      ctx.fillStyle = hot ? C.orange : C.navy;
      ctx.fillRect(cx, cy, cw, 1); ctx.fillRect(cx, cy + ch - 1, cw, 1); ctx.fillRect(cx, cy, 1, ch); ctx.fillRect(cx + cw - 1, cy, 1, ch);
      var im = sl.time ? slotThumb(sl) : null;
      if (im) ctx.drawImage(im, cx + 2, cy + 2, tw, th);
      else { ctx.fillStyle = '#000'; ctx.fillRect(cx + 2, cy + 2, tw, th); G.textC(ctx, sl.time ? '...' : 'EMPTY', cx + cw / 2, cy + 2 + th / 2 - 4, C.grey); }
      G.text(ctx, String(sl.n), cx + 4, cy + th + 5, C.orange);
      if (sl.time) G.textR(ctx, when, cx + cw - 3, cy + th + 5, C.white);
      var by = cy + th + 18, bw = sl.time ? Math.floor((cw - 10) / 2) : cw - 8;
      if (vis(by) && vis(by + bh - 8)) {
        bigButton(armed('save') ? 'SURE?' : 'SAVE', cx + 4, by, bw, bh, armed('save'), function () {
          if (sl.time && !armed('save')) { slotArm = { n: sl.n, act: 'save', until: tickN + 45 }; return; }
          slotArm = null; var ok = Native.saveSlot(sl.n); optMsg = ok ? 'SAVED ' + sl.n : 'FAILED'; optMsgT = tickN + 40;
        });
        if (sl.time) bigButton(armed('load') ? 'SURE?' : 'LOAD', cx + cw - 4 - bw, by, bw, bh, armed('load'), function () {
          if (!armed('load')) { slotArm = { n: sl.n, act: 'load', until: tickN + 45 }; return; }
          slotArm = null; var ok = Native.loadSlot(sl.n); optMsg = ok ? 'LOADED ' + sl.n : 'FAILED'; optMsgT = tickN + 40;
        });
      }
    });
    y += Math.ceil(slots.length / cols) * (ch + gap);
    y += 6;
    // --- map
    G.text(ctx, 'MAP', x, y, C.orange); y += 14;
    optionV(vis, 'ALL ROOMS', opts.reveal ? 'ON' : 'OFF', x, y, p.w, function () { opts.reveal = !opts.reveal; save('md.opts', opts); }); y += 14;
    optionV(vis, 'GUIDE', opts.guide ? 'ON' : 'OFF', x, y, p.w, function () { opts.guide = !opts.guide; save('md.opts', opts); }); y += 14;
    optionV(vis, 'MAP ZOOM', opts.zoom === 'whole' ? 'WHOLE' : 'CLOSE', x, y, p.w, function () { opts.zoom = opts.zoom === 'whole' ? 'near' : 'whole'; mapPan.x = mapPan.y = 0; save('md.opts', opts); }); y += 14;
    optionV(vis, 'ITEMS', opts.markers.toUpperCase(), x, y, p.w, function () {
      opts.markers = { explored: 'all', all: 'off', off: 'explored' }[opts.markers]; save('md.opts', opts);
    }); y += 14;
    optionV(vis, confirmClear > tickN ? 'TAP AGAIN' : 'CLEAR MAP', '', x, y, p.w, function () {
      if (confirmClear > tickN) { for (var i = 0; i < 1024; i++) visited[i] = '.'; store.setItem('md.visited', visited.join('')); confirmClear = 0; }
      else confirmClear = tickN + 45;
    }); y += 14;
    // --- 3D
    if (DUAL && Native.settings3d) {
      var s3 = {}; try { s3 = JSON.parse(Native.settings3d()); } catch (e) {}
      var set3 = function (k, v) { Native.set3d(k, String(v)); };
      y += 6; G.text(ctx, '3D VIEW', x, y, C.orange); y += 14;
      optionV(vis, '3D', s3.dev3d ? 'ON' : 'OFF', x, y, p.w, function () { set3('dev3d', !s3.dev3d); }); y += 14;
      optionV(vis, 'CAMERA', String(s3.cam3d || 'tilt').toUpperCase(), x, y, p.w, function (d) {
        var l = ['tilt', 'diorama', 'side', 'low', 'far'], i = l.indexOf(s3.cam3d || 'tilt');
        set3('cam3d', l[(i + (d < 0 ? l.length - 1 : 1)) % l.length]); }); y += 14;
      optionV(vis, 'DEPTH  - +', String(s3.depth3d || 16), x, y, p.w, function (d) {
        set3('depth3d', Math.max(4, Math.min(48, (s3.depth3d || 16) + (d < 0 ? -4 : 4)))); }); y += 14;
      optionV(vis, 'BORDERS', s3.border3d !== false ? 'ON' : 'OFF', x, y, p.w, function () { set3('border3d', !(s3.border3d !== false)); }); y += 14;
      optionV(vis, 'BACKDROP', s3.cave3d !== false ? 'ON' : 'OFF', x, y, p.w, function () { set3('cave3d', !(s3.cave3d !== false)); }); y += 14;
    }
    y += 6;
    if (RA) {
      y += 6; G.text(ctx, 'RETROARCH', x, y, C.orange); y += 14;
      var host = Native.raHost();
      if (Native.raSetHost) { optionV(vis, 'HOST', host.length > 16 ? host.substr(0, 16) : host, x, y, p.w, function () { Native.raSetHost(); }); y += 14; }
      else { G.text(ctx, 'HOST', x, y, C.white); G.textR(ctx, host, x + p.w - 16, y, C.blue); y += 14; }
      if (Native.pickRom) { optionV(vis, 'CHANGE ROM', '', x, y, p.w, function () { Native.pickRom(); }); y += 14; }
    }
    if (DUAL) { optionV(vis, RA ? 'RETROARCH MENU' : 'PAUSE GAME', '', x, y, p.w, function () { Native.menu(); }); y += 14; }
    if (DUAL && Native.overlay && Native.overlay()) { optionV(vis, 'BACK TO GAME', '', x, y, p.w, function () { Native.close(); }); y += 14; }
    G.text(ctx, (RA ? 'METROID LINK ' : 'METROID DUAL ') + (DUAL ? Native.version() : ''), x, y + 4, C.grey);
    y += 16;
    optContentH = y - y0;
    ctx.restore();
    // scroll bar
    var maxS = Math.max(0, optContentH - p.h + 8);
    if (optScroll > maxS) optScroll = maxS;
    if (maxS > 0) {
      var th = Math.max(12, (p.h - 4) * (p.h - 4) / (optContentH + 8)), ty = p.y + 2 + (p.h - 4 - th) * optScroll / maxS;
      ctx.fillStyle = C.navy; ctx.fillRect(p.x + p.w - 4, p.y + 2, 2, p.h - 4);
      ctx.fillStyle = C.blue; ctx.fillRect(p.x + p.w - 4, Math.round(ty), 2, Math.round(th));
    }
  }
  function optionV(vis, label, value, x, y, w, fn) { if (vis(y)) option(label, value, x, y, w, fn); else { G.text(ctx, label, x, y, C.white); if (value) G.textR(ctx, value, x + w - 16, y, C.blue); } }
  function option(label, value, x, y, w, fn) {
    G.text(ctx, label, x, y, C.white);
    if (value) G.textR(ctx, value, x + w - 16, y, C.blue);
    buttons.push({ x: x - 4, y: y - 3, w: w - 8, h: 14, fn: fn });
  }
  function bigButton(label, x, y, w, h, hot, fn) {
    ctx.fillStyle = hot ? '#3a1800' : C.navy; ctx.fillRect(x, y, w, h);
    ctx.fillStyle = hot ? C.orange : C.blue; ctx.fillRect(x, y, w, 1); ctx.fillRect(x, y + h - 1, w, 1);
    G.textC(ctx, label, x + w / 2, y + Math.floor((h - 7) / 2), C.white);
    buttons.push({ x: x, y: y, w: w, h: h, fn: fn });
  }
  // save-state screenshots, cached by slot + save time so a fresh save reloads its picture
  var thumbCache = {};
  function slotThumb(sl) {
    var k = sl.n + ':' + sl.time, c = thumbCache[k];
    if (c !== undefined) return c;
    thumbCache[k] = null;
    var url = DUAL && Native.thumb ? Native.thumb(sl.n) : '';
    if (url) { var img = new Image(); img.onload = function () { thumbCache[k] = img; draw(); }; img.src = url; }
    return null;
  }
  function button(label, x, y, w, fn) {
    ctx.fillStyle = C.navy; ctx.fillRect(x, y - 3, w, 13);
    ctx.fillStyle = C.blue; ctx.fillRect(x, y - 3, w, 1); ctx.fillRect(x, y + 9, w, 1);
    G.textC(ctx, label, x + w / 2, y, C.white);
    buttons.push({ x: x, y: y - 3, w: w, h: 13, fn: fn });
  }

  // ------------------------------------------------------------------ tabs
  var TAB_NAMES = { status: 'STATUS', map: 'MAP', password: 'PASSWORD', options: 'OPTIONS' };
  function drawTabs() {
    var y = layout.tabY, w = Math.floor(W / tabs.length);
    ctx.fillStyle = C.navy; ctx.fillRect(0, y, W, 1);
    tabs.forEach(function (t, i) {
      var sel = t === tab, x = i * w;
      if (sel) { ctx.fillStyle = '#0c1450'; ctx.fillRect(x, y + 1, w, H - y - 1); ctx.fillStyle = C.orange; ctx.fillRect(x + 4, H - 2, w - 8, 1); }
      var name = TAB_NAMES[t]; if (name.length * 8 > w - 4) name = name.substr(0, Math.floor((w - 4) / 8));
      G.textC(ctx, name, x + w / 2, y + 4, sel ? C.white : C.lgrey);
      buttons.push({ x: x, y: y, w: w, h: H - y, fn: function () { tab = t; } });
    });
  }

  var optDrag = null;
  function inOptions(x, y) { var p = optPane; return tab === 'options' && p && x >= p.x && x < p.x + p.w && y >= p.y && y < p.y + p.h; }
  function tapAt(x, y) {
    for (var i = buttons.length - 1; i >= 0; i--) {
      var b = buttons[i];
      if (x >= b.x && x < b.x + b.w && y >= b.y && y < b.y + b.h) { b.fn(x - b.x < b.w / 3 ? -1 : 1); draw(); return; }
    }
  }
  cv.addEventListener('pointermove', function (e) {
    if (!optDrag) return;
    var y = e.clientY * H / innerHeight, dy = y - optDrag.y;
    if (Math.abs(dy) > 4) optDrag.moved = true;
    if (optDrag.moved) { optScroll = Math.max(0, optDrag.s - dy); draw(); }
  });
  function inMap(x, y) { var b = mapBox; return b && x >= b.x && x < b.x + b.w && y >= b.y && y < b.y + b.h; }
  cv.addEventListener('pointermove', function (e) {
    if (!mapDrag || e.pointerId !== mapDrag.id) return;
    var x = e.clientX * W / innerWidth, y = e.clientY * H / innerHeight, dx = x - mapDrag.x, dy = y - mapDrag.y;
    if (!mapDrag.moved && Math.abs(dx) + Math.abs(dy) > 5) mapDrag.moved = true;
    if (mapDrag.moved && mapDrag.near) { mapPan.x = mapDrag.px + dx; mapPan.y = mapDrag.py + dy; draw(); }
  });
  cv.addEventListener('pointerup', function (e) {
    if (mapDrag && e.pointerId === mapDrag.id) { var md = mapDrag; mapDrag = null; if (!md.moved) { toggleZoom(); draw(); } return; }
    if (!optDrag) return;
    var d = optDrag; optDrag = null;
    if (!d.moved) tapAt(e.clientX * W / innerWidth, e.clientY * H / innerHeight);
  });
  cv.addEventListener('pointerdown', function (e) {
    var x = e.clientX * W / innerWidth, y = e.clientY * H / innerHeight;
    if (!report && inOptions(x, y)) { optDrag = { y: y, s: optScroll, moved: false }; return; }
    if (!report && inMap(x, y)) {
      mapDrag = { id: e.pointerId, x: x, y: y, px: mapPan.x, py: mapPan.y, moved: false, near: mapBox.near };
      try { cv.setPointerCapture(e.pointerId); } catch (err) {}
      return;
    }
    for (var i = buttons.length - 1; i >= 0; i--) {
      var b = buttons[i];
      if (x >= b.x && x < b.x + b.w && y >= b.y && y < b.y + b.h) { b.fn(x - b.x < b.w / 3 ? -1 : 1); draw(); return; }
    }
  });

  resize();
  loadRom();
  setInterval(tick, 66);
})();
