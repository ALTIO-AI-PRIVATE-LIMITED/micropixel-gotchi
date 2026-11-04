import { sessionEmail } from '../../lib/auth.js';

export default async function handler(req, res) {
  if (req.method === 'POST') { // sign out
    res.setHeader('Set-Cookie', 'gotchi_session=; Path=/; HttpOnly; Secure; SameSite=Lax; Max-Age=0');
    return res.status(200).json({ email: null });
  }
  const email = await sessionEmail(req).catch(() => null);
  res.status(email ? 200 : 401).json({ email });
}
