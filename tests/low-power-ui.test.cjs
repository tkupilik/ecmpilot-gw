const {test}=require('node:test');
const assert=require('node:assert/strict');
const fs=require('node:fs');
const vm=require('node:vm');
const path=require('node:path');
const scripts=[...fs.readFileSync(path.join(__dirname,'../main/web/index.html'),'utf8').matchAll(/<script>([\s\S]*?)<\/script>/g)];
const script=scripts.map(s=>s[1]).find(s=>s.includes('/* Low-power settings */'));
async function page(){
 const ids=Object.fromEntries(['enabled','hours','save','reload','status','message'].map(n=>['low-power-'+n,{value:'48',checked:true,disabled:false,textContent:''}]));
 ids['panel-low-power']={hidden:false};
 let snapshot={rev:1,enabled:true,idle_hours:48,capable:true,config_valid:true,state:'counting',remaining_s:172800};
 let failure=null,poll,tab;const requests=[];
 const context=vm.createContext({document:{getElementById:id=>ids[id]},onTabChange:fn=>tab=fn,setInterval:fn=>poll=fn,
  fetch:async(url,options)=>{requests.push({url,options});if(options){if(failure)return {ok:false,text:async()=>failure};const body=JSON.parse(options.body);snapshot={...snapshot,...body,rev:snapshot.rev+1};}return {ok:true,json:async()=>snapshot};}});
 vm.runInContext(script,context);tab('low-power');await new Promise(r=>setImmediate(r));
 return {ids,requests,poll:async()=>{poll();await new Promise(r=>setImmediate(r));},snapshot:s=>snapshot={...snapshot,...s},fail:s=>failure=s};
}
test('two-day countdown and unknown data are distinct; reads do not save',async()=>{
 const p=await page(),e=n=>p.ids['low-power-'+n];
 assert.equal(e('hours').value,'48');assert.match(e('status').textContent,/2d 0h 0m 0s/);
 assert.ok(p.requests.every(r=>!r.options));
 p.snapshot({state:'waiting_data',remaining_s:null});await p.poll();assert.match(e('status').textContent,/first valid BSI/);assert.doesNotMatch(e('status').textContent,/0d/);
});
test('polling preserves drafts and revision; conflict requires reload',async()=>{
 const p=await page(),e=n=>p.ids['low-power-'+n];
 e('hours').value='72';e('hours').oninput();p.snapshot({rev:2,idle_hours:24});await p.poll();assert.equal(e('hours').value,'72');
 p.fail('settings_conflict');await e('save').onclick();assert.equal(JSON.parse(p.requests.at(-1).options.body).rev,1);assert.match(e('message').textContent,/changed elsewhere/);
 e('reload').onclick();await new Promise(r=>setImmediate(r));assert.equal(e('hours').value,'24');
});
test('validates hours and explicitly saves enable and timeout only',async()=>{
 const p=await page(),e=n=>p.ids['low-power-'+n];
 for(const value of ['0','721','1.5']){e('hours').value=value;e('hours').oninput();const before=p.requests.length;await e('save').onclick();assert.equal(p.requests.length,before);}
 e('hours').value='72';e('hours').oninput();e('enabled').checked=false;e('enabled').onchange();await e('save').onclick();
 assert.deepEqual(JSON.parse(p.requests.at(-1).options.body),{rev:1,enabled:false,idle_hours:72});assert.match(e('message').textContent,/saved/);
});
