// Signed session cookie, issued after Google sign-in.
// Needs SESSION_SECRET (any long random string) in the Vercel environment.
const enc = new TextEncoder();
const SESSION_DAYS = 30;

async function hmac(text) {
  const secret = process.env.SESSION_SECRET;
  if (!secret || secret.length < 32) throw new Error('SESSION_SECRET is not set');
  const key = await crypto.subtle.importKey('raw', enc.encode(secret), { name: 'HMAC', hash: 'SHA-256' }, false, ['sign']);
  return Buffer.from(await crypto.subtle.sign('HMAC', key, enc.encode(text))).toString('base64url');
}
function same(a, b) {
  if (a.length !== b.length) return false;
  let d = 0; for (let i = 0; i < a.length; i++) d |= a.charCodeAt(i) ^ b.charCodeAt(i); return d === 0;
}
export async function sessionCookie(email) {
  const exp = Date.now() + SESSION_DAYS * 864e5;
  const value = `${Buffer.from(email).toString('base64url')}.${exp}.${await hmac(`session|${email}|${exp}`)}`;
  return `gotchi_session=${value}; Path=/; HttpOnly; Secure; SameSite=Lax; Max-Age=${SESSION_DAYS * 86400}`;
}
export async function sessionEmail(req) {
  const cookie = (req.headers.cookie || '').split(/;\s*/).find(c => c.startsWith('gotchi_session='));
  if (!cookie) return null;
  const [b64, exp, sig] = cookie.slice('gotchi_session='.length).split('.');
  if (!b64 || !exp || !sig || Date.now() > +exp) return null;
  const email = Buffer.from(b64, 'base64url').toString();
  return same(sig, await hmac(`session|${email}|${exp}`)) ? email : null;
}
export async function requireSession(req, res) {
  const email = await sessionEmail(req).catch(() => null);
  if (!email) { res.status(401).json({ error: 'Sign in first' }); return null; }
  return email;
}
