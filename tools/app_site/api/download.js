// Relays one App Store release bundle so the browser can send it to the Gotchi over USB.
import { requireSession } from '../lib/auth.js';

export const config = { maxDuration: 60 };
export default async function handler(req, res) {
  if (!await requireSession(req, res)) return;
  const id = String(req.query.id || '');
  if (!/^[0-9a-f-]{36}$/.test(id)) return res.status(400).json({ error: 'bad release id' });
  const upstream = await fetch(`https://micropixel.ai/api/v1/store/releases/${id}/download`, {
    headers: { 'User-Agent': 'Gotchi-App-Site/1.0' },
  });
  if (!upstream.ok) return res.status(502).json({ error: `micropixel.ai answered ${upstream.status}` });
  res.setHeader('Content-Type', 'application/octet-stream');
  res.setHeader('Cache-Control', 'public, s-maxage=31536000, immutable');
  res.status(200).send(Buffer.from(await upstream.arrayBuffer()));
}
