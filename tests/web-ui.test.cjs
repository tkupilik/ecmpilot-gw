// SPDX-License-Identifier: BSD-2-Clause
// Exercise the embedded page's real script with a minimal DOM/HTTP harness.
// These are logic tests, not browser rendering or ESP HTTP-server tests.
const {test} = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const vm = require('node:vm');
const script = [...fs.readFileSync(path.join(__dirname, '../main/web/index.html'), 'utf8').matchAll(/<script>([\s\S]*?)<\/script>/g)].map(match=>match[1]).join('\n');

class Element {
  constructor(tag = 'div') {this.tag = tag;this.children = [];this.dataset = {};this.value = '';this.checked = false;this.textContent = '';this.disabled = false;this.style = {};}
  setAttribute(name, value) {this[name] = value;}
  focus() {this.focused = true;}
  append(...children) {for (const child of children) {if (typeof child === 'string') continue;child.parentNode = this;this.children.push(child);}}
  remove() {if (this.parentNode) this.parentNode.children.splice(this.parentNode.children.indexOf(this), 1);}
  replaceChildren() {this.children = [];}
  insertBefore(child, before) {child.remove();child.parentNode = this;this.children.splice(this.children.indexOf(before), 0, child);}
  get previousElementSibling() {return this.parentNode?.children[this.parentNode.children.indexOf(this) - 1];}
  get nextElementSibling() {return this.parentNode?.children[this.parentNode.children.indexOf(this) + 1];}
  querySelectorAll(tag) {return this.children.flatMap(c => [...(c.tag === tag ? [c] : []), ...c.querySelectorAll(tag)]);}
}

async function page(hash='') {
  const tabNames=['home','varstore','wifi','ha','low-power','ota','log','tasks'];
  const ids = Object.fromEntries([...['enabled','host','port','period','username','password','clear-password','save','reload','message','status'].map(name=>'ha-'+name),'can-wake-send','can-wake-status',...['auto','refresh','dump','summary','message','rows'].map(name=>'tasks-'+name),...['auto','follow','refresh','clear','download','message','output'].map(name=>'log-'+name),'status','hostname','ap-ssid','ap-enabled','ap-password','ap-password-show','refresh','profiles','add','save','firmware','upload','progress','message',...['filter','auto','refresh','dump','summary','message','signals','prev','page','next'].map(name=>'vars-'+name),...['status','message','normal','dfs','light','deep','refresh'].map(name=>'power-'+name),...tabNames.flatMap(name=>['tab-'+name,'panel-'+name])].map(id => [id, new Element()]));
  const requests = [], uploads = [];
  let restart=false,apEnabled=false;
  const profiles = [{ssid:'Home',has_password:true},{ssid:'Workshop',has_password:false}];
  class Upload {
    constructor() {this.upload = {};uploads.push(this);}
    open(method, url) {this.method = method;this.url = url;}
    setRequestHeader(name, value) {this.headers ??= {};this.headers[name] = value;}
    send(file) {this.file = file;}
  }
  const fetch = async (url, options) => {
    requests.push({url, options});
    if(url==='/api/wifi'&&options)apEnabled=JSON.parse(options.body).ap_enabled;
    return {ok:true, json:async () => url === '/api/status' ? {version:'test',partition:'ota_0',uptime_s:1,free_heap:100000,heartbeat:2,connected:false,setup_ap:true,ap_ssid:'eCMPilot-TEST'} : url === '/api/vars' ? {signals:[{id:0,name:'app.heartbeat',description:'Heartbeat',unit:'count',value:42,age_ms:1,raw_value:42,raw_age_ms:1,raw_valid:true,state:'valid',forced:false,writable:false}]} : url==='/api/tasks' ? {boot:'0'.repeat(32),sampled_us:1000000,cores:2,tasks:[]} : url.startsWith('/api/log') ? {boot:'0'.repeat(32),latest:0,next:0,lost:0,entries:[]} : options ? {saved:true,restarting:restart} : {hostname:'ecmpilot-gw',ap_enabled:apEnabled,ap_ssid:'eCMPilot-TEST',ap_password:'APpassword123',networks:profiles}};
  };
  const location={hash};const listeners={};
  const context = vm.createContext({setInterval:()=>{},location,history:{replaceState:(_state,_title,url)=>{location.hash=url;}},window:{addEventListener:(name,fn)=>{listeners[name]=fn;}},document:{getElementById:id => ids[id],createElement:tag => new Element(tag),querySelectorAll:selector => {assert.equal(selector,'.profile');return ids.profiles.children;}},fetch,XMLHttpRequest:Upload,console});
  vm.runInContext(script, context);
  await new Promise(resolve => setImmediate(resolve));
  return {ids, requests, uploads, location, listeners, rows:ids.profiles.children, restart:()=>{restart=true;}};
}

test('tabs show the requested panel and preserve unfinished Wi-Fi edits', async()=>{
  const p=await page();
  assert.equal(p.ids['panel-home'].hidden,false);
  for(const name of ['varstore','wifi','ota','log','tasks'])assert.equal(p.ids['panel-'+name].hidden,true);
  p.ids['tab-wifi'].onclick();p.ids.hostname.value='my-corsa';p.rows[0].querySelectorAll('input')[0].value='Unfinished edit';
  p.ids['tab-ota'].onclick();
  assert.equal(p.ids['panel-wifi'].hidden,true);assert.equal(p.ids['panel-ota'].hidden,false);
  assert.equal(p.ids['tab-ota']['aria-selected'],'true');assert.equal(p.location.hash,'#ota');
  p.ids['tab-wifi'].onclick();assert.equal(p.ids.hostname.value,'my-corsa');assert.equal(p.rows[0].querySelectorAll('input')[0].value,'Unfinished edit');
});

test('tab links restore a selected panel and unknown fragments fall back to Home',async()=>{
  const p=await page('#varstore');assert.equal(p.ids['panel-varstore'].hidden,false);
  p.location.hash='#wifi';p.listeners.hashchange();assert.equal(p.ids['panel-wifi'].hidden,false);
  p.location.hash='#unknown';p.listeners.hashchange();assert.equal(p.ids['panel-home'].hidden,false);
});

test('tabs support keyboard navigation and move focus to the selected tab',async()=>{
  const p=await page();let prevented=false;
  p.ids['tab-home'].onkeydown({key:'ArrowLeft',preventDefault:()=>{prevented=true;}});
  assert.equal(prevented,true);assert.equal(p.ids['panel-tasks'].hidden,false);
  assert.equal(p.ids['tab-tasks'].focused,true);assert.equal(p.ids['tab-tasks'].tabIndex,0);
  p.ids['tab-ota'].onkeydown({key:'Home',preventDefault:()=>{}});
  assert.equal(p.ids['panel-home'].hidden,false);
});

test('saved credentials are kept when saving unchanged profiles', async () => {
  const p = await page();
  await p.ids.save.onclick();
  const post = p.requests.find(r => r.options?.method === 'POST');
  assert.deepEqual(JSON.parse(post.options.body), {hostname:'ecmpilot-gw',ap_enabled:false,networks:[{ssid:'Home'},{ssid:'Workshop',password:''}]});
});

test('hostname is loaded, normalized and saved with Wi-Fi settings',async()=>{
  const p=await page();assert.equal(p.ids.hostname.value,'ecmpilot-gw');
  p.ids.hostname.value='  My-Corsa  ';await p.ids.save.onclick();
  const post=p.requests.find(r=>r.options?.method==='POST');
  assert.equal(JSON.parse(post.options.body).hostname,'my-corsa');
});
test('invalid DNS labels are rejected before changing network settings',async()=>{
  const p=await page();
  for(const name of ['', '-corsa','corsa-','my_corsa','corsa.lan','my corsa','a'.repeat(33)]){
    p.ids.hostname.value=name;await p.ids.save.onclick();
    assert.equal(p.requests.filter(r=>r.options).length,0);assert.match(p.ids.message.textContent,/Hostname/);
  }
  p.ids.hostname.value='a'.repeat(32);await p.ids.save.onclick();
  assert.equal(p.requests.filter(r=>r.options).length,1);
});

test('priority arrows determine the order sent to the gateway', async () => {
  const p = await page();
  p.rows[1].children[2].children[0].onclick();
  await p.ids.save.onclick();
  const post = p.requests.find(r => r.options?.method === 'POST');
  assert.deepEqual(JSON.parse(post.options.body).networks.map(n => n.ssid), ['Workshop','Home']);
});

test('renaming a saved SSID requires credentials instead of reusing another network password', async () => {
  const p = await page();
  p.rows[0].querySelectorAll('input')[0].value = 'Different network';
  await p.ids.save.onclick();
  assert.equal(p.requests.filter(r => r.options).length, 0);
  assert.match(p.ids.message.textContent, /Enter a password/);
});

test('selecting an open network explicitly clears a saved password', async () => {
  const p = await page();
  p.rows[0].querySelectorAll('input')[2].checked = true;
  await p.ids.save.onclick();
  const post = p.requests.find(r => r.options?.method === 'POST');
  assert.equal(JSON.parse(post.options.body).networks[0].password, '');
});

test('an empty row is rejected and a ninth network cannot be added', async () => {
  const p = await page();
  p.ids.add.onclick();
  await p.ids.save.onclick();
  assert.equal(p.requests.filter(r => r.options).length, 0);
  assert.match(p.ids.message.textContent, /Wi-Fi name/);
  for (let i=0;i<10;i++) p.ids.add.onclick();
  assert.equal(p.ids.profiles.children.length, 8);
});

test('OTA sends the raw file with the required content type and reports acceptance only after HTTP 200', async () => {
  const p = await page();
  const file = {name:'ecmpilot_gw.bin'};
  p.ids.firmware.files = [file];
  p.ids.upload.onclick();
  const upload = p.uploads[0];
  assert.equal(upload.method, 'POST');
  assert.equal(upload.url, '/api/ota');
  assert.equal(upload.headers['Content-Type'], 'application/octet-stream');
  assert.equal(upload.file, file);
  assert.equal(p.ids.upload.disabled, true);
  assert.match(p.ids.message.textContent, /Uploading/);
  upload.status = 400;upload.responseText = 'Invalid image';upload.onload();
  assert.equal(p.ids.message.textContent, 'Invalid image');
  assert.equal(p.ids.upload.disabled, false);
});


test('AP password is displayed, revealable, editable and preserved across tabs',async()=>{
  const p=await page();assert.equal(p.ids['ap-ssid'].textContent,'eCMPilot-TEST');
  assert.equal(p.ids['ap-password'].value,'APpassword123');p.ids['ap-password-show'].checked=true;p.ids['ap-password-show'].onchange();
  assert.equal(p.ids['ap-password'].type,'text');
  p.ids['ap-password'].value='new AP password';p.ids['tab-log'].onclick();p.ids['tab-wifi'].onclick();
  assert.equal(p.ids['ap-password'].value,'new AP password');p.restart();await p.ids.save.onclick();
  const post=p.requests.find(r=>r.options?.method==='POST');assert.equal(JSON.parse(post.options.body).ap_password,'new AP password');
  assert.match(p.ids.message.textContent,/restarting/);
});

test('invalid AP passwords are rejected; blank keeps the saved password',async()=>{
  const p=await page();
  for(const value of ['short','a'.repeat(64),'bad\npassword','neplatnéheslo']){
    p.ids['ap-password'].value=value;await p.ids.save.onclick();assert.equal(p.requests.filter(r=>r.options).length,0);assert.match(p.ids.message.textContent,/AP password/);
  }
  p.ids['ap-password'].value='';await p.ids.save.onclick();
  assert.equal(JSON.parse(p.requests.find(r=>r.options).options.body).ap_password,undefined);
});

test('full page loads Varstore through tab entry once and binds its visible controls',async()=>{
  const p=await page();p.ids['tab-varstore'].onclick();
  await new Promise(resolve=>setImmediate(resolve));
  assert.equal(p.requests.filter(request=>request.url==='/api/vars').length,1);
  assert.equal(p.ids['vars-signals'].children.length,1);
  assert.equal(p.ids['vars-signals'].children[0].hidden,false);
  assert.match(p.ids['vars-page'].textContent,/Page 1 \/ 1/);
  assert.equal(typeof p.ids['vars-refresh'].onclick,'function');
  assert.equal(typeof p.ids['vars-filter'].oninput,'function');
});

test('AP fallback defaults unchecked and saves/reloads both enabled and disabled',async()=>{
  const p=await page();assert.equal(p.ids['ap-enabled'].checked,false);
  p.ids['ap-enabled'].checked=true;await p.ids.save.onclick();
  let posts=p.requests.filter(r=>r.options?.method==='POST');
  assert.equal(JSON.parse(posts.at(-1).options.body).ap_enabled,true);
  assert.equal(p.ids['ap-enabled'].checked,true);
  p.ids['ap-enabled'].checked=false;await p.ids.save.onclick();
  posts=p.requests.filter(r=>r.options?.method==='POST');
  assert.equal(JSON.parse(posts.at(-1).options.body).ap_enabled,false);
  assert.equal(p.ids['ap-enabled'].checked,false);
});
test('unfinished AP policy survives tab changes',async()=>{
  const p=await page();p.ids['ap-enabled'].checked=true;
  p.ids['tab-log'].onclick();p.ids['tab-wifi'].onclick();
  assert.equal(p.ids['ap-enabled'].checked,true);
});
