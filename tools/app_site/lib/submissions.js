// Developer app submissions, kept in a private Vercel Blob store.
// submissions/<id>/meta.json, bundle.bin and cover.<png|jpg|webp>.
import { readFile } from 'node:fs/promises';
import path from 'node:path';
import { createHash } from 'node:crypto';
import { get, list, put } from '@vercel/blob';

export const PREFIX = process.env.SUBMISSIONS_PREFIX || 'submissions';
export const LIMITS = { bundle: 3_800_000, cover: 500_000, pendingPerDeveloper: 5, title: 60, description: 2000 };
const GOTCHI = { width: 296, height: 240 };
const AOT_XTENSA = 2, AOT_UNCHECKED = 1 << 2;

export function isAdmin(email) {
  const admins = (process.env.ADMIN_EMAILS || '').toLowerCase().split(',').map(s => s.trim()).filter(Boolean);
  return !!email && admins.includes(email.toLowerCase());
}

export const cleanText = (value, max) => String(value ?? '').replace(/[\u0000-\u0008\u000b-\u001f\u007f]/g, '').trim().slice(0, max);

function fnv1a32(bytes) {
  let h = 0x811c9dc5;
  for (const b of bytes) { h ^= b; h = Math.imul(h, 0x01000193) >>> 0; }
  return h;
}

// The same checks the MicroPixel CLI makes before installing a Bundle v1, plus
// what this site needs: built for the Gotchi's chip and able to fit its screen.
export function inspectBundle(buf) {
  const fail = message => { throw new UserError(message); };
  const HEADER = 128, SECTION = 48, ALIGN = 64 * 1024;
  if (buf.length < HEADER || buf.length % ALIGN || buf.subarray(0, 8).toString('latin1') !== 'MPXBNDL\0') fail('That file is not a MicroPixel app bundle.');
  const u32 = o => buf.readUInt32LE(o);
  const [version, headerSize, size, toc] = [8, 12, 16, 20].map(u32);
  const [idLength, sections, abi] = [88, 92, 96].map(u32);
  const reservedClear = [108, 112, 116, 120, 124].every(o => u32(o) === 0);
  if (version !== 1 || headerSize !== HEADER || size !== buf.length || toc !== HEADER || idLength < 1 || idLength > 64 ||
      sections < 1 || sections > 128 || toc + sections * SECTION > buf.length || abi !== 1 || !reservedClear) fail('The app bundle header is invalid. Rebuild it with the current MicroPixel CLI.');
  const header = Buffer.from(buf.subarray(0, HEADER)); header.writeUInt32LE(0, 104);
  if (fnv1a32(header) !== u32(104)) fail('The app bundle is damaged (header check failed).');
  const appId = buf.subarray(24, 24 + idLength).toString('latin1');
  if (!/^[a-z0-9]+([.-][a-z0-9]+)+$/i.test(appId) || buf.subarray(24 + idLength, 88).some(b => b)) fail('The app bundle has an invalid app ID.');
  let target = 0, flags = 0, aot = 0, metadata = null;
  for (let i = 0; i < sections; i++) {
    const s = toc + i * SECTION, type = u32(s), offset = u32(s + 8), length = u32(s + 12);
    if (offset + length > buf.length) fail('The app bundle is damaged (bad section).');
    if (type === 1) { aot++; flags = u32(s + 36); target = u32(s + 40); }
    if (type === 3 && u32(s + 20) === 7) {
      try { metadata = JSON.parse(buf.subarray(offset, offset + length).toString('utf8')); } catch { fail('The app bundle metadata is damaged.'); }
    }
  }
  if (aot !== 1) fail('The bundle must contain compiled app code.');
  if (target !== AOT_XTENSA) fail('This bundle is not built for the Gotchi. Package it with --aot-target xtensa.');
  if (flags & AOT_UNCHECKED) fail('Bundles built with --unchecked-memory are not accepted.');
  if (!metadata?.version) fail('The bundle has no version. Add it to app.json and rebuild.');
  const requirements = metadata.requirements || {};
  const d = requirements.display;
  if (d) {
    const w = d.min_width || 0, h = d.min_height || 0;
    if (!((GOTCHI.width >= w && GOTCHI.height >= h) || (GOTCHI.height >= w && GOTCHI.width >= h))) fail(`This app needs a ${w}x${h} screen and the Gotchi is ${GOTCHI.width}x${GOTCHI.height}.`);
  }
  return { appId, version: String(metadata.version), requirements, target: 'xtensa', sizeBytes: buf.length, sha256: createHash('sha256').update(buf).digest('hex') };
}

export function coverType(buf) {
  if (buf.length > 8 && buf.subarray(0, 8).equals(Buffer.from([0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a]))) return { ext: 'png', mime: 'image/png' };
  if (buf.length > 3 && buf[0] === 0xff && buf[1] === 0xd8 && buf[2] === 0xff) return { ext: 'jpg', mime: 'image/jpeg' };
  if (buf.length > 12 && buf.toString('latin1', 0, 4) === 'RIFF' && buf.toString('latin1', 8, 12) === 'WEBP') return { ext: 'webp', mime: 'image/webp' };
  return null;
}

export class UserError extends Error {}

const OPTS = { access: 'private', addRandomSuffix: false, allowOverwrite: true };
export const putFile = (name, body, contentType) => put(name, body, { ...OPTS, contentType });
export const saveMeta = meta => putFile(`${PREFIX}/${meta.id}/meta.json`, JSON.stringify(meta), 'application/json');

export async function readFileBlob(pathname) {
  const found = await get(pathname, { access: 'private', useCache: false });
  if (!found || found.statusCode !== 200) return null;
  return Buffer.from(await new Response(found.stream).arrayBuffer());
}
export async function readMeta(id) {
  if (!/^[0-9a-f-]{36}$/.test(id)) return null;
  const data = await readFileBlob(`${PREFIX}/${id}/meta.json`);
  return data ? JSON.parse(data.toString('utf8')) : null;
}
export async function allMeta() {
  const paths = []; let cursor;
  do {
    const page = await list({ prefix: `${PREFIX}/`, cursor, limit: 1000 });
    paths.push(...page.blobs.map(b => b.pathname).filter(p => p.endsWith('/meta.json')));
    cursor = page.hasMore ? page.cursor : undefined;
  } while (cursor);
  const metas = await Promise.all(paths.map(async p => { const d = await readFileBlob(p); return d ? JSON.parse(d.toString('utf8')) : null; }));
  return metas.filter(Boolean).sort((a, b) => b.createdAt.localeCompare(a.createdAt));
}

// App IDs a developer may not claim: our own apps and the MicroPixel Store's.
export async function reservedAppIds() {
  const ours = JSON.parse(await readFile(path.join(process.cwd(), 'private/apps/index.json'), 'utf8'));
  const r = await fetch('https://micropixel.ai/api/v1/store/apps', { headers: { 'User-Agent': 'Gotchi-App-Site/1.0', Accept: 'application/json' }, signal: AbortSignal.timeout(10000) });
  if (!r.ok) throw new Error('store catalog unavailable');
  const store = await r.json();
  return new Set([...(ours.apps || []), ...(store.apps || [])].map(a => a.appId));
}

// What the developer sees about their own submission.
export const ownerView = m => ({ id: m.id, title: m.title, description: m.description, appId: m.appId, version: m.version, status: m.status, note: m.note || '', createdAt: m.createdAt, reviewedAt: m.reviewedAt || null, sizeBytes: m.sizeBytes, cover: `/api/submission-file?id=${m.id}&kind=cover` });
