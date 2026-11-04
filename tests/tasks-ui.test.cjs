// SPDX-License-Identifier: BSD-2-Clause
const {test}=require('node:test');
const assert=require('node:assert/strict');
const fs=require('node:fs');const path=require('node:path');const vm=require('node:vm');
const source=fs.readFileSync(path.join(__dirname,'../main/web/index.html'),'utf8');
const matches=[...source.matchAll(/<script>([\s\S]*?)<\/script>/g)].filter(match=>match[1].includes("getElementById('tasks-'+id)"));
assert.equal(matches.length,1,'The page must include exactly one tasks controller');
const script=matches[0][1];
const tick=()=>new Promise(resolve=>setImmediate(resolve));
class Element {
  constructor(){this.checked=true;this.disabled=false;this.textContent='';this.children=[];}
  append(child){this.children.push(child);}
  replaceChildren(){this.children=[];}
}
const task=(id,name,core,runtime_us,idle_core=null)=>({id,name,core,runtime_us,idle_core,state:'blocked',priority:4,base_priority:3,stack_min_free_bytes:1536,stack_base:'0x3fc90100'});
async function page(){
  const ids=Object.fromEntries(['auto','refresh','dump','summary','message','rows','heap'].map(id=>[id,new Element()]));
  const panel={hidden:true};let select,interval,fail=false;const requests=[];
  const data={boot:'a',sampled_us:5000000000,cores:2,tasks:[task(1,'IDLE0',0,3000000000,0),task(2,'IDLE1',1,3000000000,1),task(3,'network',0,1000000000),task(4,'floating',null,1000000000)]};
  const context=vm.createContext({document:{getElementById:id=>id==='panel-tasks'?panel:ids[id.replace('tasks-','')],createElement:()=>new Element()},
    onTabChange:fn=>{select=fn;},setInterval:(fn,ms)=>{assert.equal(ms,2000);interval=fn;},fetch:async url=>{
      requests.push(url);assert.equal(url,'/api/tasks');return {ok:!fail,text:async()=>'Disconnected',json:async()=>structuredClone(data)};
    }});
  vm.runInContext(script,context);await tick();
  return {ids,data,requests,fail:()=>{fail=true;},select:async tab=>{panel.hidden=tab!=='tasks';select(tab);await tick();},poll:async()=>{interval();await tick();},refresh:async()=>{await ids.refresh.onclick();await tick();},row:name=>ids.rows.children.find(row=>row.children[0].textContent===name)?.children.map(cell=>cell.textContent)};
}
test('first sample shows CPU time, affinity and byte watermark without invented load',async()=>{
  const p=await page();await p.select('tasks');
  const row=p.row('network');assert.equal(row[4],'CPU0');assert.equal(row[5],'1000.000');assert.equal(row[6],'--');assert.equal(row[7],'1536');assert.equal(row[8],'0x3fc90100');
  assert.equal(p.row('floating')[4],'Any');assert.match(p.ids.summary.textContent,/CPU0: --/);
});
test('uses wall time per core and averages core load for total, beyond 32-bit uptime',async()=>{
  const p=await page();await p.select('tasks');p.data.sampled_us+=2000000;
  p.data.tasks[0].runtime_us+=1500000;p.data.tasks[1].runtime_us+=500000;p.data.tasks[2].runtime_us+=500000;
  await p.poll();assert.equal(p.row('network')[6],'25.0%');assert.match(p.ids.summary.textContent,/CPU0: 25.0% · CPU1: 75.0% · total load: 50.0%/);
});
test('new tasks, counter resets and reboot get a fresh baseline',async()=>{
  const p=await page();await p.select('tasks');p.data.sampled_us+=2000000;
  p.data.tasks[2].runtime_us=1;p.data.tasks.push(task(5,'new task',1,500000));await p.poll();
  assert.equal(p.row('network')[6],'--');assert.equal(p.row('new task')[6],'--');
  p.data.boot='b';p.data.sampled_us=100000;await p.poll();assert.equal(p.row('network')[6],'--');assert.match(p.ids.message.textContent,/restarted/);
});
test('polls only while visible/enabled; re-entry starts a current sampling window',async()=>{
  const p=await page();await p.poll();assert.equal(p.requests.length,0);await p.select('tasks');
  p.ids.auto.checked=false;await p.poll();assert.equal(p.requests.length,1);
  await p.refresh();assert.equal(p.requests.length,2);await p.select('home');p.ids.auto.checked=true;await p.poll();assert.equal(p.requests.length,2);
  await p.select('tasks');assert.equal(p.row('network')[6],'--');
});
test('HTTP failure preserves the last table and re-enables Refresh',async()=>{
  const p=await page();await p.select('tasks');const row=p.row('network');p.fail();await p.refresh();
  assert.deepEqual(p.row('network'),row);assert.equal(p.ids.message.textContent,'Disconnected');assert.equal(p.ids.refresh.disabled,false);
});

test('heap views render capability groups, zero PSRAM and refreshed largest blocks',async()=>{
  const p=await page();
  p.data.heap=[{name:'Internal RAM',total_bytes:300000,allocated_bytes:200000,free_bytes:100000,minimum_free_bytes:90000,largest_free_block_bytes:64000,free_blocks:7},
    {name:'PSRAM',total_bytes:0,allocated_bytes:0,free_bytes:0,minimum_free_bytes:0,largest_free_block_bytes:0,free_blocks:0}];
  await p.select('tasks');
  assert.equal(p.ids.heap.children[0].children[5].textContent,'64,000');
  assert.equal(p.ids.heap.children[1].children[2].textContent,'0');
  p.data.heap[0].largest_free_block_bytes=32000;await p.poll();
  assert.equal(p.ids.heap.children[0].children[5].textContent,'32,000');
});
