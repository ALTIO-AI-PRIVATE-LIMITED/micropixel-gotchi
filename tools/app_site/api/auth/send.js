import { makeTicket, normalizeEmail, OTP_MINUTES } from '../../lib/auth.js';

// Emails a 6-digit code through Resend (RESEND_API_KEY, MAIL_FROM).
export default async function handler(req, res) {
  if (req.method !== 'POST') return res.status(405).end();
  const email = normalizeEmail(req.body?.email);
  if (!email) return res.status(400).json({ error: 'Enter a valid email address' });
  const code = String(crypto.getRandomValues(new Uint32Array(1))[0] % 1e6).padStart(6, '0');
  const sent = await fetch('https://api.resend.com/emails', {
    method: 'POST',
    headers: { Authorization: `Bearer ${process.env.RESEND_API_KEY}`, 'Content-Type': 'application/json' },
    body: JSON.stringify({
      from: process.env.MAIL_FROM || 'Gotchi Apps <onboarding@resend.dev>',
      to: [email],
      subject: `${code} is your Gotchi Apps code`,
      text: `Your Gotchi Apps sign-in code is ${code}. It expires in ${OTP_MINUTES} minutes.\n\nIf you didn't ask for it, ignore this email.`,
    }),
  });
  if (!sent.ok) return res.status(502).json({ error: 'Could not send the email. Try again in a minute.' });
  res.status(200).json({ ticket: await makeTicket(email, code) });
}
