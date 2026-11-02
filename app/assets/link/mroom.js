/* mroom.js - Metroid (USA) room decoder: rebuilds a room's nametable, palette and pattern tables from the ROM,
 * the way the game does (SetupRoom/DrawStruct, LoadGFX). Shared by the room viewer page and its tests. */
(function (root) {
  'use strict';
  var R = {};
  R.AREAS = ['BRINSTAR', 'NORFAIR', 'KRAID', 'TOURIAN', 'RIDLEY'];
  var BANK = [1, 2, 4, 3, 5];                         // area -> PRG bank
  // GFXInfo (bank 7, $C6E0): [bank, src, ppu dest, length]
  var GFX = [[6,0x8000,0x0000,0x09A0],[4,0x8D60,0x0000,0x0520],[1,0x8D60,0x1000,0x0400],[6,0x9DA0,0x1000,0x0150],
    [5,0x8D60,0x1200,0x0450],[6,0x9EF0,0x1800,0x0800],[1,0x9160,0x0C00,0x0400],[6,0xA6F0,0x1000,0x0260],[6,0xA950,0x1700,0x0070],
    [2,0x8D60,0x0C00,0x0400],[6,0xA9C0,0x1000,0x02E0],[6,0xACA0,0x1200,0x0600],[6,0xB2A0,0x1900,0x0090],[5,0x91B0,0x1D00,0x0300],
    [2,0x9160,0x0C00,0x0400],[6,0xB330,0x1700,0x00C0],[4,0x9360,0x1E00,0x0200],[3,0x8D60,0x0C00,0x0400],[6,0xB3F0,0x1700,0x00C0],
    [3,0x9160,0x0C00,0x0400],[6,0x89A0,0x0C00,0x0100],[6,0x8BE0,0x1400,0x0500],[6,0x9980,0x1FC0,0x0040],[6,0xB4C0,0x1000,0x0400],
    [6,0xB4C0,0x0A00,0x00A0],[6,0x9980,0x0FC0,0x0040],[6,0xB4C0,0x1D00,0x02A0],[6,0x90E0,0x0000,0x07B0],[6,0x9890,0x1F40,0x0010]];
  // Init<Area>GFX lists (entry 0 = Samus/items sprites, loaded at game start)
  var AREA_GFX = [[0,3,4,5,6,0x19,0x16], [0,4,5,7,8,9,0x19,0x16], [0,4,5,0x0A,0x0F,0x10,0x11,0x19,0x16],
                  [0,5,0x0A,0x0B,0x0C,0x0D,0x0E,0x1A,0x1C,0x19,0x16], [0,4,5,0x0A,0x12,0x13,0x19,0x16]];
  R.WORLD_MAP = 0x254E;

  R.check = function (rom) {
    if (rom.length < 0x20010 || rom[0] !== 0x4E || rom[1] !== 0x45 || rom[2] !== 0x53) return 'not an iNES ROM';
    var c = 0xFFFFFFFF;
    for (var i = 16; i < rom.length; i++) { c ^= rom[i]; for (var k = 0; k < 8; k++) c = (c >>> 1) ^ (0xEDB88320 & -(c & 1)); }
    c = (~c) >>> 0;
    return c === 0x70080810 ? '' : 'not Metroid (USA) (CRC ' + c.toString(16) + ')';
  };
  function off(bank, addr) { return 16 + bank * 0x4000 + ((addr - 0x8000) & 0x3FFF); }
  function w(rom, o) { return rom[o] | rom[o + 1] << 8; }

  // What is already in CHR RAM when an area loads: the title screen, then LoadSamusGFX at game start
  var START_GFX = [0x15, 0, 0x14, 0x17, 0x18, 0x19, 0x16];
  R.chr = function (rom, area) {
    var chr = new Uint8Array(0x2000);
    START_GFX.concat(AREA_GFX[area]).forEach(function (e) {
      var g = GFX[e], s = off(g[0], g[1]);
      for (var i = 0; i < g[3]; i++) chr[(g[2] + i) & 0x1FFF] = rom[s + i];
    });
    return chr;
  };
  R.area = function (rom, a) {
    var b = BANK[a], hdr = off(b, 0x9598);
    var o = { bank: b, spec: w(rom, hdr), rooms: w(rom, hdr + 2), structs: w(rom, hdr + 4), macros: w(rom, hdr + 6) };
    o.roomCount = (o.structs - o.rooms) >> 1;
    // palette: Palette00, a PPU string list (hi, lo, len, data...) ended by 0
    var pal = new Uint8Array(32), p = off(b, w(rom, off(b, 0x9560)));
    for (var guard = 0; guard < 8 && rom[p] === 0x3F; guard++) {
      var dst = rom[p + 1] & 0x1F, n = rom[p + 2]; p += 3;
      for (var i = 0; i < n; i++) pal[(dst + i) & 31] = rom[p + i];
      p += n;
    }
    o.pal = pal;
    return o;
  };
  R.worldMap = function (rom) { return rom.slice(R.WORLD_MAP, R.WORLD_MAP + 1024); };

  /** Builds room n of area a into a 1 KB nametable (32x30 tiles + attributes). Returns {nt, doors, objects}. */
  R.room = function (rom, a, n) {
    var A = R.area(rom, a), b = A.bank, nt = new Uint8Array(0x400).fill(0xFF);
    var rp = off(b, w(rom, off(b, A.rooms) + n * 2)), roomPal = rom[rp] & 3;
    for (var i = 0x3C0; i < 0x400; i++) nt[i] = [0x00, 0x55, 0xAA, 0xFF][roomPal];
    var p = rp + 1, objects = [];
    for (var guard = 0; guard < 200; guard++) {
      var c = rom[p];
      if (c === 0xFF || c === 0xFD) break;
      if (c === 0xFE) { p++; continue; }
      var pos = c, si = rom[p + 1], opal = rom[p + 2] & 3;
      objects.push({ y: pos >> 4, x: pos & 15, struct: si, pal: opal });
      drawStruct(rom, A, nt, ((pos >> 4) * 2) * 32 + (pos & 15) * 2, si, opal, opal !== roomPal);
      p += 3;
    }
    var doors = [], enemies = [], elevator = null, statues = false;
    if (rom[p] === 0xFD) {
      p++;
      for (guard = 0; guard < 40 && rom[p] !== 0xFF; guard++) {
        var t = rom[p] & 0x0F;
        if (t === 1) { enemies.push({ slot: rom[p] >> 4, type: rom[p + 1], y: rom[p + 2] >> 4, x: rom[p + 2] & 15 }); p += 3; }
        else if (t === 2) { var d = rom[p + 1]; doors.push({ left: !!(d & 0x10), kind: d & 3 }); p += 2; }
        else if (t === 4) { elevator = rom[p + 1]; p += 2; }
        else if (t === 6) { statues = true; p += 1; }
        else if (t === 7) { p += 2; }
        else break;
      }
    }
    // LoadDoor: the door's own column of invisible solid tiles ($4E), rows 10-15, column 2 (left) or 29 (right)
    doors.forEach(function (d) { for (var r = 10; r < 16; r++) nt[r * 32 + (d.left ? 2 : 29)] = 0x4E; });
    return { nt: nt, pal: roomPal, objects: objects, doors: doors, enemies: enemies, elevator: elevator, statues: statues };
  };
  function drawStruct(rom, A, nt, start, si, opal, setAttr) {
    var b = A.bank, sp = off(b, w(rom, off(b, A.structs) + si * 2)), row = start;
    for (var guard = 0; guard < 32; guard++) {
      var len = rom[sp]; if (len === 0xFF) return;
      var count = (len & 15) || 16, x = row + (len >> 4) * 2;
      for (var k = 0; k < count; k++) {
        if (x >= 0x3C0) return;
        var m = rom[sp + 1 + k], mp = off(b, A.macros) + m * 4;
        nt[x] = rom[mp]; nt[x + 1] = rom[mp + 1]; nt[x + 32] = rom[mp + 2]; nt[x + 33] = rom[mp + 3];
        if (setAttr) {
          var tx = x & 31, ty = x >> 5, ai = 0x3C0 + (ty >> 2) * 8 + (tx >> 2), sh = ((ty & 2) << 1) | (tx & 2);
          nt[ai] = (nt[ai] & ~(3 << sh)) | (opal << sh);
        }
        x += 2;
        if ((x & 31) === 0) { k = count; break; }        // row ends at the right edge (the game cuts it)
      }
      sp += 1 + count; row += 64;
    }
  }
  /** Solid 2x2 blocks the area's rooms are built from: [{tiles:[4], pal, count}] (the 3D backdrop's material). */
  R.blocks = function (rom, a) {
    var A = R.area(rom, a), b = A.bank, count = new Int32Array(1024), out = [];
    for (var n = 0; n < A.roomCount && n < 128; n++) {
      var p = off(b, w(rom, off(b, A.rooms) + n * 2)) + 1;
      for (var g = 0; g < 200; g++) {
        var c = rom[p];
        if (c === 0xFF || c === 0xFD) break;
        if (c === 0xFE) { p++; continue; }
        var pal = rom[p + 2] & 3, sp = off(b, w(rom, off(b, A.structs) + rom[p + 1] * 2));
        for (var row = 0; row < 32; row++) {
          var len = rom[sp]; if (len === 0xFF) break;
          var cnt = (len & 15) || 16;
          for (var k = 0; k < cnt; k++) count[rom[sp + 1 + k] * 4 + pal]++;
          sp += 1 + cnt;
        }
        p += 3;
      }
    }
    for (var key = 0; key < 1024; key++) {
      if (!count[key]) continue;
      var mp = off(b, A.macros) + (key >> 2) * 4, t = [rom[mp], rom[mp + 1], rom[mp + 2], rom[mp + 3]];
      if (t.every(function (x) { return x < 0xA0; })) out.push({ tiles: t, pal: key & 3, count: count[key] });
    }
    return out;
  };
  /** Special items table of area a: [{x, y, powerups: [{type, x, y}], elevator, palette}] (ScanForItems). */
  R.spec = function (rom, a) {
    var A = R.area(rom, a), b = A.bank, out = [], L = A.spec;
    for (var g1 = 0; g1 < 64 && L !== 0xFFFF; g1++) {
      var lp = off(b, L), y = rom[lp], next = w(rom, lp + 1), p = lp + 3;
      for (var g2 = 0; g2 < 32; g2++) {
        var e = { x: rom[p], y: y, powerups: [], elevator: null, palette: false }, nxt = rom[p + 1], q = p + 2;
        for (var g3 = 0; g3 < 8; g3++) {           // handler chain; stop at an unknown length
          var t = rom[q] & 15;
          if (t === 0) break;
          if (t === 2) { e.powerups.push({ type: rom[q + 1], y: rom[q + 2] >> 4, x: rom[q + 2] & 15 }); q += 3; }
          else if (t === 4) { e.elevator = rom[q + 1]; q += 2; }
          else if (t === 9) { q += 2; }
          else if (t === 1) { q += 3; }
          else if (t === 10) { e.palette = true; q += 1; }
          else break;
        }
        out.push(e);
        if (nxt === 0xFF) break;
        p += nxt;
      }
      L = next;
    }
    return out;
  };
  /** Best guess of the area of every world-map cell (-1 = no room): grown outwards from the cells each area's
   *  special items table names, never into a cell whose room number that area doesn't have. */
  /** Room edges: which of the 32 columns are open at the top/bottom, which rows are open at the sides, doors. */
  R.shape = function (rom, a, n) {
    var r = R.room(rom, a, n), nt = r.nt, open = function (row, col) { var t = nt[row * 32 + col]; return t === 0xFF || t >= 0xA0; };
    var sh = { top: [], bottom: [], left: [], right: [], doorL: -1, doorR: -1 };
    for (var c = 0; c < 32; c++) { sh.top.push(open(0, c) && open(1, c)); sh.bottom.push(open(28, c) && open(29, c)); }
    for (var rr = 0; rr < 30; rr++) { sh.left.push(open(rr, 0)); sh.right.push(open(rr, 31)); }
    r.doors.forEach(function (d) { if (d.left) sh.doorL = d.kind; else sh.doorR = d.kind; });
    return sh;
  };
  /** Which area each world-map cell belongs to (-1: unknown). Grown from each area's special-item cells (items,
   *  elevators, doors). Pass 1 follows real connections only (open edges that line up, or a door on each side), so
   *  what it reaches is certain; `strict` keeps that result. Pass 2 then fills the cells it couldn't reach (shafts
   *  behind breakable blocks etc.) by plain adjacency within the area's room count. */
  R.areaMap = function (rom, strictOnly) {
    var map = R.worldMap(rom), am = new Int8Array(1024).fill(-1), q = [], counts = [], shapes = {}, seeds = [];
    var shapeOf = function (a, n) { var k = a * 256 + n; if (!(k in shapes)) { try { shapes[k] = R.shape(rom, a, n); } catch (e) { shapes[k] = null; } } return shapes[k]; };
    var any = function (x, y) { for (var i = 0; i < x.length; i++) if (x[i] && y[i]) return true; return false; };
    var DIRS = [[1, 0], [-1, 0], [0, 1], [0, -1]];
    for (var a = 0; a < 5; a++) {
      counts[a] = R.area(rom, a).roomCount;
      R.spec(rom, a).forEach(function (e) { var i = e.y * 32 + e.x; if (e.x < 32 && e.y < 32 && map[i] !== 0xFF && map[i] < counts[a] && am[i] < 0) { am[i] = a; q.push(i); } });
    }
    var grow = function (strict) {
      for (var h = 0; h < q.length; h++) {
        var i = q[h], a2 = am[i], x = i & 31, y = i >> 5, A = strict ? shapeOf(a2, map[i]) : null;
        if (strict && !A) continue;
        DIRS.forEach(function (d) {
          var nx = x + d[0], ny = y + d[1]; if (nx < 0 || ny < 0 || nx > 31 || ny > 31) return;
          var j = ny * 32 + nx; if (am[j] >= 0 || map[j] === 0xFF || map[j] >= counts[a2]) return;
          if (strict) {
            var B = shapeOf(a2, map[j]); if (!B) return;
            var ok = d[0] === 1 ? (A.doorR >= 0 && B.doorL >= 0) || (A.doorR < 0 && B.doorL < 0 && any(A.right, B.left))
                   : d[0] === -1 ? (A.doorL >= 0 && B.doorR >= 0) || (A.doorL < 0 && B.doorR < 0 && any(A.left, B.right))
                   : d[1] === 1 ? any(A.bottom, B.top) : any(A.top, B.bottom);
            if (!ok) return;
          }
          am[j] = a2; q.push(j);
        });
      }
    };
    grow(true);
    if (strictOnly) return am;
    q = []; for (var k = 0; k < 1024; k++) if (am[k] >= 0) q.push(k);
    grow(false);
    return am;
  };
  root.MRoom = R;
  if (typeof module !== 'undefined') module.exports = R;
})(this);
