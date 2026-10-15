// POST /api/call/end  (from the Gotchi: cancel while ringing, or hang up)
// Body: {"callId": "..."}   Auth: Bearer DEVICE_SECRET
import { authorized, pushTokenFor } from '../../lib/config.js';
import { sendVoipPush } from '../../lib/apns.js';
import { deleteRoom } from '../../lib/livekit.js';
import { endMeetCall, isMeetCall } from '../../lib/meet.js';

export default async function handler(req, res) {
  if (req.method !== 'POST') return res.status(405).json({ error: 'POST only' });
  if (!authorized(req, 'DEVICE_SECRET')) return res.status(401).json({ error: 'unauthorized' });
  const callId = req.body?.callId;
  if (!callId) return res.status(400).json({ error: 'callId required' });
  if (isMeetCall(callId)) {
    await endMeetCall(callId); // Takes the bot out of the meeting.
    return res.status(200).json({ ok: true });
  }

  await deleteRoom(`call-${callId}`);
  try {
    // Stops CallKit ringing if the phone has not answered yet.
    await sendVoipPush(pushTokenFor('iphone'), { type: 'end', callId });
  } catch (err) {
    console.warn(err.message);
  }
  res.status(200).json({ ok: true });
}
