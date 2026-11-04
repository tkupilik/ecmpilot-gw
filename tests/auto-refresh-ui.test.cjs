const {test}=require('node:test');
const assert=require('node:assert/strict');
const fs=require('node:fs');
const vm=require('node:vm');
const path=require('node:path');
const script=[...fs.readFileSync(path.join(__dirname,'../main/web/index.html'),'utf8').matchAll(/<script>([\s\S]*?)<\/script>/g)].map(x=>x[1]).find(x=>x.includes('/* Automatic refresh settings */'));
const initial={rev:1,enabled:false,standby_min:60,charging_min:5,min_aux_soc_pct:50,state:'disabled',remaining_ms:0,capable:true,config_valid:true};
async function page(){
  const ids=Object.fromEntries(['enabled','standby','charging','soc','save','reload','message','status','limit-enabled','limit-soc','limit-value','limit-status'].map(x=>['auto-refresh-'+x,{value:'',checked:false,textContent:'',disabled:false}]));
  ids['panel-home']={hidden:false};
  let snapshot={...initial},failure=null,interval;
  const requests=[];
  const fetch=async(url,options)=>{
    assert.equal(url,'/api/auto-refresh');requests.push({url,options});
    if(options){
      if(failure)return{ok:false,text:async()=>failure};
      snapshot={...snapshot,...JSON.parse(options.body),rev:snapshot.rev+1};
    }
    return{ok:true,json:async()=>({...snapshot})};
  };
  vm.runInNewContext(script,{document:{getElementById:x=>ids[x]},fetch,onTabChange:()=>{},setInterval:fn=>{interval=fn;}});
  const flush=()=>new Promise(r=>setImmediate(r));await flush();
  return {ids,requests,flush,poll:async()=>{interval();await flush();},snapshot:x=>{snapshot={...snapshot,...x};},fail:x=>{failure=x;}};
}
test('defaults, passive polling, and full explicit settings payload',async()=>{
  const p=await page(),e=x=>p.ids['auto-refresh-'+x];
  assert.equal(e('enabled').checked,false);assert.equal(e('standby').value,'60');assert.equal(e('charging').value,'5');assert.equal(e('soc').value,'50');
  await p.poll();assert.ok(p.requests.every(x=>!x.options));
  e('enabled').checked=true;e('enabled').onchange();e('standby').value='15';e('standby').oninput();
  await e('save').onclick();
  assert.deepEqual(JSON.parse(p.requests.at(-1).options.body),{rev:1,enabled:true,standby_min:15,charging_min:5,min_aux_soc_pct:50,charge_limit_enabled:false,charge_limit_soc_pct:80});
  assert.match(e('message').textContent,/saved/);
});
test('polling retains drafts and their base revision; conflict requires reload',async()=>{
  const p=await page(),e=x=>p.ids['auto-refresh-'+x];
  e('standby').value='20';e('standby').oninput();p.snapshot({rev:2,standby_min:10,state:'low_aux_battery'});
  await p.poll();assert.equal(e('standby').value,'20');assert.match(e('status').textContent,/below the limit/);
  p.fail('settings_conflict');await e('save').onclick();
  assert.equal(JSON.parse(p.requests.at(-1).options.body).rev,1);assert.match(e('message').textContent,/changed elsewhere/);
  assert.equal(e('standby').value,'20');await e('reload').onclick();
  assert.equal(e('standby').value,'10');
});
test('invalid inputs cannot write and an active save freezes the editor',async()=>{
  const p=await page(),e=x=>p.ids['auto-refresh-'+x];
  for(const [name,value]of[['standby','0'],['charging','1441'],['soc','101'],['soc','2.5']]){
    e('standby').value='60';e('charging').value='5';e('soc').value='50';e(name).value=value;
    e(name).oninput();const before=p.requests.length;await e('save').onclick();assert.equal(p.requests.length,before);
  }
  e('soc').value='55';e('soc').oninput();const saving=e('save').onclick();
  assert.equal(e('standby').disabled,true);assert.equal(e('save').disabled,true);await saving;
  assert.equal(e('standby').disabled,false);
});

test('charge limit requires refresh, preserves target and is cleared when refresh is disabled',async()=>{
  const p=await page(),e=x=>p.ids['auto-refresh-'+x];
  assert.equal(e('limit-enabled').disabled,true);assert.equal(e('limit-soc').disabled,true);
  e('enabled').checked=true;e('enabled').onchange();assert.equal(e('limit-enabled').disabled,false);
  e('limit-enabled').checked=true;e('limit-enabled').onchange();assert.equal(e('limit-soc').disabled,false);
  e('limit-soc').value='85';e('limit-soc').oninput();await e('save').onclick();
  let sent=JSON.parse(p.requests.at(-1).options.body);
  assert.equal(sent.charge_limit_enabled,true);assert.equal(sent.charge_limit_soc_pct,85);
  e('enabled').checked=false;e('enabled').onchange();assert.equal(e('limit-enabled').checked,false);
  assert.equal(e('limit-enabled').disabled,true);await e('save').onclick();
  sent=JSON.parse(p.requests.at(-1).options.body);assert.equal(sent.charge_limit_enabled,false);
  assert.equal(sent.charge_limit_soc_pct,85);
});
