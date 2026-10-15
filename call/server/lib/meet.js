// Google Meet calls: the Gotchi dials "meet:<key>" and, instead of ringing a
// phone, a Recall.ai bot joins the meeting. The bot's web page
// (public/bot.html) joins the call's LiveKit room and relays audio both ways.
import crypto from 'node:crypto';
import { env } from './config.js';
import { upcomingMeetings } from './google.js';
import { createRoom, deleteRoom, joinToken } from './livekit.js';
import { createBot, leaveBot } from './recall.js';

export const MEET_PREFIX = 'meet:';

// Short id for a calendar event, so it fits the Gotchi's 31-byte callee.
export function meetingKey(eventId) {
  return crypto.createHash('sha256').update(eventId).digest('hex').slice(0, 12);
}

// Public base URL of this deployment, e.g. https://gotchi-call.vercel.app
export function publicUrl(req) {
  return process.env.PUBLIC_URL || `https://${req.headers.host}`;
}

// Meet call ids look like "meet.<room id>.<Recall bot id>" (under 64 bytes),
// so /api/call/end can find both without storing anything.
export function isMeetCall(callId) {
  return callId.startsWith('meet.');
}

function roomFor(callId) {
  return `call-${callId.split('.').slice(0, 2).join('.')}`;
}

// Returns the Gotchi's join details, like a phone call.
export async function startMeetCall(req, key) {
  const { items } = await upcomingMeetings();
  const event = items.find((e) => meetingKey(e.id) === key);
  if (!event) {
    const err = new Error(`no upcoming meeting ${key}`);
    err.status = 404;
    throw err;
  }
  const roomId = `meet.${crypto.randomBytes(4).toString('hex')}`;
  const room = `call-${roomId}`;
  await createRoom(room);
  const [gotchiToken, botToken] = await Promise.all([
    joinToken(room, 'gotchi', 'CGotchi', '3h'),
    joinToken(room, 'meet-bot', 'Meet', '3h'),
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
  return { callId: `${roomId}.${botId}`, url: env('LIVEKIT_URL'), token: gotchiToken, identity: 'gotchi' };
}

export async function endMeetCall(callId) {
  const botId = callId.split('.')[2];
  await Promise.all([botId ? leaveBot(botId) : null, deleteRoom(roomFor(callId))]);
}
