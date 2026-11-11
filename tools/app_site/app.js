import { GotchiUsb } from './gotchi-usb.js';

const STORE = 'https://micropixel.ai';
// Cheeko Gotchi: ESP32-S3, 296x240 screen. Used until a real Gotchi is connected.
const GOTCHI = { target: 'xtensa', width: 296, height: 240 };
const $ = id => document.getElementById(id);
let catalog = [];        // merged list of installable apps
let device = null;       // { status, apps, storeUsed, storeTotal }
let usb = null, busy = false, filter = 'all', firstPaint = true;
const progress = new Map();

// ---------- catalog: micropixel.ai store + our own prebuilt apps ----------
function cmpVersion(a = '', b = '') {
  const pa = a.split(/[.-]/).map(Number), pb = b.split(/[.-]/).map(Number);
  for (let i = 0; i < Math.max(pa.length, pb.length); i++) { const d = (pa[i] || 0) - (pb[i] || 0); if (d) return Math.sign(d); }
  return 0;
}

async function loadCatalog() {
  const [store, ours, info, community] = await Promise.all([
    fetch('/api/store', { signal: AbortSignal.timeout(15000) }).then(r => r.ok ? r.json() : { apps: [] }).catch(() => ({ apps: [] })),
    fetch('/api/file?path=apps/index.json').then(r => r.ok ? r.json() : { apps: [] }).catch(() => ({ apps: [] })),
    fetch('/app_info.json').then(r => r.ok ? r.json() : {}).catch(() => ({})),
    fetch('/api/submissions?scope=approved').then(r => r.ok ? r.json() : { apps: [] }).catch(() => ({ apps: [] })),
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
  for (const a of community.apps || []) {
    if (byId.has(a.appId)) continue; // an official app always keeps its ID
    byId.set(a.appId, { appId: a.appId, title: a.title, summary: a.summary, developer: 'Community', cover: a.cover, source: 'Community apps', info: { about: a.summary }, releases: [a.release] });
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
  const q = $('q').value.toLowerCase();
  const installed = new Map((device?.apps || []).map(a => [a.appId, a]));
  const rows = catalog.map(a => {
    const rel = releaseFor(a), dev = installed.get(a.appId);
    const update = dev && rel && (dev.sha256 ? dev.sha256 !== rel.sha256 && (!dev.version || cmpVersion(rel.version, dev.version) >= 0) : cmpVersion(rel.version, dev.version || '0') > 0);
    return { ...a, rel, dev, update, fit: fits(rel) };
  }).filter(r => r.rel && r.fit);
  for (const [id, d] of installed) if (!catalog.some(a => a.appId === id))
    rows.push({ appId: id, title: d.displayName, summary: 'Installed on your Gotchi. Not in any catalog.', source: 'Only on this Gotchi', releases: [], rel: null, dev: d, fit: true });

  renderChips(rows);
  $('appTotal').textContent = `${rows.length} ${rows.length === 1 ? 'app' : 'apps'}`;
  const groups = new Map();
  for (const r of rows) {
    if (q && !(r.title + ' ' + r.appId + ' ' + r.developer).toLowerCase().includes(q)) continue;
    if (filter === 'installed' && !r.dev) continue;
    if (filter === 'updates' && !r.update) continue;
    if (filter.startsWith('kind:') && groupOf(r) !== filter.slice(5)) continue;
    if (!groups.has(r.source)) groups.set(r.source, []);
    groups.get(r.source).push(r);
  }
  $('updateAll').disabled = busy || !rows.some(r => r.update);
  const root = $('sections'); root.innerHTML = '';
  for (const name of ['Cheeko apps', 'MicroPixel Store', 'Community apps', 'Only on this Gotchi']) {
    const list = groups.get(name); if (!list?.length) continue;
    const h = document.createElement('h2'); h.textContent = name; const n = document.createElement('span'); n.className = 'count'; n.textContent = list.length; h.append(n); root.append(h);
    const grid = document.createElement('div'); grid.className = 'grid'; root.append(grid);
    list.forEach((r, i) => { const c = card(r); c.style.setProperty('--i', i); grid.append(c); });
  }
  if (!root.children.length) root.innerHTML = '<p class="msg">No apps match.</p>';
  if (firstPaint && rows.length) { firstPaint = false; root.classList.add('enter'); setTimeout(() => root.classList.remove('enter'), 1600); }
}

const GROUPS = ['Games', 'Multi-Gotchi', 'Companions', 'Tools'];
function groupOf(r) {
  const k = r.info?.kind; if (!k) return null;
  return /game/i.test(k) ? 'Games' : /two gotchis|group/i.test(k) ? 'Multi-Gotchi' : /companion/i.test(k) ? 'Companions' : 'Tools';
}
let chipKey = '';
function renderChips(rows) {
  const list = [['all', 'All apps', rows.length]];
  if (device) list.push(['installed', 'Installed', rows.filter(r => r.dev).length], ['updates', 'Updates', rows.filter(r => r.update).length]);
  const kinds = new Map();
  for (const r of rows) { const g = groupOf(r); if (g) kinds.set(g, (kinds.get(g) || 0) + 1); }
  for (const g of GROUPS) if (kinds.has(g)) list.push(['kind:' + g, g, kinds.get(g)]);
  if (!list.some(c => c[0] === filter)) filter = 'all';
  const key = JSON.stringify([list, filter]);
  if (key === chipKey) return;
  chipKey = key;
  const box = $('chips'); box.innerHTML = '';
  for (const [id, label, n] of list) {
    const b = document.createElement('button'); b.className = 'chip'; b.textContent = label; b.setAttribute('aria-pressed', id === filter);
    const c = document.createElement('span'); c.className = 'count'; c.textContent = n; b.append(c);
    b.onclick = () => { filter = id; render(); box.querySelector('[aria-pressed=true]')?.focus(); };
    box.append(b);
  }
}

// The Gotchi drawn in the header plays through the app thumbnails.
let heroStarted = false;
function startHero() {
  if (heroStarted) return;
  const [a, b] = $('heroScreen').querySelectorAll('img');
  const pool = () => catalog.filter(x => x.cover);
  if (!pool().length) return;
  heroStarted = true;
  let i = 0, top = a;
  const show = () => {
    const list = pool(), app = list[i++ % list.length], next = top === a ? b : a;
    next.onload = () => { next.classList.add('show'); top.classList.remove('show'); top = next; $('nowShowing').textContent = app.title; };
    next.src = app.cover;
  };
  a.onload = () => { a.classList.add('show'); $('nowShowing').textContent = pool()[0].title; };
  a.src = pool()[0].cover; i = 1;
  if (!matchMedia('(prefers-reduced-motion: reduce)').matches) setInterval(show, 3200);
}

// ---------- tabs ----------
const VIEWS = ['apps', 'firmware', 'developers', 'review'];
function showView(name) {
  if (!VIEWS.includes(name) || (name === 'review' && $('tabReview').hidden)) name = 'apps';
  for (const v of VIEWS) $('view-' + v).hidden = v !== name;
  for (const t of document.querySelectorAll('[data-tab]')) { if (t.dataset.tab === name) t.setAttribute('aria-current', 'page'); else t.removeAttribute('aria-current'); }
  scrollTo(0, 0);
}
addEventListener('hashchange', () => showView(location.hash.slice(1)));

function card(r) {
  const el = document.createElement('div'); el.className = 'card';
  el.innerHTML = `<div class="cover"></div><div class="body"><h3></h3><div class="meta"></div><div class="pills"></div><div class="sum"></div><details class="more" hidden><summary>About this app</summary><div class="info"></div></details><div class="progress" hidden><div></div></div><div class="actions"></div></div>`;
  const cover = el.querySelector('.cover');
  if (r.cover) { const img = new Image(); img.loading = 'lazy'; img.alt = ''; img.onerror = () => { cover.textContent = r.title.slice(0, 14); }; img.src = r.cover; cover.append(img); } else cover.textContent = r.title.slice(0, 14);
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
    if (r.info.credit) {
      const c = r.info.credit, h = document.createElement('b'); h.textContent = 'Credit and source'; const t = document.createElement('p');
      const link = (href, text) => { const a = document.createElement('a'); a.href = href; a.textContent = text; a.target = '_blank'; a.rel = 'noopener'; return a; };
      if (c.own) t.append(`Our own app, ${c.license}. `, link(c.source, 'Source code')); else t.append(`Original by ${c.by} (${c.license}). `, link(c.repo, 'Original code'), ' · ', link(c.source, 'Gotchi port code'));
      box.append(h, t);
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
  const btn = (label, cls, fn, off = false) => { const b = document.createElement('button'); b.textContent = label; b.className = cls; b.disabled = busy || !device || off; if (!device) b.title = 'Connect your Gotchi first'; b.onclick = fn; actions.append(b); };
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
  $('toasts').append(m); setTimeout(() => m.remove(), 9000);
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
$('q').oninput = render;
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
  showView(location.hash.slice(1));
  loadDeveloperArea();
  await loadCatalog(); render(); startHero();
}

// ---------- developer submissions and review ----------
const STATUS = { pending: 'Waiting for review', approved: 'Approved and live', rejected: 'Rejected' };
function subCard(s) {
  const el = document.createElement('div'); el.className = 'panel sub';
  el.innerHTML = '<img alt=""><div class="txt"><h3></h3><div class="meta"></div><p></p></div>';
  el.querySelector('img').src = s.cover;
  el.querySelector('h3').textContent = s.title;
  el.querySelector('p').textContent = s.description;
  return el;
}
function addLine(parent, text, cls = 'status') { const d = document.createElement('div'); d.className = cls; d.textContent = text; parent.append(d); return d; }

async function loadMine() {
  const r = await fetch('/api/submissions?scope=mine');
  const { submissions = [] } = r.ok ? await r.json() : {};
  const root = $('mine'); root.innerHTML = '';
  if (!submissions.length) return;
  const h = document.createElement('h2'); h.textContent = 'Your submissions'; root.append(h);
  for (const s of submissions) {
    const el = subCard(s), txt = el.querySelector('.txt');
    txt.querySelector('.meta').textContent = `${s.appId} · v${s.version} · ${fmt(s.sizeBytes)}`;
    addLine(txt, STATUS[s.status], `status ${s.status === 'approved' ? 'good' : s.status === 'rejected' ? 'bad' : ''}`);
    if (s.note) addLine(txt, 'Reviewer note: ' + s.note);
    root.append(el);
  }
}

async function testOnGotchi(s, msg) {
  if (!usb || !device) { msg.textContent = 'Connect your Gotchi at the top of the page first.'; return; }
  if (busy) return;
  busy = true; render();
  try {
    msg.textContent = 'Downloading…';
    const r = await fetch(`/api/submission-file?id=${s.id}&kind=bundle`);
    if (!r.ok) throw new Error(`Download failed (${r.status})`);
    await usb.install(s.appId, new Uint8Array(await r.arrayBuffer()), p => { msg.textContent = `Installing… ${p}%`; });
    await usb.start(s.appId);
    msg.textContent = 'Installed and opened on your Gotchi.';
  } catch (e) { msg.textContent = e.message; }
  busy = false;
  try { await refreshDevice(); } catch {}
}

async function decide(s, decision, note, msg) {
  try { await post('/api/review', { id: s.id, decision, note }); await Promise.all([loadReview(), loadMine(), loadCatalog()]); render(); }
  catch (e) { msg.textContent = e.message; }
}

function reviewCard(s) {
  const el = subCard(s), txt = el.querySelector('.txt');
  txt.querySelector('.meta').textContent = `From ${s.developer} · ${new Date(s.createdAt).toLocaleDateString()}`;
  const req = s.requirements || {}, needs = [...(req.required || []), ...(req.optional || []).map(x => x + ' (optional)')].join(', ') || 'none listed';
  addLine(txt, `${s.appId} · v${s.version} · ${fmt(s.sizeBytes)} · ${s.target}`, 'meta');
  addLine(txt, `Uses: ${needs}`, 'meta');
  addLine(txt, `SHA-256: ${s.sha256}`, 'meta');
  const actions = document.createElement('div'); actions.className = 'actions'; txt.append(actions);
  const msg = addLine(txt, '');
  const note = document.createElement('textarea'); note.placeholder = 'Tell the developer why (required to reject or take down)'; note.maxLength = 500; note.rows = 2; note.hidden = true;
  note.style.cssText = 'display:block;width:100%;box-sizing:border-box;margin-top:8px';
  const btn = (label, cls, fn) => { const b = document.createElement('button'); b.type = 'button'; b.textContent = label; b.className = cls; b.onclick = fn; actions.append(b); return b; };
  btn('Test on my Gotchi', '', () => testOnGotchi(s, msg));
  if (s.status === 'pending') btn('Approve', 'primary', () => decide(s, 'approve', '', msg));
  const refuse = btn(s.status === 'approved' ? 'Take down' : 'Reject', 'danger', () => {
    if (note.hidden || note.value.trim() === '') { note.hidden = false; note.focus(); msg.textContent = 'Write a note for the developer, then press the button again.'; return; }
    refuse.disabled = true; decide(s, 'reject', note.value, msg).finally(() => { refuse.disabled = false; });
  });
  actions.after(note);
  return el;
}

async function loadReview() {
  const r = await fetch('/api/submissions?scope=review');
  if (!r.ok) return;
  const { submissions = [] } = await r.json();
  const root = $('review'); root.innerHTML = '';
  const pending = submissions.filter(s => s.status === 'pending'), live = submissions.filter(s => s.status === 'approved');
  $('reviewTitle').textContent = `Review apps · ${pending.length} waiting`; $('reviewCount').textContent = pending.length || '';
  if (!pending.length) addLine(root, 'Nothing is waiting for review.', 'msg');
  for (const s of pending) root.append(reviewCard(s));
  if (live.length) { const h = document.createElement('h2'); h.textContent = 'Live community apps'; root.append(h); for (const s of live) root.append(reviewCard(s)); }
}

async function loadDeveloperArea() {
  loadMine();
  const me = await fetch('/api/auth/me').then(r => r.ok ? r.json() : null).catch(() => null);
  if (me?.admin) { $('tabReview').hidden = false; loadReview(); if (location.hash === '#review') showView('review'); }
}

$('submitForm').onsubmit = async e => {
  e.preventDefault();
  const form = e.target, msg = $('submitMsg'), data = new FormData(form);
  const fail = t => { msg.className = 'status bad'; msg.textContent = t; };
  if (data.get('bundle').size > 3_800_000) return fail('Apps can be up to 3.8 MB.');
  if (data.get('cover').size > 500_000) return fail('Thumbnails can be up to 500 KB.');
  $('submitBtn').disabled = true; msg.className = 'status'; msg.textContent = 'Uploading…';
  try {
    const r = await fetch('/api/submissions', { method: 'POST', body: data });
    const j = await r.json().catch(() => ({}));
    if (!r.ok) throw new Error(j.error || 'The upload failed. Please try again.');
    form.reset(); msg.className = 'status good'; msg.textContent = 'Submitted. It will appear in the catalog once it is approved.';
    await loadMine();
    if (!$('tabReview').hidden) loadReview();
  } catch (err) { fail(err.message); }
  $('submitBtn').disabled = false;
};

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
