// All settings come from environment variables (Vercel project settings).
export function env(name, fallback) {
  const value = process.env[name] || fallback;
  if (!value) throw new Error(`Missing environment variable ${name}`);
  return value;
}

// The Gotchi sends Authorization: Bearer DEVICE_SECRET.
export function deviceAuthorized(req) {
  return (req.headers.authorization || '') === `Bearer ${env('DEVICE_SECRET')}`;
}

// Public base URL of this deployment, e.g. https://gotchi-meet.vercel.app
export function publicUrl(req) {
  return process.env.PUBLIC_URL || `https://${req.headers.host}`;
}
