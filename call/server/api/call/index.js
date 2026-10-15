// POST /api/call  (from the Gotchi)
// Body: {"to": "iphone"} or {"to": "meet:<key>"}   Auth: Bearer DEVICE_SECRET
// Creates a room, rings the phone with a VoIP push, and returns the Gotchi's join details.
// "meet:<key>" (a key from /api/meetings) sends a bot into that Google Meet instead.
import crypto from 'node:crypto';
import { authorized, env, pushTokenFor } from '../../lib/config.js';
import { sendVoipPush } from '../../lib/apns.js';
import { createRoom, joinToken } from '../../lib/livekit.js';
import { MEET_PREFIX, startMeetCall } from '../../lib/meet.js';

export default async function handler(req, res) {
  if (req.method !== 'POST') return res.status(405).json({ error: 'POST only' });
  if (!authorized(req, 'DEVICE_SECRET')) return res.status(401).json({ error: 'unauthorized' });

  const callee = req.body?.to || 'iphone';
  if (callee.startsWith(MEET_PREFIX)) {
    try {
      return res.status(200).json(await startMeetCall(req, callee.slice(MEET_PREFIX.length)));
    } catch (err) {
      console.error(err);
      return res.status(err.status || 502).json({ error: err.message });
    }
  }
  const pushToken = pushTokenFor(callee);
  if (!pushToken) return res.status(404).json({ error: `unknown callee ${callee}` });

  const callId = crypto.randomUUID();
  const room = `call-${callId}`;
  try {
    await createRoom(room);
    const [gotchiToken, phoneToken] = await Promise.all([
      joinToken(room, 'gotchi', 'CGotchi'),
      joinToken(room, callee, callee),
    ]);
    await sendVoipPush(pushToken, {
      type: 'incoming',
      callId,
      callerName: 'CGotchi',
      url: env('LIVEKIT_URL'),
      room,
      token: phoneToken,
    });
    res.status(200).json({ callId, url: env('LIVEKIT_URL'), token: gotchiToken, identity: 'gotchi' });
  } catch (err) {
    console.error(err);
    res.status(502).json({ error: err.message });
  }
}
