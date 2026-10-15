// All settings come from environment variables (Vercel project settings).
export function env(name) {
  const value = process.env[name];
  if (!value) throw new Error(`Missing environment variable ${name}`);
  return value;
}

// Callee name (what the Gotchi asks for) -> that phone's VoIP push token.
// One phone for now: IPHONE_VOIP_TOKEN.
export function pushTokenFor(callee) {
  if (callee === 'iphone') return env('IPHONE_VOIP_TOKEN');
  return null;
}

// Bearer-secret check. DEVICE_SECRET for the Gotchi, APP_SECRET for the iPhone app.
export function authorized(req, secretName) {
  const header = req.headers.authorization || '';
  return header === `Bearer ${env(secretName)}`;
}
