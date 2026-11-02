const fs=require('fs'), R=require('../../app/assets/link/mroom.js');
const rom=new Uint8Array(fs.readFileSync('/home/claude/test/metroid.nes'));
for (let a=0;a<5;a++){
  const chr=R.chr(rom,a), A=R.area(rom,a), empty=t=>{for(let i=0;i<16;i++) if(chr[0x1000+t*16+i]) return false; return true;};
  const cnt={};
  for (let n=0;n<A.roomCount;n++){ const r=R.room(rom,a,n); for(let i=0;i<0x3C0;i++){ const t=r.nt[i]; if(t!==0xFF && empty(t)) { const k=t.toString(16); (cnt[k]=cnt[k]||new Set()).add(n);} } }
  console.log(R.AREAS[a], Object.entries(cnt).map(([k,v])=>k+':'+[...v].slice(0,6).join('/')).join('  '));
}
