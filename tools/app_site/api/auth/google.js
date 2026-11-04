import { sessionCookie } from '../../lib/auth.js';

// Verifies a Google Identity Services ID token and starts a session.
// Needs GOOGLE_CLIENT_ID (the OAuth web client ID) in the Vercel environment.
export default async function handler(req, res) {
  if (req.method !== 'POST') return res.status(405).end();
  const credential = String(req.body?.credential || '');
  if (!credential) return res.status(400).json({ error: 'Missing Google credential' });
  const r = await fetch('https://oauth2.googleapis.com/tokeninfo?id_token=' + encodeURIComponent(credential));
  const info = r.ok ? await r.json() : null;
  const ok = info && info.aud === process.env.GOOGLE_CLIENT_ID &&
    ['accounts.google.com', 'https://accounts.google.com'].includes(info.iss) &&
    (info.email_verified === true || info.email_verified === 'true') && Number(info.exp) * 1000 > Date.now();
  if (!ok) return res.status(401).json({ error: 'Google sign-in could not be verified' });
  const email = String(info.email).toLowerCase();
  res.setHeader('Set-Cookie', await sessionCookie(email));
  console.log(JSON.stringify({ event: 'sign_in', email, name: info.name || '' }));
  res.status(200).json({ email });
}
