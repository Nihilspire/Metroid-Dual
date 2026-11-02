const fs=require('fs'), R=require('../../app/assets/link/mroom.js');
const rom=new Uint8Array(fs.readFileSync('/home/claude/test/metroid.nes'));
console.log('check:', R.check(rom)||'ok');
const map=R.worldMap(rom);
function cmp(name, dump, area, mx, my, table) {
  const d=new Uint8Array(fs.readFileSync(dump)), ppu=d.subarray(0x8000);
  const n=map[my*32+mx], r=R.room(rom, area, n), chr=R.chr(rom, area);
  const nt=ppu.subarray(0x2000+table*0x400, 0x2400+table*0x400);
  let diff=0, adiff=0, first=[];
  for (let i=0;i<0x3C0;i++) if (nt[i]!==r.nt[i]) { diff++; if (first.length<8) first.push(`${i>>5},${i&31}: emu ${nt[i].toString(16)} mine ${r.nt[i].toString(16)}`); }
  for (let i=0x3C0;i<0x400;i++) if (nt[i]!==r.nt[i]) adiff++;
  let cdiff=0; for (let i=0x1000;i<0x2000;i++) if (ppu[i]!==chr[i]) cdiff++;
  let sdiff=0; for (let i=0;i<0x1000;i++) if (ppu[i]!==chr[i]) sdiff++;
  console.log(name, 'room', n.toString(16), 'tile diffs', diff, 'attr diffs', adiff, 'bg chr diffs', cdiff, 'spr chr diffs', sdiff, JSON.stringify(r.doors), first.join(' | '));
}
cmp('lava', '/tmp/lava/lava.bin', 1, 0x17, 0x0E, 0);
cmp('hj', '/tmp/lava/hj.bin', 1, 0x1B, 0x11, 3);
cmp('brinstar', '/tmp/lava/br.bin', 0, 0x01, 0x0E, 0);
const A=R.area(rom,1); console.log('norfair', A, Array.from(A.pal).map(x=>x.toString(16)).join(' '));
