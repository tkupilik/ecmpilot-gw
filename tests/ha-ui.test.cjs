// Exercise the real embedded MQTT editor without a broker or vehicle.
const {test}=require('node:test');
const assert=require('node:assert/strict');
const fs=require('node:fs');
const vm=require('node:vm');
const path=require('node:path');
const script=[...fs.readFileSync(path.join(__dirname,'../main/web/index.html'),'utf8').matchAll(/<script>([\s\S]*?)<\/script>/g)].map(x=>x[1]).find(x=>x.includes('/* Home Assistant settings */'));
async function page(){
  const ids=Object.fromEntries(['enabled','host','port','period','username','password','clear-password','save','reload','message','status'].map(x=>['ha-'+x,{value:'',checked:false,textContent:'',disabled:false}]));
  ids['panel-ha']={hidden:false};
  let snapshot={rev:1,enabled:false,host:'ha.lan',port:1883,username:'pilot',has_password:true,period_s:1,connected:false,last_error:'ESP_OK'},failure=null,interval;
  const requests=[];
  const fetch=async(url,options)=>{
    assert.equal(url,'/api/ha');requests.push({url,options});
    if(options?.method==='POST'){
      if(failure)return{ok:false,text:async()=>failure};
      const p=JSON.parse(options.body);snapshot={...snapshot,...p,rev:snapshot.rev+1};
      if('password' in p)snapshot.has_password=!!p.password;
      delete snapshot.password;
    }
    return{ok:true,json:async()=>({...snapshot})};
  };
  vm.runInNewContext(script,{document:{getElementById:x=>ids[x]},fetch,onTabChange:()=>{},setInterval:fn=>{interval=fn;}});
  const flush=()=>new Promise(r=>setImmediate(r));await flush();
  return {ids,requests,flush,poll:async()=>{interval();await flush();},snapshot:x=>{snapshot={...snapshot,...x};},fail:x=>{failure=x;}};
}
test('MQTT reads are passive and saving an unchanged password omits the secret',async()=>{
  const p=await page(),e=x=>p.ids['ha-'+x];
  assert.equal(e('enabled').checked,false);assert.equal(e('host').value,'ha.lan');
  assert.equal(e('password').value,'');assert.match(e('password').placeholder,/Saved/);
  await p.poll();assert.ok(p.requests.every(x=>x.options?.method!=='POST'));
  e('enabled').checked=true;e('enabled').onchange();await e('save').onclick();
  const sent=JSON.parse(p.requests.at(-1).options.body);
  assert.deepEqual(sent,{rev:1,enabled:true,host:'ha.lan',port:1883,period_s:1,username:'pilot'});
  assert.match(e('message').textContent,/saved/);assert.equal(e('save').disabled,true);
});
test('explicit replacement and removal of password, with no returned secret',async()=>{
  const p=await page(),e=x=>p.ids['ha-'+x];
  e('password').value='new-secret';e('password').oninput();await e('save').onclick();
  assert.equal(JSON.parse(p.requests.at(-1).options.body).password,'new-secret');
  assert.equal(e('password').value,'');assert.doesNotMatch(e('message').textContent,/new-secret/);
  e('clear-password').checked=true;e('clear-password').onchange();await e('save').onclick();
  assert.equal(JSON.parse(p.requests.at(-1).options.body).password,'');
  assert.equal(e('password').placeholder,'Password');
});
test('polling preserves drafts and conflicts require explicit reload',async()=>{
  const p=await page(),e=x=>p.ids['ha-'+x];
  e('host').value='edited.lan';e('host').oninput();p.snapshot({rev:2,host:'other.lan'});
  await p.poll();assert.equal(e('host').value,'edited.lan');
  p.fail('settings_conflict');await e('save').onclick();
  assert.equal(JSON.parse(p.requests.at(-1).options.body).rev,1);
  assert.match(e('message').textContent,/changed elsewhere/);assert.equal(e('host').value,'edited.lan');
  await e('reload').onclick();assert.equal(e('host').value,'other.lan');
});
test('invalid broker and numeric bounds never send a save',async()=>{
  const p=await page(),e=x=>p.ids['ha-'+x];
  for(const [field,value]of[['host','mqtt://ha:1883'],['port','0'],['port','65536'],['period','0'],['period','61'],['period','2.5']]){
    e('host').value='ha.lan';e('port').value='1883';e('period').value='1';e(field).value=value;e(field).oninput();
    const before=p.requests.length;await e('save').onclick();assert.equal(p.requests.length,before);
  }
});
