const {test}=require('node:test');
const assert=require('node:assert/strict');
const fs=require('node:fs');
const vm=require('node:vm');
const path=require('node:path');
const source=fs.readFileSync(path.join(__dirname,'../main/web/index.html'),'utf8');
const script=[...source.matchAll(/<script>([\s\S]*?)<\/script>/g)].map(m=>m[1]).find(s=>s.includes('/* Gateway restart */'));
function page(failure=false){
  const button={disabled:false},message={textContent:''},requests=[];
  const fetch=async(url,options)=>{
    requests.push({url,options});if(failure)throw Error('Connection lost');
    return {ok:true,json:async()=>({restarting:true})};
  };
  vm.runInNewContext(script,{document:{getElementById:id=>id==='restart-gateway'?button:message},fetch});
  return {button,message,requests};
}
test('restart requires an explicit click, sends once and disables repeated clicks',async()=>{
  const p=page();assert.equal(p.requests.length,0);
  await p.button.onclick();await p.button.onclick();
  assert.equal(p.requests.length,1);assert.equal(p.requests[0].url,'/api/restart');
  assert.equal(p.requests[0].options.method,'POST');assert.equal(p.requests[0].options.body,'{}');
  assert.equal(p.button.disabled,true);assert.match(p.message.textContent,/Restart scheduled/);
});
test('ambiguous restart failure never retries and explains how to check/re-enable',async()=>{
  const p=page(true);await p.button.onclick();await p.button.onclick();
  assert.equal(p.requests.length,1);assert.equal(p.button.disabled,true);
  assert.match(p.message.textContent,/Restart not confirmed.*Connection lost/);
  assert.match(p.message.textContent,/reload this page/);
});
