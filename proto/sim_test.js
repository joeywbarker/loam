const C = require("./core.js");
const rivals = [{lin:1,nx:16,ny:30},{lin:2,nx:32,ny:36},{lin:3,nx:22,ny:46}];
const you = {lin:4,nx:40,ny:66};
function run(label, youMult, ticks){
  C.newWorld(12345, [...rivals.map(r=>({...r})), {...you}]);
  let lo={}, hi={};
  for(let t=1;t<=ticks;t++){
    const tendingRivals = (t%900)<400;           // rivals tend ~45% of the time
    C.tick({1:tendingRivals?3:0, 2:tendingRivals?2:0, 3:tendingRivals?1.5:0, 4:youMult(t)});
    if(t>300){ const w=C.counts().wisps; for(const l of [1,2,3,4]){ lo[l]=Math.min(lo[l]??1e9,w[l]||0); hi[l]=Math.max(hi[l]||0,w[l]||0);} }
  }
  const c=C.counts(); const total=C.state().ws.filter(w=>w.alive).length;
  console.log(label.padEnd(26), "wisps", JSON.stringify(c.wisps), "ground", JSON.stringify(c.ground), "range4", lo[4]+"-"+hi[4], "total", total);
}
const t0=Date.now();
run("you never tend", t=>0, 6000);
run("you tend 25% (x1)", t=>(t%1200)<300?1:0, 6000);
run("you tend 25% (x3 streak)", t=>(t%1200)<300?3:0, 6000);
run("you tend constantly x3", t=>3, 6000);
console.log("ms", Date.now()-t0);
