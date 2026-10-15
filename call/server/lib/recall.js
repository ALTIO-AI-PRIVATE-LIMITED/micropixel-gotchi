// Recall.ai meeting bots. https://docs.recall.ai
import { env } from './config.js';

function base() {
  return `https://${process.env.RECALL_REGION || 'us-west-2'}.recall.ai/api/v1`;
}

async function call(method, path, body) {
  const res = await fetch(`${base()}${path}`, {
    method,
    headers: { Authorization: env('RECALL_API_KEY'), 'Content-Type': 'application/json' },
    body: body ? JSON.stringify(body) : undefined,
  });
  if (!res.ok) throw new Error(`Recall ${method} ${path}: HTTP ${res.status} ${await res.text()}`);
  return res.status === 204 ? null : res.json();
}

// Sends a bot into the meeting. The bot opens `pageUrl` in its browser: what
// the page plays goes into the meeting, and the page's microphone is the
// meeting audio.
export async function createBot(meetingUrl, pageUrl) {
  const bot = await call('POST', '/bot/', {
    meeting_url: meetingUrl,
    bot_name: process.env.BOT_NAME || 'CGotchi',
    output_media: { camera: { kind: 'webpage', config: { url: pageUrl } } },
    // Output Media runs a real browser with WebRTC; the default bot is too small.
    variant: { google_meet: 'web_4_core' },
  });
  return bot.id;
}

export async function leaveBot(botId) {
  try {
    await call('POST', `/bot/${encodeURIComponent(botId)}/leave_call/`);
  } catch (err) {
    console.warn(err.message); // Already left is fine.
  }
}
