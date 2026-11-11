// GET ?scope=approved|mine|review lists submissions; POST (multipart) submits an app.
import { Readable } from 'node:stream';
import { randomUUID } from 'node:crypto';
import { requireSession } from '../lib/auth.js';
import { LIMITS, PREFIX, UserError, allMeta, cleanText, coverType, inspectBundle, isAdmin, ownerView, putFile, reservedAppIds, saveMeta } from '../lib/submissions.js';

export const config = { api: { bodyParser: false }, maxDuration: 60 };
const MAX_BODY = LIMITS.bundle + LIMITS.cover + 100_000; // stays under Vercel's 4.5 MB request limit

async function create(req, res, email) {
  if (!String(req.headers['content-type'] || '').startsWith('multipart/form-data')) throw new UserError('Send the form as multipart.');
  if (!(Number(req.headers['content-length']) <= MAX_BODY)) return res.status(413).json({ error: 'The upload is too large. Apps can be up to 3.8 MB and thumbnails up to 500 KB.' });
  const form = await new Request('http://local/', { method: 'POST', headers: { 'content-type': req.headers['content-type'] }, body: Readable.toWeb(req), duplex: 'half' }).formData();
  const title = cleanText(form.get('title'), LIMITS.title + 1), description = cleanText(form.get('description'), LIMITS.description + 1);
  const bundleFile = form.get('bundle'), coverFile = form.get('cover');
  if (!title || title.length > LIMITS.title) throw new UserError(`Give the app a title of up to ${LIMITS.title} characters.`);
  if (description.length < 20 || description.length > LIMITS.description) throw new UserError(`Write a description of 20 to ${LIMITS.description} characters.`);
  if (!(bundleFile instanceof File) || !bundleFile.size) throw new UserError('Choose the app bundle file.');
  if (!(coverFile instanceof File) || !coverFile.size) throw new UserError('Choose a thumbnail image.');
  if (bundleFile.size > LIMITS.bundle) throw new UserError('Apps can be up to 3.8 MB.');
  if (coverFile.size > LIMITS.cover) throw new UserError('Thumbnails can be up to 500 KB.');
  const bundle = Buffer.from(await bundleFile.arrayBuffer()), cover = Buffer.from(await coverFile.arrayBuffer());
  const info = inspectBundle(bundle);
  const image = coverType(cover);
  if (!image) throw new UserError('The thumbnail must be a PNG, JPEG or WebP image.');

  const [existing, reserved] = await Promise.all([allMeta(), reservedAppIds()]);
  if (reserved.has(info.appId)) throw new UserError(`The app ID ${info.appId} belongs to an existing app. Use your own app ID in app.json.`);
  const live = existing.filter(m => m.status === 'pending' || m.status === 'approved');
  if (live.some(m => m.appId === info.appId && m.developer !== email)) throw new UserError(`The app ID ${info.appId} is already used by another developer.`);
  if (live.some(m => m.appId === info.appId && m.version === info.version)) throw new UserError(`Version ${info.version} of this app is already submitted. Raise the version in app.json and rebuild.`);
  if (existing.filter(m => m.status === 'pending' && m.developer === email).length >= LIMITS.pendingPerDeveloper) throw new UserError(`You already have ${LIMITS.pendingPerDeveloper} apps waiting for review.`);

  const id = randomUUID();
  await Promise.all([
    putFile(`${PREFIX}/${id}/bundle.bin`, bundle, 'application/octet-stream'),
    putFile(`${PREFIX}/${id}/cover.${image.ext}`, cover, image.mime),
  ]);
  const meta = { id, status: 'pending', title, description, developer: email, ...info, coverExt: image.ext, coverMime: image.mime, createdAt: new Date().toISOString() };
  await saveMeta(meta); // written last, so a listed submission always has its files
  console.log(JSON.stringify({ event: 'app_submitted', id, appId: info.appId, version: info.version, developer: email }));
  res.status(201).json({ submission: ownerView(meta) });
}

export default async function handler(req, res) {
  const email = await requireSession(req, res);
  if (!email) return;
  try {
    if (req.method === 'POST') return await create(req, res, email);
    if (req.method !== 'GET') return res.status(405).end();
    const scope = String(req.query.scope || 'approved');
    res.setHeader('Cache-Control', 'private, no-store');
    const metas = await allMeta();
    if (scope === 'mine') return res.status(200).json({ submissions: metas.filter(m => m.developer === email).map(ownerView) });
    if (scope === 'review') {
      if (!isAdmin(email)) return res.status(403).json({ error: 'Reviewers only' });
      return res.status(200).json({ submissions: metas.filter(m => m.status !== 'rejected').map(m => ({ ...ownerView(m), developer: m.developer, sha256: m.sha256, requirements: m.requirements, target: m.target })) });
    }
    // Developer emails stay private: the catalog only shows that it's a community app.
    const newest = new Map();
    for (const m of metas) if (m.status === 'approved' && !newest.has(m.appId)) newest.set(m.appId, m);
    return res.status(200).json({ apps: [...newest.values()].map(m => ({
      appId: m.appId, title: m.title, summary: m.description, cover: `/api/submission-file?id=${m.id}&kind=cover`,
      release: { target: m.target, version: m.version, sizeBytes: m.sizeBytes, sha256: m.sha256, requirements: m.requirements, url: `/api/submission-file?id=${m.id}&kind=bundle` },
    })) });
  } catch (e) {
    if (e instanceof UserError) return res.status(400).json({ error: e.message });
    console.error(e);
    res.status(500).json({ error: 'Something went wrong. Please try again.' });
  }
}
