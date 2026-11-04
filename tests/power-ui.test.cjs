// Real embedded power controls; DOM/network boundaries substituted.
const {test}=require('node:test');
const assert=require('node:assert/strict');
const fs=require('node:fs');const vm=require('node:vm');const path=require('node:path');
const html=fs.readFileSync(path.join(__dirname,'../main/web/index.html'),'utf8');
const script=[...html.matchAll(/<script>([\s\S]*?)<\/script>/g)].map(x=>x[1]).find(x=>x.includes("document.getElementById('power-'+id)"));
const tick=()=>new Promise(resolve=>setImmediate(resolve));
async function page(){
  const ids=Object.fromEntries(['status','message','normal','dfs','light','deep','refresh'].map(id=>[id,{disabled:false,textContent:''}]));
  const panel={hidden:false};let interval,select,reject=false;
  const requests=[];let state={mode:0,cpu_min_mhz:160,cpu_max_mhz:160,auto_light_sleep:0,light_sleep_count:0,light_sleep_ms:0,can_enabled:1,can_state:2,requested:0,completed:0,last_error:0,deep_sleep_available:true,deep_stage:0,wake_cause:0};
  const context=vm.createContext({document:{getElementById:id=>id==='panel-low-power'?panel:ids[id.replace('power-','')]},onTabChange:fn=>select=fn,setInterval:fn=>interval=fn,fetch:async(url,options)=>{
    requests.push({url,options});return {ok:!reject,text:async()=>'Unavailable',json:async()=>options?{accepted:true}:state};
  }});
  vm.runInContext(script,context);await tick();
  return {ids,requests,state:next=>state={...state,...next},reject:()=>reject=true,
    select:async name=>{panel.hidden=name!=='low-power';select(name);await tick();},
    poll:async()=>{interval();await tick();}};
}
test('initial policy and actual sleep counters remain distinct',async()=>{
  const p=await page();assert.match(p.ids.status.textContent,/Applied policy: Normal/);
  p.state({mode:2,cpu_min_mhz:40,cpu_max_mhz:80,auto_light_sleep:1});await p.poll();
  assert.match(p.ids.status.textContent,/Successful sleep calls: 0/);
  assert.match(p.ids.message.textContent,/CAN driver prevents actual sleep/);
});
test('manual request uses a dedicated endpoint and waits for completion',async()=>{
  const p=await page();await p.ids.light.onclick();
  const request=p.requests.at(-1);assert.equal(request.url,'/api/power');
  assert.equal(request.options.method,'POST');assert.deepEqual(JSON.parse(request.options.body),{mode:'light_sleep'});
  assert.equal(p.ids.normal.disabled,true);p.state({requested:1,completed:0});await p.poll();
  assert.match(p.ids.message.textContent,/Applying/);assert.equal(p.ids.dfs.disabled,true);
  p.state({requested:1,completed:1});await p.poll();assert.equal(p.ids.normal.disabled,false);
});
test('normal and DFS controls send their own mode names',async()=>{
  for(const [button,mode] of [['normal','normal'],['dfs','dfs']]){
    const p=await page();await p.ids[button].onclick();assert.equal(JSON.parse(p.requests.at(-1).options.body).mode,mode);
  }
});
test('power polling stops when Low power is hidden',async()=>{
  const p=await page();const before=p.requests.length;await p.select('log');await p.poll();
  assert.equal(p.requests.length,before);await p.select('low-power');assert.equal(p.requests.length,before+1);
});
test('request errors preserve controls and report the server response',async()=>{
  const p=await page();p.reject();await p.ids.light.onclick();
  assert.equal(p.ids.light.disabled,false);assert.equal(p.ids.message.textContent,'Unavailable');
});
test('hardware errors are not reported as successful application',async()=>{
  const p=await page();p.state({last_error:257});await p.poll();
  assert.match(p.ids.message.textContent,/failed: 257/);
});

test('manual deep sleep sends CAN-only mode and explains intentional disconnect',async()=>{
  const p=await page();await p.ids.deep.onclick();
  assert.deepEqual(JSON.parse(p.requests.at(-1).options.body),{mode:'can_deep_sleep'});
  assert.match(p.ids.message.textContent,/Wi-Fi will stop/);
  assert.equal(p.ids.deep.disabled,true);
  p.reject();await p.poll();
  assert.match(p.ids.message.textContent,/Check current draw/);
  assert.doesNotMatch(p.ids.message.textContent,/successfully entered/);
});
test('deep sleep is disabled when no CAN wake path is available',async()=>{
  const p=await page();p.state({deep_sleep_available:false});await p.poll();
  assert.equal(p.ids.deep.disabled,true);
  assert.equal(p.ids.normal.disabled,false);
});
test('after CAN wake normal startup re-enables the manual deep sleep control',async()=>{
  const p=await page();await p.ids.deep.onclick();
  p.state({requested:0,completed:0,wake_cause:2});await p.poll();
  assert.equal(p.ids.deep.disabled,false);
  assert.match(p.ids.status.textContent,/wake cause: 2/);
});
