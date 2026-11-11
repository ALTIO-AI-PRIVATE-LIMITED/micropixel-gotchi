import { readFile } from 'node:fs/promises';
import path from 'node:path';
import { requireSession } from '../lib/auth.js';

// Serves our prebuilt app bundles and firmware images to signed-in people only.
const ROOT = path.join(process.cwd(), 'private');
export default async function handler(req, res) {
  if (!await requireSession(req, res)) return;
  const rel = String(req.query.path || '');
  if (!/^(apps|firmware)\/[A-Za-z0-9._-]+$/.test(rel)) return res.status(400).json({ error: 'bad path' });
  try {
    const data = await readFile(path.join(ROOT, rel));
    res.setHeader('Content-Type', rel.endsWith('.json') ? 'application/json' : 'application/octet-stream');
    res.setHeader('Cache-Control', 'private, no-cache');
    res.status(200).send(data);
  } catch { res.status(404).json({ error: 'not found' }); }
}
