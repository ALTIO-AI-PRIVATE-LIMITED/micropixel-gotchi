// POST /api/meet/join  (from the Gotchi)   Auth: Bearer DEVICE_SECRET
// Body: {"eventId": "..."}
// Makes a LiveKit room for the Gotchi and sends a Recall.ai bot into the
// meeting. The bot's web page (public/bot.html) joins the same room and relays
// audio both ways. Returns the Gotchi's join details.
import crypto from 'node:crypto';
import { deviceAuthorized, env, publicUrl } from '../../lib/config.js';
import { meeting } from '../../lib/google.js';
import { createRoom, deleteRoom, joinToken } from '../../lib/livekit.js';
import { createBot } from '../../lib/recall.js';

export default async function handler(req, res) {
  if (req.method !== 'POST') return res.status(405).json({ error: 'POST only' });
  if (!deviceAuthorized(req)) return res.status(401).json({ error: 'unauthorized' });
  const eventId = req.body?.eventId;
  if (!eventId) return res.status(400).json({ error: 'eventId required' });

  const meetId = crypto.randomUUID();
  const room = `meet-${meetId}`;
  try {
    const event = await meeting(eventId);
    if (!event.hangoutLink) return res.status(404).json({ error: 'no Meet link on that event' });

    await createRoom(room);
    const [gotchiToken, botToken] = await Promise.all([
      joinToken(room, 'gotchi', 'CGotchi'),
      joinToken(room, 'meet-bot', 'Meet'),
    ]);
    // The token rides in the fragment, which browsers never send to a server.
    const page = `${publicUrl(req)}/bot.html#${new URLSearchParams({ url: env('LIVEKIT_URL'), token: botToken })}`;
    let botId;
    try {
      botId = await createBot(event.hangoutLink, page);
    } catch (err) {
      await deleteRoom(room);
      throw err;
    }
    res.status(200).json({ meetId, botId, url: env('LIVEKIT_URL'), token: gotchiToken, identity: 'gotchi' });
  } catch (err) {
    console.error(err);
    res.status(502).json({ error: err.message });
  }
}
