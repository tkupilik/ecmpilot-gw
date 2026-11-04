// SPDX-License-Identifier: BSD-2-Clause
const {test}=require('node:test');
const assert=require('node:assert/strict');
const fs=require('node:fs');const path=require('node:path');const vm=require('node:vm');
const source=fs.readFileSync(path.join(__dirname,'../main/web/index.html'),'utf8');
const matches=[...source.matchAll(/<script>([\s\S]*?)<\/script>/g)].filter(match=>match[1].includes("getElementById('log-'+id)"));
assert.equal(matches.length,1,'The page must include exactly one log controller');
const script=matches[0][1];
const tick=()=>new Promise(resolve=>setImmediate(resolve));
async function page(){
  const ids=Object.fromEntries(['auto','follow','refresh','clear','download','message','output'].map(id=>[id,{checked:true,disabled:false,textContent:'',scrollHeight:100}]));
  const panel={hidden:true};let select,interval,entries=[],retained=[],resetReason=null,boot='a'.repeat(32),reject=false,latest=0;const requests=[];
  const context=vm.createContext({document:{getElementById:id=>id==='panel-log'?panel:ids[id.replace('log-','')]},
    onTabChange:fn=>{select=fn;},setInterval:fn=>{interval=fn;},fetch:async url=>{
      requests.push(url);const after=Number(url.match(/after=(\d+)/)[1]);const askedBoot=url.match(/boot=([0-9a-f]+)/)?.[1];
      const reset=askedBoot&&askedBoot!==boot;const cursor=reset?0:after;
      const previous=url.includes('previous=1');const selected=previous?retained:entries;
      const visible=selected.filter(e=>e.seq>cursor).slice(0,16);
      return {ok:!reject,text:async()=>'Disconnected',json:async()=>({boot,reset,previous,previous_available:retained.length>0,reset_reason:resetReason,reset_reason_code:4,oldest:selected[0]?.seq??0,latest:previous?(retained.at(-1)?.seq??0):latest,next:visible.at(-1)?.seq??cursor,lost:Math.max(0,(selected[0]?.seq??1)-cursor-1),entries:visible})};
    }});
  vm.runInContext(script,context);await tick();
  return {ids,requests,add:(text)=>{entries.push({seq:++latest,text});},overwrite:()=>{entries=entries.slice(-64);},restart:()=>{boot='b'.repeat(32);latest=0;entries=[];},warmRestart:()=>{retained=entries.slice(-12);resetReason='PANIC';boot='b'.repeat(32);latest=0;entries=[];},reject:()=>{reject=true;},
    select:async tab=>{panel.hidden=tab!=='log';select(tab);await tick();},poll:async()=>{interval();await tick();},refresh:async()=>{await ids.refresh.onclick();await tick();}};
}
test('Log polls only while visible and live, then resumes from its cursor',async()=>{
  const p=await page();p.add('first\n');await p.poll();assert.equal(p.requests.length,0);
  await p.select('log');assert.equal(p.ids.output.textContent,'first\n');
  p.ids.auto.checked=false;p.add('second\n');await p.poll();assert.equal(p.ids.output.textContent,'first\n');
  await p.refresh();assert.equal(p.ids.output.textContent,'first\nsecond\n');
  await p.select('home');p.ids.auto.checked=true;await p.poll();assert.equal(p.requests.length,2);
});
test('drains backlog, strips ANSI and Clear view does not erase the gateway buffer',async()=>{
  const p=await page();for(let n=0;n<40;n++)p.add('\x1b[32mentry '+n+'\x1b[0m\n');
  await p.select('log');assert.equal(p.requests.length,3);assert.match(p.ids.output.textContent,/entry 39/);assert.doesNotMatch(p.ids.output.textContent,/\x1b/);
  p.ids.clear.onclick();assert.equal(p.ids.output.textContent,'');p.add('new\n');await p.poll();
  assert.equal(p.ids.output.textContent,'new\n');assert.match(p.requests.at(-1),/after=40/);
});
test('reports overwritten entries and gateway restart, keeping previous visible text',async()=>{
  const p=await page();for(let n=0;n<80;n++)p.add('old '+n+'\n');p.overwrite();await p.select('log');
  assert.match(p.ids.output.textContent,/16 log entries overwritten/);
  p.restart();p.add('after reboot\n');await p.poll();
  assert.match(p.ids.output.textContent,/Gateway restarted/);assert.match(p.ids.output.textContent,/after reboot/);
});
test('network errors preserve displayed text and re-enable controls',async()=>{
  const p=await page();p.add('keep\n');await p.select('log');p.reject();await p.refresh();
  assert.equal(p.ids.output.textContent,'keep\n');assert.equal(p.ids.message.textContent,'Disconnected');
  assert.equal(p.ids.refresh.disabled,false);assert.equal(p.ids.clear.disabled,false);
});

test('loads frozen previous boot automatically once, before current startup logs',async()=>{
  const p=await page();for(let n=0;n<20;n++)p.add('before '+n+'\n');
  p.warmRestart();for(let n=0;n<80;n++)p.add('startup '+n+'\n');p.overwrite();
  await p.select('log');const text=p.ids.output.textContent;
  assert.match(text,/Previous boot: last 12 retained entries; reset reason: PANIC/);
  assert.doesNotMatch(text,/before 7\n/);assert.match(text,/before 19\n/);
  assert.ok(text.indexOf('before 19')<text.indexOf('startup 16'));
  assert.match(text,/Reset reason: PANIC \(4\)/);
  await p.poll();assert.equal(p.requests.filter(url=>url.includes('previous=1')).length,1);
});

test('an already open log shows the new previous tail after a restart',async()=>{
  const p=await page();p.add('awake\n');await p.select('log');
  p.add('BSI PowerManagement: 0 -> 1\n');p.warmRestart();p.add('new boot\n');await p.poll();
  assert.match(p.ids.output.textContent,/Gateway restarted/);
  assert.match(p.ids.output.textContent,/BSI PowerManagement: 0 -> 1/);
  assert.match(p.ids.output.textContent,/new boot/);
});
