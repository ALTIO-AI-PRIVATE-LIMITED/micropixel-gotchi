// POST /api/meet/leave  (from the Gotchi)   Auth: Bearer DEVICE_SECRET
// Body: {"meetId": "...", "botId": "..."}
// Takes the bot out of the meeting and closes the room.
import { deviceAuthorized } from '../../lib/config.js';
import { deleteRoom } from '../../lib/livekit.js';
import { leaveBot } from '../../lib/recall.js';

export default async function handler(req, res) {
  if (req.method !== 'POST') return res.status(405).json({ error: 'POST only' });
  if (!deviceAuthorized(req)) return res.status(401).json({ error: 'unauthorized' });
  const { meetId, botId } = req.body || {};
  if (!meetId || !botId) return res.status(400).json({ error: 'meetId and botId required' });

  await Promise.all([leaveBot(botId), deleteRoom(`meet-${meetId}`)]);
  res.status(200).json({ ok: true });
}
