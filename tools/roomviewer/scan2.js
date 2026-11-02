const fs=require('fs'), R=require('../../app/assets/link/mroom.js');
const rom=new Uint8Array(fs.readFileSync('/home/claude/test/metroid.nes'));
for (let a=0;a<5;a++){
  const chr=R.chr(rom,a), A=R.area(rom,a), pat=t=>Array.from(chr.slice(0x1000+t*16,0x1010+t*16)).join(',');
  const solidPats=new Map(); for(let t=0;t<0xA0;t++) solidPats.set(pat(t), t);
  const used={};
  for (let n=0;n<A.roomCount;n++){ const r=R.room(rom,a,n); for(let i=0;i<0x3C0;i++){ const t=r.nt[i]; if(t>=0xA0 && t!==0xFF) (used[t]=used[t]||new Set()).add(n);} }
  const keys=Object.keys(used).map(Number).sort((x,y)=>x-y);
  console.log(R.AREAS[a], keys.map(t=>t.toString(16)+(solidPats.has(pat(t))?'='+solidPats.get(pat(t)).toString(16):'')+'('+used[t].size+')').join(' '));
}
