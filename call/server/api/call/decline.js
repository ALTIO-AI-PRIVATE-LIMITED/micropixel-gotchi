// POST /api/call/decline  (from the iPhone app: declined, or hung up)
// Body: {"callId": "..."}   Auth: Bearer APP_SECRET
// Deleting the room disconnects the Gotchi, which then shows the call ended.
import { authorized } from '../../lib/config.js';
import { deleteRoom } from '../../lib/livekit.js';

export default async function handler(req, res) {
  if (req.method !== 'POST') return res.status(405).json({ error: 'POST only' });
  if (!authorized(req, 'APP_SECRET')) return res.status(401).json({ error: 'unauthorized' });
  const callId = req.body?.callId;
  if (!callId) return res.status(400).json({ error: 'callId required' });
  await deleteRoom(`call-${callId}`);
  res.status(200).json({ ok: true });
}
