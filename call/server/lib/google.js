// Google Calendar, read-only, for the one account in GOOGLE_REFRESH_TOKEN.
import { env } from './config.js';

const API = 'https://www.googleapis.com/calendar/v3/calendars/primary';

async function accessToken() {
  const res = await fetch('https://oauth2.googleapis.com/token', {
    method: 'POST',
    body: new URLSearchParams({
      client_id: env('GOOGLE_CLIENT_ID'),
      client_secret: env('GOOGLE_CLIENT_SECRET'),
      refresh_token: env('GOOGLE_REFRESH_TOKEN'),
      grant_type: 'refresh_token',
    }),
  });
  if (!res.ok) throw new Error(`Google token: HTTP ${res.status} ${await res.text()}`);
  return (await res.json()).access_token;
}

async function get(path, params = {}) {
  const url = `${API}${path}?${new URLSearchParams(params)}`;
  const res = await fetch(url, { headers: { Authorization: `Bearer ${await accessToken()}` } });
  if (!res.ok) throw new Error(`Google Calendar: HTTP ${res.status} ${await res.text()}`);
  return res.json();
}

function declined(event) {
  return (event.attendees || []).some((a) => a.self && a.responseStatus === 'declined');
}

// Meetings with a Meet link that are on now or start in the next 24 hours.
export async function upcomingMeetings() {
  const now = Date.now();
  const data = await get('/events', {
    timeMin: new Date(now).toISOString(), // Includes meetings already under way.
    timeMax: new Date(now + 24 * 3600 * 1000).toISOString(),
    singleEvents: 'true',
    orderBy: 'startTime',
    maxResults: '20',
  });
  const items = (data.items || []).filter((e) => e.hangoutLink && e.start?.dateTime && !declined(e));
  return { timeZone: data.timeZone || 'UTC', items };
}

export async function meeting(eventId) {
  return get(`/events/${encodeURIComponent(eventId)}`);
}
