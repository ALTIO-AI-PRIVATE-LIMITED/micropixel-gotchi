// Relays the public MicroPixel App Store catalog (micropixel.ai sends no CORS headers).
import { requireSession } from '../lib/auth.js';

export default async function handler(req, res) {
  if (!await requireSession(req, res)) return;
  const upstream = await fetch('https://micropixel.ai/api/v1/store/apps', {
    headers: { 'User-Agent': 'Gotchi-App-Site/1.0', Accept: 'application/json' },
  });
  if (!upstream.ok) return res.status(502).json({ error: `micropixel.ai answered ${upstream.status}` });
  res.setHeader('Cache-Control', 's-maxage=600, stale-while-revalidate=3600');
  res.status(200).json(await upstream.json());
}
