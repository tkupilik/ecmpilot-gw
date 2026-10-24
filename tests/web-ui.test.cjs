// SPDX-License-Identifier: BSD-2-Clause
// Exercise the embedded page's real script with a minimal DOM/HTTP harness.
// These are logic tests, not browser rendering or ESP HTTP-server tests.
const {test} = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const vm = require('node:vm');
const script = fs.readFileSync(path.join(__dirname, '../main/web/index.html'), 'utf8').match(/<script>([\s\S]*?)<\/script>/)[1];

class Element {
  constructor(tag = 'div') {this.tag = tag;this.children = [];this.dataset = {};this.value = '';this.checked = false;this.textContent = '';this.disabled = false;this.style = {};}
  setAttribute(name, value) {this[name] = value;}
  append(...children) {for (const child of children) {if (typeof child === 'string') continue;child.parentNode = this;this.children.push(child);}}
  remove() {if (this.parentNode) this.parentNode.children.splice(this.parentNode.children.indexOf(this), 1);}
  replaceChildren() {this.children = [];}
  insertBefore(child, before) {child.remove();child.parentNode = this;this.children.splice(this.children.indexOf(before), 0, child);}
  get previousElementSibling() {return this.parentNode?.children[this.parentNode.children.indexOf(this) - 1];}
  get nextElementSibling() {return this.parentNode?.children[this.parentNode.children.indexOf(this) + 1];}
  querySelectorAll(tag) {return this.children.flatMap(c => [...(c.tag === tag ? [c] : []), ...c.querySelectorAll(tag)]);}
}

async function page() {
  const ids = Object.fromEntries(['status','refresh','profiles','add','save','firmware','upload','progress','message'].map(id => [id, new Element()]));
  const requests = [], uploads = [];
  const profiles = [{ssid:'Home',has_password:true},{ssid:'Workshop',has_password:false}];
  class Upload {
    constructor() {this.upload = {};uploads.push(this);}
    open(method, url) {this.method = method;this.url = url;}
    setRequestHeader(name, value) {this.headers ??= {};this.headers[name] = value;}
    send(file) {this.file = file;}
  }
  const fetch = async (url, options) => {
    requests.push({url, options});
    return {ok:true, json:async () => url === '/api/status' ? {version:'test',partition:'ota_0',uptime_s:1,free_heap:100000,heartbeat:2,connected:false,setup_ap:true,ap_ssid:'eCMPilot-TEST'} : options ? {saved:true} : {networks:profiles}};
  };
  const context = vm.createContext({document:{getElementById:id => ids[id],createElement:tag => new Element(tag),querySelectorAll:selector => {assert.equal(selector,'.profile');return ids.profiles.children;}},fetch,XMLHttpRequest:Upload,console});
  vm.runInContext(script, context);
  await new Promise(resolve => setImmediate(resolve));
  return {ids, requests, uploads, rows:ids.profiles.children};
}

test('saved credentials are kept when saving unchanged profiles', async () => {
  const p = await page();
  await p.ids.save.onclick();
  const post = p.requests.find(r => r.options?.method === 'POST');
  assert.deepEqual(JSON.parse(post.options.body), {networks:[{ssid:'Home'},{ssid:'Workshop',password:''}]});
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
