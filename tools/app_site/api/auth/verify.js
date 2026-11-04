import { checkTicket, normalizeEmail, sessionCookie } from '../../lib/auth.js';

export default async function handler(req, res) {
  if (req.method !== 'POST') return res.status(405).end();
  const email = normalizeEmail(req.body?.email);
  const ok = email && /^\d{6}$/.test(String(req.body?.code || '').trim()) && await checkTicket(email, req.body.code, req.body.ticket);
  if (!ok) return res.status(401).json({ error: 'That code is wrong or has expired' });
  res.setHeader('Set-Cookie', await sessionCookie(email));
  console.log(JSON.stringify({ event: 'sign_in', email }));
  res.status(200).json({ email });
}
