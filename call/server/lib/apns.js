// Minimal APNs client for VoIP pushes (PushKit), using Node's built-in http2
// and a token-based (.p8) provider JWT.
import crypto from 'node:crypto';
import http2 from 'node:http2';
import { env } from './config.js';

let cachedJwt = null;
let cachedAt = 0;

function providerToken() {
  // Apple accepts a provider token for up to an hour; refresh every 50 minutes.
  const now = Math.floor(Date.now() / 1000);
  if (cachedJwt && now - cachedAt < 50 * 60) return cachedJwt;
  const b64 = (obj) => Buffer.from(JSON.stringify(obj)).toString('base64url');
  const unsigned = `${b64({ alg: 'ES256', kid: env('APNS_KEY_ID') })}.${b64({ iss: env('APNS_TEAM_ID'), iat: now })}`;
  const key = env('APNS_KEY').replace(/\\n/g, '\n');
  const signature = crypto.sign('sha256', Buffer.from(unsigned), { key, dsaEncoding: 'ieee-p1363' });
  cachedJwt = `${unsigned}.${signature.toString('base64url')}`;
  cachedAt = now;
  return cachedJwt;
}

export function sendVoipPush(deviceToken, payload) {
  const host = process.env.APNS_SANDBOX === '1'
    ? 'https://api.sandbox.push.apple.com'
    : 'https://api.push.apple.com';
  return new Promise((resolve, reject) => {
    const client = http2.connect(host);
    client.on('error', reject);
    const req = client.request({
      ':method': 'POST',
      ':path': `/3/device/${deviceToken}`,
      authorization: `bearer ${providerToken()}`,
      'apns-topic': `${env('APNS_BUNDLE_ID')}.voip`,
      'apns-push-type': 'voip',
      'apns-priority': '10',
      'apns-expiration': '0',
      'content-type': 'application/json',
    });
    let status = 0;
    let body = '';
    req.on('response', (headers) => { status = headers[':status']; });
    req.on('data', (chunk) => { body += chunk; });
    req.on('end', () => {
      client.close();
      if (status === 200) resolve();
      else reject(new Error(`APNs ${status}: ${body}`));
    });
    req.on('error', (err) => { client.close(); reject(err); });
    req.end(JSON.stringify(payload));
  });
}
