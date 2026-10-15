// GET /api/meetings  (from the Gotchi)   Auth: Bearer DEVICE_SECRET
// Returns the Meet meetings that are on now or start in the next 24 hours:
// {"now": 1760536800, "meetings": [{"id", "title", "start", "end", "label"}]}
// start/end are Unix seconds; label is the start time in the calendar's time zone.
import { deviceAuthorized } from '../lib/config.js';
import { upcomingMeetings } from '../lib/google.js';

// The Gotchi's font is ASCII only.
function ascii(text) {
  return (text || 'Meeting').normalize('NFKD').replace(/[^\x20-\x7e]/g, '').replace(/ +/g, ' ').trim().slice(0, 60) || 'Meeting';
}

function label(date, timeZone) {
  const day = (d) => d.toLocaleDateString('en-CA', { timeZone });
  const time = date.toLocaleTimeString('en-GB', { timeZone, hour: '2-digit', minute: '2-digit', hourCycle: 'h23' });
  return day(date) === day(new Date()) ? time : `Tmrw ${time}`;
}

export default async function handler(req, res) {
  if (!deviceAuthorized(req)) return res.status(401).json({ error: 'unauthorized' });
  try {
    const { timeZone, items } = await upcomingMeetings();
    const meetings = items.slice(0, 8).map((e) => {
      const start = new Date(e.start.dateTime);
      return {
        id: e.id,
        title: ascii(e.summary),
        start: Math.floor(start.getTime() / 1000),
        end: Math.floor(new Date(e.end.dateTime).getTime() / 1000),
        label: label(start, timeZone),
      };
    });
    res.status(200).json({ now: Math.floor(Date.now() / 1000), meetings });
  } catch (err) {
    console.error(err);
    res.status(502).json({ error: err.message });
  }
}
