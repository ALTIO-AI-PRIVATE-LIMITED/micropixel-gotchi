// Serves a submission's thumbnail or bundle. Approved apps are open to anyone
// signed in; anything else only to its developer and the reviewer.
import { requireSession } from '../lib/auth.js';
import { PREFIX, isAdmin, readFileBlob, readMeta } from '../lib/submissions.js';

export const config = { maxDuration: 60 };
export default async function handler(req, res) {
  const email = await requireSession(req, res);
  if (!email) return;
  const id = String(req.query.id || ''), kind = String(req.query.kind || '');
  if (!['bundle', 'cover'].includes(kind)) return res.status(400).json({ error: 'bad kind' });
  const meta = await readMeta(id);
  if (!meta || !(meta.status === 'approved' || meta.developer === email || isAdmin(email))) return res.status(404).json({ error: 'not found' });
  const name = kind === 'bundle' ? 'bundle.bin' : `cover.${meta.coverExt}`;
  const data = await readFileBlob(`${PREFIX}/${id}/${name}`);
  if (!data) return res.status(404).json({ error: 'not found' });
  res.setHeader('Content-Type', kind === 'bundle' ? 'application/octet-stream' : meta.coverMime);
  res.setHeader('X-Content-Type-Options', 'nosniff');
  res.setHeader('Cache-Control', 'private, no-cache');
  res.status(200).send(data);
}
