const C = require("./core.js");
const rivals = [{lin:1,nx:15,ny:24},{lin:2,nx:33,ny:30},{lin:3,nx:22,ny:42}];
function run(label, opt){
  C.newWorld(777, [...rivals.map(r=>({...r})), {lin:4,nx:24,ny:66}]);
  const t0=Date.now();
  for(let t=1;t<=opt.ticks;t++){
    const a={}; for(const r of rivals) a[r.lin] = ((t + r.lin*300)%900)<380 ? 1+0.3*(8-2*r.lin) : 0;
    const on = opt.me(t); if(on!==null) a[4]=on;
    if(opt.walls && t%40===0){ const S=C.state(); C.place(10+(t/40)%30|0, 60, C.WALL, 4); }
    C.tick(a);
  }
  const S=C.state(), k=C.counts(), my=S.cols.find(c=>c.lin===4);
  const est = c => C.TRAITS.filter((_,b)=>(c.est>>b)&1).map(x=>x.name).join(",")||"-";
  console.log(`\n== ${label} (${Date.now()-t0} ms)`);
  for(const c of S.cols) console.log(` lin${c.lin} wisps ${k.wisps[c.lin]||0} ground ${k.ground[c.lin]||0} established [${est(c)}] share ${C.traitShare(c.lin).map(x=>x.toFixed(2)).join(" ")} press ${Object.entries(c.press).map(([a,b])=>a[0]+Math.round(b)).join(" ")}`);
  const kinds={}; for(const e of S.events) kinds[e.kind]=(kinds[e.kind]||0)+1;
  console.log(" events:", JSON.stringify(kinds));
  console.log(" sample:", S.events.filter(e=>e.lin===4).slice(-4).map(e=>e.text));
}
run("tended steadily, 20k ticks", {ticks:20000, me:t=>2});
run("builder style (walls), 20k", {ticks:20000, me:t=>2, walls:true});
run("long absences (asleep 70%)", {ticks:20000, me:t=>{ if(t%3000===900) C.pressure(4,"absence",150); return ((t%3000)<900)?2:null; }});
run("many short visits", {ticks:20000, me:t=>{ if(t%300===0) C.pressure(4,"presence",25); return ((t%300)<120)?2:null; }});
