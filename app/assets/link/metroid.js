/* metroid.js - Metroid (USA) data from RAM and ROM. Pure functions; used by the Link page and the tests. */
(function (root) {
  var M = {};
  M.AREAS = ['BRINSTAR', 'NORFAIR', 'KRAID', 'TOURIAN', 'RIDLEY'];
  M.ROM = { font: 0x1B4D0, sprites: 0x18010, worldMap: 0x254E, itemData: 0x1039, frames: 0x461B, places: 0x46EF, bank1: 0x4010 };
  // RAM reads polled each tick: [name, addr, len]
  M.SPEC = [['mode', 0x1D, 2], ['frame', 0x2D, 1], ['map', 0x4F, 2], ['area', 0x74, 1], ['mbHits', 0x99, 1], ['health', 0x106, 2], ['escape', 0x10A, 2],
            ['stats', 0x6877, 10], ['ending', 0x6883, 1], ['endType', 0x6872, 1], ['unique', 0x6886, 0x77], ['bailey', 0x69B3, 1]];
  M.specString = function () { return M.SPEC.map(function (s) { return s[1].toString(16) + ':' + s[2]; }).join(','); };
  M.parse = function (hex) {
    var o = {}, p = 0;
    M.SPEC.forEach(function (s) {
      var a = [];
      for (var i = 0; i < s[2]; i++, p += 2) a.push(parseInt(hex.substr(p, 2), 16) || 0);
      o[s[0]] = a;
    });
    return o;
  };
  // Gear bits
  M.GEAR = [
    { bit: 0x10, name: 'MORPH BALL', tiles: [0x7D, 0x7E, 0x8D, 0x8E] },
    { bit: 0x01, name: 'BOMBS', tiles: [0x86, 0x87, 0x96, 0x97] },
    { bit: 0x02, name: 'HIGH JUMP', tiles: [0x7B, 0x7C, 0x8B, 0x8C] },
    { bit: 0x08, name: 'SCREW ATTACK', tiles: [0x80, 0x81, 0x90, 0x91] },
    { bit: 0x20, name: 'VARIA', tiles: [0x82, 0x83, 0x92, 0x93] },
    { bit: 0x04, name: 'LONG BEAM', tiles: [0x88, 0x67, 0x98, 0x99] },
    { bit: 0x80, name: 'ICE BEAM', tiles: [0x88, 0x68, 0x98, 0x99] },
    { bit: 0x40, name: 'WAVE BEAM', tiles: [0x88, 0x89, 0x98, 0x99] }];
  M.TANK_TILES = [0x84, 0x85, 0x94, 0x95];
  M.MISSILE_TILES = [0x3F, 0x3F | 0x100, 0x4F, 0x4F | 0x100];   // 0x100 = horizontal flip
  M.ITEM_KIND = { 0: 'BOMBS', 1: 'HIGH JUMP', 3: 'SCREW ATTACK', 4: 'MORPH BALL', 5: 'VARIA', 8: 'ENERGY TANK', 9: 'MISSILES', 10: 'DOOR', 14: 'MOTHER BRAIN' };

  /** Item table from ROM: 60 two-byte entries (the game searches 60, the table holds 59). */
  M.items = function (rom) {
    var list = [];
    for (var i = 0; i < 60; i++) {
      var lo = rom[M.ROM.itemData + i * 2], hi = rom[M.ROM.itemData + i * 2 + 1], w = lo | hi << 8;
      list.push({ i: i, lo: lo, hi: hi, type: w >> 10, x: (w >> 5) & 31, y: w & 31 });
    }
    return list;
  };
  M.worldMap = function (rom) { return rom.slice(M.ROM.worldMap, M.ROM.worldMap + 1024); };

  /** Which ItemData entries are in the unique item history (same walk as the game's ProcessUniqueItems). */
  M.collected = function (ram, items) {
    var n = ram.unique[0], hist = ram.unique.slice(1), got = {}, y = 0;
    do {
      var lo = hist[y], hi = hist[y + 1];
      y += 2;
      for (var x = 0; x < 60; x++) if (items[x].lo === lo && items[x].hi === hi) { got[x] = true; break; }
    } while (y < n && y < hist.length);
    return got;
  };

  M.energy = function (ram) {
    var lo = ram.health[0], hi = ram.health[1];
    return { en: (hi & 15) * 10 + (lo >> 4), full: hi >> 4, tanks: Math.min(6, ram.stats[0]) };
  };
  M.status = function (ram) {
    var s = ram.stats;
    return { tanks: s[0], gear: s[1], missiles: s[2], maxMissiles: s[3], kraid: s[4], ridley: s[5],
             age: [s[6], s[7], s[8]], bailey: ram.bailey[0] === 1 };
  };
  /** Play time in seconds. Samus' age ticks once every 256 frames (when FrameCount $2D wraps to 0; the low age byte
   *  wraps at $D0), so the frames since the last tick come from FrameCount. */
  M.playSeconds = function (age, frame) { return ((age[2] * 256 * 208 + age[1] * 208 + age[0]) * 256 + (frame || 0)) / 60.0988; };
  /** Ending the game would give now (5 = best) and seconds until it drops a rank. */
  M.ending = function (age, frame) {
    var table = [0x7A, 0x16, 0x0A, 0x04], y = 1;
    if (age[2] === 0) { while (y < 5 && age[1] < table[y - 1]) y++; }
    var next = null;
    if (y > 1) {
      var lim = table[y - 2];
      next = (((lim - age[1]) * 208 - age[0]) * 256 - (frame || 0)) / 60.0988;
    }
    return { rank: y, secondsLeft: next };
  };

  /** The password the game would give right now (CalculatePassword, $8C7A). shift = PasswordByte10 (1..15). */
  M.password = function (ram, items, shift) {
    var P = new Array(18).fill(0), got = M.collected(ram, items);
    for (var k in got) P[k >> 3] |= 1 << (k & 7);
    if (P[7] & 4) { P[7] = 0; P[6] &= 3; }
    var s = ram.stats;
    P[8] = (ram.area[0] & 0x3F) | (ram.bailey[0] ? 0x80 : 0);
    P[9] = s[1]; P[10] = s[2];
    var b = 0;
    if (s[4] & 0x80) b |= 0x80;
    if (s[4] & 0x01) b |= 0x40;
    if (s[5] & 0x80) b |= 0x20;
    if (s[5] & 0x02) b |= 0x10;
    P[15] = b;
    for (var i = 0; i < 4; i++) P[11 + i] = s[6 + i];      // $687D-$6880 -> $6993-$6996
    P[16] = shift & 15 || 1;
    var sum = 0; for (i = 0; i <= 16; i++) sum += P[i]; P[17] = sum & 255;
    for (var r = 0; r < P[16]; r++) {                      // rotate the 128-bit block right by one
      var carry = P[15] & 1;
      for (i = 0; i < 16; i++) { var nc = P[i] & 1; P[i] = (P[i] >> 1) | (carry << 7); carry = nc; }
    }
    var c = [];
    for (i = 0; i < 18; i += 3) {
      c.push(P[i] >> 2, ((P[i] & 3) << 4) | (P[i + 1] >> 4), ((P[i + 1] & 15) << 2) | (P[i + 2] >> 6), P[i + 2] & 63);
    }
    return { bytes: P, chars: c };
  };
  /** Collection totals for the mission report: tanks, missile packs, power-ups (beams count once: long + ice/wave). */
  M.collection = function (ram, items) {
    var got = M.collected(ram, items), st = M.status(ram), tanks = 0, ms = 0, tT = 0, tM = 0;
    items.slice(0, 59).forEach(function (it) { if (it.type === 8) tT++; if (it.type === 9) tM++; });
    for (var k in got) { var it = items[k]; if (it && it.type === 8) tanks++; if (it && it.type === 9) ms++; }
    var g = st.gear, gear = 0;
    [0x10, 0x01, 0x02, 0x08, 0x20, 0x04].forEach(function (b) { if (g & b) gear++; });
    if (g & 0xC0) gear++;
    var have = tanks + ms + gear, total = tT + tM + 7;
    return { tanks: tanks, tanksTotal: tT, missiles: ms, missilesTotal: tM, gear: gear, gearTotal: 7,
             percent: Math.round(have * 100 / total) };
  };
  /**
   * Next-location guide. Goals in a sensible progression order; each is skipped once done, so any play order works.
   * Target rooms come from the item table in the player's ROM (by item type, or by table index for doors).
   * area: where the goal is (0 Brinstar, 1 Norfair, 2 Kraid, 3 Tourian, 4 Ridley).
   */
  M.GOALS = [
    { name: 'MORPH BALL', area: 0, done: function (s) { return s.gear & 0x10; }, type: 4, hint: 'LEFT OF THE START' },
    { name: 'MISSILES', area: 0, done: function (s) { return s.maxMissiles > 0; }, index: 1, hint: 'OPEN RED DOORS' },
    { name: 'LONG BEAM', area: 0, done: function (s) { return s.gear & 0x04; }, index: 2, hint: 'BEHIND A RED DOOR' },
    { name: 'BOMBS', area: 0, done: function (s) { return s.gear & 0x01; }, type: 0, hint: 'BEHIND A RED DOOR' },
    { name: 'ICE BEAM', area: 0, done: function (s) { return s.gear & 0x80; }, index: 7, hint: 'NEEDED FOR TOURIAN' },
    { name: 'VARIA', area: 0, done: function (s) { return s.gear & 0x20; }, type: 5, hint: 'BOMB THE FLOOR' },
    { name: 'HIGH JUMP', area: 1, done: function (s) { return s.gear & 0x02; }, type: 1, hint: 'IN NORFAIR' },
    { name: 'SCREW ATTACK', area: 1, done: function (s) { return s.gear & 0x08; }, type: 3, hint: 'IN NORFAIR' },
    { name: 'KRAID', area: 2, done: function (s) { return M.kraidDown(s); }, index: 41, hint: 'KRAID\'S LAIR' },
    { name: 'RIDLEY', area: 4, done: function (s) { return M.ridleyDown(s); }, index: 47, hint: 'RIDLEY\'S LAIR' },
    // the gate: Brinstar's statue room (room $2B, next to the Tourian elevator), where the statues raise the bridge
    { name: 'TOURIAN GATE', area: 0, done: function (s, ram) { return (ram.area[0] & 15) === 3; }, at: { x: 4, y: 2 }, hint: 'STATUES RAISE THE BRIDGE' },
    // Mother Brain's room: Tourian's special-items entry for her (type 6) at map 02,0B
    { name: 'MOTHER BRAIN', area: 3, done: function (s, ram, got) { return got[58]; }, at: { x: 2, y: 11 }, hint: 'BREAK THE ZEBETITES' }
  ];
  /** Kraid / Ridley defeated. The game sets bit 0 / bit 1 when the boss dies, then overwrites the byte with $80 + a
   *  counter when the statue rises in the gate room ($681B,x with x=$60/$61), which can clear that bit. So a raised
   *  statue (bit 7) also means defeated. */
  M.kraidDown = function (s) { return (s.kraid & 0x81) !== 0; };
  M.ridleyDown = function (s) { return (s.ridley & 0x82) !== 0; };
  M.guide = function (ram, items) {
    var st = M.status(ram), got = M.collected(ram, items);
    if (ram.escape[1] !== 0xFF) return { name: 'ESCAPE', hint: 'GET OUT BEFORE TIME RUNS OUT', target: null, area: 3 };
    for (var i = 0; i < M.GOALS.length; i++) {
      var g = M.GOALS[i];
      if (g.done(st, ram, got)) continue;
      var it = g.at || null;
      if (g.index !== undefined) it = items[g.index];
      else if (g.type !== undefined) for (var k = 0; k < 59; k++) if (items[k].type === g.type) { it = items[k]; break; }
      return { name: g.name, hint: g.hint, area: g.area, target: it && (it.x || it.y) ? { x: it.x, y: it.y } : null };
    }
    return { name: 'MISSION DONE', hint: '', target: null, area: -1 };
  };
  M.PW_CHARS = '0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz?-';
  M.pwText = function (chars) { return chars.map(function (v) { return M.PW_CHARS[v]; }).join(''); };

  /** Sprite frame from the ROM's frame/placement tables (bank 1), as [{x, y, tile, hflip, vflip, pal}]. */
  M.frame = function (rom, n) {
    var b = M.ROM.bank1, w = function (o) { return rom[o] | rom[o + 1] << 8; };
    var fp = b + w(M.ROM.frames + n * 2) - 0x8000, ctrl = rom[fp];
    var place = b + w(M.ROM.places + (ctrl & 15) * 2) - 0x8000;
    var i = fp + 3, pi = 0, dy = 0, dx = 0, c = ctrl, out = [], sb = function (v) { return v > 127 ? v - 256 : v; };
    for (var guard = 0; guard < 64; guard++) {
      var t = rom[i++];
      if (t === 0xFF) break;
      if (t === 0xFC) { dy = sb(rom[i]); dx = sb(rom[i + 1]); i += 2; continue; }
      if (t === 0xFD) { c = rom[i++]; continue; }
      if (t === 0xFE) { pi++; continue; }
      out.push({ x: sb(rom[place + pi * 2 + 1]) + dx, y: sb(rom[place + pi * 2]) + dy, tile: t, hflip: !!(c & 0x40), vflip: !!(c & 0x80), pal: (c >> 4) & 3 });
      pi++;
    }
    return out;
  };
  root.Metroid = M;
  if (typeof module !== 'undefined') module.exports = M;
})(this);
