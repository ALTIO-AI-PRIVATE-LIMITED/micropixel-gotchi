// GET /api/google/callback  (Google redirects here after /api/google/login)
import { env, publicUrl } from '../../lib/config.js';

const escape = (s) => String(s).replace(/[&<>"]/g, (c) => `&#${c.charCodeAt(0)};`);

export default async function handler(req, res) {
  res.setHeader('Content-Type', 'text/html; charset=utf-8');
  res.setHeader('Cache-Control', 'no-store');
  const code = req.query.code;
  if (!code) return res.status(400).send(`<p>Sign-in failed: ${escape(req.query.error || 'no code')}</p>`);

  const tokenRes = await fetch('https://oauth2.googleapis.com/token', {
    method: 'POST',
    body: new URLSearchParams({
      code,
      client_id: env('GOOGLE_CLIENT_ID'),
      client_secret: env('GOOGLE_CLIENT_SECRET'),
      redirect_uri: `${publicUrl(req)}/api/google/callback`,
      grant_type: 'authorization_code',
    }),
  });
  const data = await tokenRes.json();
  if (!tokenRes.ok || !data.refresh_token) {
    return res.status(502).send(`<p>Google did not return a refresh token: ${escape(JSON.stringify(data))}</p>`);
  }
  res.status(200).send(`<!doctype html><meta name="viewport" content="width=device-width">
<body style="font-family:system-ui;max-width:640px;margin:40px auto;padding:0 16px">
<h2>Calendar connected</h2>
<p>Add this as the <b>GOOGLE_REFRESH_TOKEN</b> environment variable in the Vercel project, then redeploy.
Don't share it: it reads your calendar.</p>
<textarea readonly rows="4" style="width:100%">${escape(data.refresh_token)}</textarea>
</body>`);
}
