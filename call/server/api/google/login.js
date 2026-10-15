// GET /api/google/login  (open once in your browser)
// Signs in to Google with read-only calendar access; /api/google/callback then
// shows the refresh token to put in GOOGLE_REFRESH_TOKEN.
import { env } from '../../lib/config.js';
import { publicUrl } from '../../lib/meet.js';

export default function handler(req, res) {
  const params = new URLSearchParams({
    client_id: env('GOOGLE_CLIENT_ID'),
    redirect_uri: `${publicUrl(req)}/api/google/callback`,
    response_type: 'code',
    scope: 'https://www.googleapis.com/auth/calendar.readonly',
    access_type: 'offline',
    prompt: 'consent', // Always return a refresh token.
  });
  res.redirect(302, `https://accounts.google.com/o/oauth2/v2/auth?${params}`);
}
