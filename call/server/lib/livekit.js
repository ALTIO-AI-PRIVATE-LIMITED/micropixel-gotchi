import { AccessToken, RoomServiceClient } from 'livekit-server-sdk';
import { env } from './config.js';

function rooms() {
  // RoomServiceClient wants the HTTPS form of the wss:// URL.
  const host = env('LIVEKIT_URL').replace(/^ws/, 'http');
  return new RoomServiceClient(host, env('LIVEKIT_API_KEY'), env('LIVEKIT_API_SECRET'));
}

export async function createRoom(name) {
  // The room closes itself if nobody joins or everyone leaves.
  await rooms().createRoom({ name, emptyTimeout: 60, departureTimeout: 5, maxParticipants: 2 });
}

export async function deleteRoom(name) {
  try {
    await rooms().deleteRoom(name);
  } catch (err) {
    // Already gone (both sides hung up, or it timed out) is fine.
    console.warn(`deleteRoom ${name}: ${err.message}`);
  }
}

export async function joinToken(room, identity, name) {
  const token = new AccessToken(env('LIVEKIT_API_KEY'), env('LIVEKIT_API_SECRET'), {
    identity,
    name,
    ttl: '10m',
  });
  token.addGrant({ roomJoin: true, room, canPublish: true, canSubscribe: true });
  return token.toJwt();
}
