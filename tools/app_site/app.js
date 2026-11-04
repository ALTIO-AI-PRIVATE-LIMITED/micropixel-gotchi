import { GotchiUsb } from './gotchi-usb.js';

const STORE = 'https://micropixel.ai';
// Cheeko Gotchi: ESP32-S3, 296x240 screen. Used until a real Gotchi is connected.
const GOTCHI = { target: 'xtensa', width: 296, height: 240 };
const $ = id => document.getElementById(id);
let catalog = [];        // merged list of installable apps
let device = null;       // { status, apps, storeUsed, storeTotal }
let usb = null, busy = false;
const progress = new Map();

// ---------- catalog: micropixel.ai store + our own prebuilt apps ----------
function cmpVersion(a = '', b = '') {
  const pa = a.split(/[.-]/).map(Number), pb = b.split(/[.-]/).map(Number);
  for (let i = 0; i < Math.max(pa.length, pb.length); i++) { const d = (pa[i] || 0) - (pb[i] || 0); if (d) return Math.sign(d); }
  return 0;
}

async function loadCatalog() {
  const [store, ours, info] = await Promise.all([
    fetch('/api/store', { signal: AbortSignal.timeout(15000) }).then(r => r.ok ? r.json() : { apps: [] }).catch(() => ({ apps: [] })),
    fetch('/api/file?path=apps/index.json').then(r => r.ok ? r.json() : { apps: [] }).catch(() => ({ apps: [] })),
    fetch('/app_info.json').then(r => r.ok ? r.json() : {}).catch(() => ({})),
  ]);
  const byId = new Map();
  for (const a of store.apps || []) {
    if (a.hidden) continue;
    const releases = (a.releases || []).filter(r => !r.withdrawn && (r.packageType || 'app') === 'app');
    if (!releases.length) continue;
    byId.set(a.appId, {
      appId: a.appId, title: a.title, summary: a.summary || '', developer: a.developer || '',
      cover: a.cover ? STORE + a.cover : '', source: 'MicroPixel Store',
      releases: releases.map(r => ({ target: r.target, version: r.version, sizeBytes: r.sizeBytes, sha256: r.sha256, requirements: r.requirements, url: '/api/download?id=' + r.id })),
    });
  }
  for (const a of ours.apps || []) {
    const rel = { target: a.target, version: a.version, sizeBytes: a.sizeBytes, sha256: a.sha256, requirements: a.requirements, url: '/api/file?path=' + a.file };
    const existing = byId.get(a.appId);
    if (existing) {
      // keep whichever build is newer for this chip
      const other = existing.releases.find(r => r.target === a.target);
      if (!other || cmpVersion(a.version, other.version) > 0) existing.releases = [...existing.releases.filter(r => r.target !== a.target), rel];
    } else {
      byId.set(a.appId, { appId: a.appId, title: a.title, summary: a.summary || '', developer: 'Cheeko', cover: '', source: 'Cheeko apps', releases: [rel] });
    }
  }
  // Our own thumbnails and longer descriptions win over the catalog's.
  for (const [id, a] of byId) {
    const extra = info[id]; if (!extra) continue;
    a.summary = extra.summary || a.summary; a.info = extra;
    if (extra.cover !== false && (!a.cover || a.source === 'Cheeko apps')) a.cover = a.cover || `/covers/${id}.png`;
  }
  catalog = [...byId.values()].sort((x, y) => x.title.localeCompare(y.title));
}

function releaseFor(app) {
  const target = (device?.status || GOTCHI).target;
  return app.releases.find(r => r.target === target) || null;
}
function fits(rel) {
  if (!rel) return false;
  const d = rel.requirements?.display; if (!d) return true;
  const { width: w, height: h } = device?.status || GOTCHI;
  const mw = d.min_width || 0, mh = d.min_height || 0;
  return (w >= mw && h >= mh) || (h >= mw && w >= mh);
}

// ---------- rendering ----------
const fmt = b => b > 1e6 ? (b / 1e6).toFixed(1) + ' MB' : Math.round(b / 1e3) + ' KB';

function render() {
  const q = $('q').value.toLowerCase(), filter = $('filter').value;
  const installed = new Map((device?.apps || []).map(a => [a.appId, a]));
  const rows = catalog.map(a => {
    const rel = releaseFor(a), dev = installed.get(a.appId);
    const update = dev && rel && (dev.sha256 ? dev.sha256 !== rel.sha256 && (!dev.version || cmpVersion(rel.version, dev.version) >= 0) : cmpVersion(rel.version, dev.version || '0') > 0);
    return { ...a, rel, dev, update, fit: fits(rel) };
  }).filter(r => r.rel && r.fit);
  for (const [id, d] of installed) if (!catalog.some(a => a.appId === id))
    rows.push({ appId: id, title: d.displayName, summary: 'Installed on your Gotchi. Not in any catalog.', source: 'Only on this Gotchi', releases: [], rel: null, dev: d, fit: true });

  const groups = new Map();
  for (const r of rows) {
    if (q && !(r.title + ' ' + r.appId + ' ' + r.developer).toLowerCase().includes(q)) continue;
    if (filter === 'installed' && !r.dev) continue;
    if (filter === 'updates' && !r.update) continue;
    if (filter === 'fits' && !(r.rel && r.fit)) continue;
    if (!groups.has(r.source)) groups.set(r.source, []);
    groups.get(r.source).push(r);
  }
  $('updateAll').disabled = busy || !rows.some(r => r.update);
  const root = $('sections'); root.innerHTML = '';
  for (const name of ['Cheeko apps', 'MicroPixel Store', 'Only on this Gotchi']) {
    const list = groups.get(name); if (!list?.length) continue;
    const h = document.createElement('h2'); h.textContent = `${name} · ${list.length}`; root.append(h);
    const grid = document.createElement('div'); grid.className = 'grid'; root.append(grid);
    for (const r of list) grid.append(card(r));
  }
  if (!root.children.length) root.innerHTML = '<p class="msg">No apps match.</p>';
}

function card(r) {
  const el = document.createElement('div'); el.className = 'card';
  el.innerHTML = `<div class="cover"></div><div class="body"><h3></h3><div class="meta"></div><div class="pills"></div><div class="sum"></div><details class="more" hidden><summary>About this app</summary><div class="info"></div></details><div class="progress" hidden><div></div></div><div class="actions"></div></div>`;
  const cover = el.querySelector('.cover');
  if (r.cover) { const img = new Image(); img.loading = 'lazy'; img.alt = ''; img.src = r.cover; cover.append(img); } else cover.textContent = r.title.slice(0, 14);
  el.querySelector('h3').textContent = r.title;
  el.querySelector('.meta').textContent = [r.developer, r.rel ? 'v' + r.rel.version : null, r.rel ? fmt(r.rel.sizeBytes) : null].filter(Boolean).join(' · ');
  el.querySelector('.sum').textContent = r.summary;
  if (r.info) {
    const more = el.querySelector('.more'); more.hidden = false;
    const box = more.querySelector('.info');
    const add = (label, text) => { if (!text) return; const h = document.createElement('b'); h.textContent = label; const t = document.createElement('p'); t.textContent = text; box.append(h, t); };
    add('What it does', r.info.about); add('Why we made it', r.info.why);
    if (r.info.needs?.length) {
      const h = document.createElement('b'); h.textContent = 'What you need'; const ul = document.createElement('ul');
      for (const n of r.info.needs) { const li = document.createElement('li'); li.textContent = n; ul.append(li); }
      box.append(h, ul);
    }
  }
  const pills = el.querySelector('.pills');
  const pill = (t, c) => { const s = document.createElement('span'); s.className = 'pill ' + c; s.textContent = t; pills.append(s); };
  if (r.info?.kind) pill(r.info.kind, 'kind');
  if (r.dev) pill('Installed' + (r.dev.version ? ' v' + r.dev.version : ''), 'ok');
  if (r.update) pill('Update', 'up');
  if (r.dev?.active) pill('Running', 'run');
  if (!r.rel && r.releases.length) pill('Not built for this chip', 'no');
  else if (r.rel && !r.fit) pill('Needs a bigger screen', 'no');
  const p = progress.get(r.appId);
  if (p != null) { const bar = el.querySelector('.progress'); bar.hidden = false; bar.firstChild.style.width = p + '%'; }
  const actions = el.querySelector('.actions');
  const btn = (label, cls, fn, off = false) => { const b = document.createElement('button'); b.textContent = label; b.className = cls; b.disabled = busy || !device || off; b.onclick = fn; actions.append(b); };
  if (r.rel && (!r.dev || r.update)) btn(r.dev ? 'Update' : 'Install', 'primary', () => install(r), !r.fit);
  if (r.dev) btn('Open', '', () => run(() => usb.start(r.appId)));
  if (r.dev) btn('Uninstall', 'danger', () => confirmThen(`Uninstall ${r.title} from your Gotchi?`, () => run(() => usb.uninstall(r.appId))));
  return el;
}

function confirmThen(text, fn) {
  $('confirmText').textContent = text; const d = $('confirm'); d.showModal();
  $('confirmNo').onclick = () => d.close();
  $('confirmYes').textContent = /reset/i.test(text) ? 'Erase and flash' : 'Uninstall';
  $('confirmYes').onclick = () => { d.close(); fn(); };
}

// ---------- device ----------
async function refreshDevice() {
  if (!usb) { device = null; return showDevice(); }
  const status = await usb.status();
  const list = await usb.listApps();
  device = { status, ...list };
  showDevice();
}
function showDevice() {
  $('dot').classList.toggle('on', !!device);
  $('connect').textContent = device ? 'Disconnect' : 'Connect Gotchi';
  $('meter').hidden = !device;
  if (device) {
    const s = device.status;
    $('devName').textContent = `${s.board || 'Gotchi'} connected`;
    $('devInfo').textContent = `${s.chip} · ${s.width}×${s.height} · firmware ${s.firmware} · ${device.apps.length} apps · ${fmt(device.storeUsed)} of ${fmt(device.storeTotal)}`;
    $('meter').firstChild.style.width = (device.storeTotal ? 100 * device.storeUsed / device.storeTotal : 0) + '%';
  } else { $('devName').textContent = 'No Gotchi connected'; $('devInfo').textContent = 'Plug it in over USB, then Connect'; }
  render();
}

async function run(fn) {
  busy = true; render();
  try { await fn(); } catch (e) { alertMsg(e.message); }
  busy = false;
  try { await refreshDevice(); } catch (e) { alertMsg(e.message); render(); }
}

async function install(r) {
  await run(async () => {
    progress.set(r.appId, 0); render();
    const res = await fetch(r.rel.url);
    if (!res.ok) throw new Error(`Download failed for ${r.title} (${res.status})`);
    const bytes = new Uint8Array(await res.arrayBuffer());
    let last = -1;
    await usb.install(r.appId, bytes, p => { progress.set(r.appId, p); if (p - last >= 3 || p === 100) { last = p; render(); } });
    progress.delete(r.appId);
  });
  progress.delete(r.appId);
}

function alertMsg(text) {
  const m = document.createElement('div'); m.className = 'msg warn'; m.textContent = text; m.setAttribute('role', 'alert');
  $('sections').before(m); setTimeout(() => m.remove(), 9000);
}

$('connect').onclick = async () => {
  if (usb) { await usb.disconnect(); usb = null; device = null; return showDevice(); }
  try {
    const c = new GotchiUsb(); await c.connect(); usb = c;
    await refreshDevice();
  } catch (e) { usb = null; if (e.name !== 'NotFoundError') alertMsg('Could not connect: ' + e.message); }
};
$('updateAll').onclick = async () => {
  const todo = catalog.map(a => ({ ...a, rel: releaseFor(a), dev: device?.apps.find(d => d.appId === a.appId) }))
    .filter(r => r.dev && r.rel && r.dev.sha256 && r.dev.sha256 !== r.rel.sha256 && fits(r.rel));
  for (const r of todo) await install(r);
};
$('q').oninput = render; $('filter').onchange = render;
if (!GotchiUsb.supported()) { $('noserial').hidden = false; $('connect').disabled = true; }
navigator.serial?.addEventListener('disconnect', () => { usb = null; device = null; showDevice(); });

// ---------- sign-in gate ----------
async function post(url, body) {
  const r = await fetch(url, { method: 'POST', headers: { 'Content-Type': 'application/json' }, body: JSON.stringify(body) });
  const j = await r.json().catch(() => ({})); if (!r.ok) throw new Error(j.error || 'Something went wrong'); return j;
}
async function showLogin() {
  $('login').hidden = false;
  const { googleClientId } = await fetch('/api/auth/config').then(r => r.json()).catch(() => ({}));
  if (!googleClientId) { $('loginMsg').textContent = 'Google sign-in is not set up yet.'; return; }
  await new Promise((ok, fail) => { const s = document.createElement('script'); s.src = 'https://accounts.google.com/gsi/client'; s.onload = ok; s.onerror = fail; document.head.append(s); });
  google.accounts.id.initialize({ client_id: googleClientId, callback: async ({ credential }) => {
    try { const { email } = await post('/api/auth/google', { credential }); await enter(email); }
    catch (err) { $('loginMsg').textContent = err.message; }
  } });
  google.accounts.id.renderButton($('googleBtn'), { theme: 'filled_black', size: 'large', shape: 'pill', text: 'signin_with' });
}
$('signout').onclick = async e => { e.preventDefault(); await post('/api/auth/me', {}); try { google.accounts.id.disableAutoSelect(); } catch {} location.reload(); };

async function enter(email) {
  $('login').hidden = true; $('appView').hidden = false; $('who').textContent = email;
  loadFirmware();
  await loadCatalog(); render();
}

// ---------- main firmware (esptool-js over Web Serial) ----------
let firmware = null;
async function loadFirmware() {
  const r = await fetch('/api/file?path=firmware/manifest.json');
  if (!r.ok) { $('fwMeta').textContent = 'No firmware published yet.'; return; }
  firmware = await r.json();
  $('fwTitle').textContent = `Main firmware v${firmware.version}`;
  $('fwMeta').textContent = `Cheeko Gotchi · ESP32-S3 · built ${firmware.built}`;
  $('fwUpdate').disabled = $('fwFull').disabled = !GotchiUsb.supported();
}
async function flash(mode) {
  const parts = mode === 'full' ? firmware.full : firmware.update;
  const log = t => { $('fwLog').hidden = false; $('fwLog').textContent += t + '\n'; $('fwLog').scrollTop = 1e9; };
  if (usb) { await usb.disconnect(); usb = null; device = null; showDevice(); }
  $('fwUpdate').disabled = $('fwFull').disabled = true; $('fwLog').textContent = '';
  const bar = $('fwProgress'); bar.hidden = false; bar.firstChild.style.width = '0%';
  let transport, restarted = false;
  try {
    const { ESPLoader, Transport } = await import('https://unpkg.com/esptool-js@0.5.7/bundle.js');
    const port = await navigator.serial.requestPort({ filters: [{ usbVendorId: 0x303a }] });
    log('Downloading firmware…');
    const files = [];
    for (const p of parts) {
      const res = await fetch('/api/file?path=firmware/' + p.file);
      if (!res.ok) throw new Error('Download failed: ' + p.file);
      const bytes = new Uint8Array(await res.arrayBuffer());
      let bin = ''; for (let i = 0; i < bytes.length; i += 0x8000) bin += String.fromCharCode.apply(null, bytes.subarray(i, i + 0x8000));
      files.push({ data: bin, address: p.offset });
    }
    transport = new Transport(port, true);
    const loader = new ESPLoader({ transport, baudrate: 460800, romBaudrate: 115200,
      terminal: { clean() {}, writeLine: log, write: t => { $('fwLog').textContent += t; } } });
    log('Connecting to ' + await loader.main());
    const total = files.reduce((n, f) => n + f.data.length, 0); const done = files.map(() => 0);
    await loader.writeFlash({ fileArray: files, flashSize: 'keep', flashMode: 'keep', flashFreq: 'keep', eraseAll: mode === 'full', compress: true,
      reportProgress: (i, written) => { done[i] = written; bar.firstChild.style.width = (100 * done.reduce((a, b) => a + b, 0) / total) + '%'; } });
    log('Firmware written. Restarting the Gotchi…');
    try { await loader.after('hard_reset'); } catch {}
    // USB-JTAG reset: hold RTS with DTR released, then let go.
    try {
      await transport.setDTR(false); await transport.setRTS(true);
      await new Promise(r => setTimeout(r, 120)); await transport.setRTS(false);
    } catch {}
    log('Done.');
    restarted = true;
  } catch (e) { if (e.name !== 'NotFoundError') log('Failed: ' + e.message); }
  try { await transport?.disconnect(); } catch {}
  $('fwUpdate').disabled = $('fwFull').disabled = false;
  if (restarted) $('restartNotice').showModal();
}
$('restartOk').onclick = () => $('restartNotice').close();
$('fwUpdate').onclick = () => flash('update');
$('fwFull').onclick = () => confirmThen('Full reset erases every app and setting on this Gotchi, then installs the firmware. You can reinstall apps from this page after. Continue?', () => flash('full'));

const me = await fetch('/api/auth/me').then(r => r.ok ? r.json() : null).catch(() => null);
if (me?.email) await enter(me.email); else await showLogin();
