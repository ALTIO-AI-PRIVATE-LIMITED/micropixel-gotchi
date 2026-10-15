import { AccessToken, RoomServiceClient } from 'livekit-server-sdk';
import { env } from './config.js';

function rooms() {
  // RoomServiceClient wants the HTTPS form of the wss:// URL.
  const host = env('LIVEKIT_URL').replace(/^ws/, 'http');
  return new RoomServiceClient(host, env('LIVEKIT_API_KEY'), env('LIVEKIT_API_SECRET'));
}

export async function createRoom(name) {
  // Two participants: the Gotchi and the Meet bot's web page.
  await rooms().createRoom({ name, emptyTimeout: 60, departureTimeout: 5, maxParticipants: 2 });
}

export async function deleteRoom(name) {
  try {
    await rooms().deleteRoom(name);
  } catch (err) {
    console.warn(`deleteRoom ${name}: ${err.message}`);
  }
}

// Long TTL: the bot page may sit in the Meet waiting room before it joins.
export async function joinToken(room, identity, name) {
  const token = new AccessToken(env('LIVEKIT_API_KEY'), env('LIVEKIT_API_SECRET'), {
    identity,
    name,
    ttl: '3h',
  });
  token.addGrant({ roomJoin: true, room, canPublish: true, canSubscribe: true });
  return token.toJwt();
}
