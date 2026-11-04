// SPDX-License-Identifier: BSD-2-Clause
// Test the embedded signal page's real JavaScript; no external dependencies.
const {test} = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const vm = require('node:vm');
const scripts = [...fs.readFileSync(path.join(__dirname, '../main/web/index.html'), 'utf8').matchAll(/<script>([\s\S]*?)<\/script>/g)];
const matches=scripts.filter(match=>match[1].includes("getElementById('vars-'+id)"));
assert.equal(matches.length,1,'The page must include exactly one Varstore controller');
const script=matches[0][1];
class Element {
  constructor(tag='div') {Object.assign(this,{tag,children:[],value:'',textContent:'',disabled:false,checked:true});}
  append(...children) {this.children.push(...children);}
  setAttribute(name,value) {this[name]=value;}
  click() {}
}
const tick = () => new Promise(resolve => setImmediate(resolve));
async function page() {
  const ids=Object.fromEntries(['filter','auto','refresh','dump','summary','message','signals','prev','page','next'].map(id=>[id,new Element()]));
  const signals=[
    {id:0,name:'app.heartbeat',description:'Application heartbeat',unit:'count',max_age_ms:3000,writable:false,state:'valid',forced:false,value:42,age_ms:100,raw_value:42,raw_age_ms:100,raw_valid:true},
    {id:1,name:'demo.value',description:'Manual signal',unit:'raw',max_age_ms:5000,writable:true,state:'invalid',forced:false,value:null,age_ms:null,raw_value:null,raw_age_ms:null,raw_valid:false}
  ];
  const posts=[],downloads=[];let reject=false;let interval;let tabChange;
  const panel={hidden:false};
  const context=vm.createContext({Blob:class{constructor(parts){downloads.push(JSON.parse(parts[0]));}},URL:{createObjectURL:()=> 'blob:test',revokeObjectURL:()=>{}},onTabChange:fn=>{tabChange=fn;},document:{getElementById:id=>id==='panel-varstore'?panel:ids[id.replace(/^vars-/,'')],createElement:tag=>new Element(tag)},
    setInterval:fn=>{interval=fn;},fetch:async (url,options)=>{
      assert.equal(url,'/api/vars');
      if(options) posts.push(JSON.parse(options.body));
      return {ok:!reject,text:async ()=>'Command rejected',json:async ()=>options?{updated:true}:{signals}};
    }});
  vm.runInContext(script,context);await tick();
  return {ids,signals,posts,downloads,context,panel,select:async(tab)=>{panel.hidden=tab!=='varstore';tabChange(tab);await tick();},refresh:async()=>{await ids.refresh.onclick();await tick();},poll:async()=>{interval();await tick();},reject:()=>{reject=true;},rows:ids.signals.children};
}
function controls(row){return row.children[5].children[0].children;}

test('shows invalid samples and separates effective forced values from producer data',async()=>{
  const p=await page();
  assert.equal(p.rows[1].children[1].textContent,'No sample');
  Object.assign(p.signals[0],{state:'forced',forced:true,value:99,raw_value:43});
  await p.refresh();
  assert.equal(p.rows[0].children[1].textContent,'99 count');
  assert.match(p.rows[0].children[4].textContent,/43 count/);
  assert.match(p.ids.summary.textContent,/1 forced/);
});
test('only manual signals permit direct writes and signed int32 values are validated',async()=>{
  const p=await page();const owned=controls(p.rows[0]),manual=controls(p.rows[1]);
  assert.equal(owned[1].disabled,true);assert.equal(owned[2].disabled,false);
  assert.equal(owned[4].disabled,true);assert.equal(owned.length,5);
  for(const invalid of ['', '1.5','2147483648','-2147483649','1e3']){
    manual[0].value=invalid;await manual[1].onclick();assert.equal(p.posts.length,0);
  }
  manual[0].value='-2147483648';await manual[1].onclick();
  assert.deepEqual(p.posts[0],{id:1,action:'write',value:-2147483648});
});
test('polling preserves edited input and force/release send the intended commands',async()=>{
  const p=await page();const c=controls(p.rows[0]);
  c[0].value='123';await p.poll();assert.equal(c[0].value,'123');
  await c[2].onclick();assert.deepEqual(p.posts[0],{id:0,action:'force',value:123});
  Object.assign(p.signals[0],{forced:true,state:'forced'});await p.refresh();
  assert.equal(c[3].disabled,false);await c[3].onclick();
  assert.deepEqual(p.posts[1],{id:0,action:'release'});
});
test('filters by description and recovers controls after a rejected command',async()=>{
  const p=await page();p.ids.filter.value='manual';p.ids.filter.oninput();
  assert.equal(p.rows[0].hidden,true);assert.equal(p.rows[1].hidden,false);
  p.reject();const c=controls(p.rows[1]);c[0].value='12';await c[2].onclick();
  assert.equal(p.ids.message.textContent,'Command rejected');
  assert.equal(c[1].disabled,false);assert.equal(c[2].disabled,false);
  assert.equal(controls(p.rows[0])[1].disabled,true);
});
test('hidden Varstore stops polling and refreshes on re-entry without losing input',async()=>{
  const p=await page();const c=controls(p.rows[0]);c[0].value='123';
  await p.select('home');p.signals[0].value=77;await p.poll();
  assert.equal(p.rows[0].children[1].textContent,'42 count');
  await p.select('varstore');assert.equal(p.rows[0].children[1].textContent,'77 count');
  assert.equal(c[0].value,'123');
});

function expand(p){
  for(let id=2;id<31;id++)p.signals.push({...p.signals[0],id,name:'test.signal.'+id});
}
const visibleRows=p=>p.rows.filter(row=>!row.hidden);
test('paginates all signals in groups of 14 and retains page and edits on refresh',async()=>{
  const p=await page();expand(p);await p.refresh();
  assert.equal(visibleRows(p).length,14);assert.match(p.ids.page.textContent,/Page 1 \/ 3/);
  assert.equal(p.ids.prev.disabled,true);assert.equal(p.ids.next.disabled,false);
  p.ids.next.onclick();assert.equal(visibleRows(p).length,14);
  assert.match(p.ids.page.textContent,/Page 2 \/ 3/);
  const input=controls(p.rows[14])[0];input.value='1234';
  await p.poll();assert.match(p.ids.page.textContent,/Page 2 \/ 3/);assert.equal(input.value,'1234');
  p.ids.next.onclick();assert.equal(visibleRows(p).length,3);assert.equal(p.ids.next.disabled,true);
  p.ids.prev.onclick();assert.match(p.ids.page.textContent,/Page 2 \/ 3/);
});
test('filter resets pagination and no matches show page zero with disabled navigation',async()=>{
  const p=await page();expand(p);await p.refresh();p.ids.next.onclick();
  p.ids.filter.value='manual';p.ids.filter.oninput();
  assert.equal(visibleRows(p).length,1);assert.match(p.ids.page.textContent,/Page 1 \/ 1/);
  p.ids.filter.value='not found';p.ids.filter.oninput();
  assert.equal(visibleRows(p).length,0);assert.equal(p.ids.page.textContent,'Page 0 / 0');
  assert.equal(p.ids.prev.disabled,true);assert.equal(p.ids.next.disabled,true);
  p.ids.filter.value='';p.ids.filter.oninput();assert.equal(visibleRows(p).length,14);
});
test('JSON download exports every signal despite filtering and pagination',async()=>{
  const p=await page();expand(p);await p.refresh();
  p.ids.next.onclick();p.ids.filter.value='manual';p.ids.filter.oninput();p.ids.dump.onclick();
  assert.equal(p.downloads.length,1);assert.equal(p.downloads[0].signals.length,31);
});
test('failed refresh shows the server error and preserves the existing rows',async()=>{
  const p=await page();p.reject();await p.refresh();
  assert.equal(p.ids.message.textContent,'Command rejected');assert.equal(p.rows.length,2);
  assert.equal(p.rows[0].children[1].textContent,'42 count');
});
