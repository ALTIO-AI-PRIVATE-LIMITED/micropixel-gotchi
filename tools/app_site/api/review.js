// Reviewer only: approve, reject or take down a submission.
import { requireSession } from '../lib/auth.js';
import { cleanText, isAdmin, readMeta, saveMeta } from '../lib/submissions.js';

export default async function handler(req, res) {
  if (req.method !== 'POST') return res.status(405).end();
  const email = await requireSession(req, res);
  if (!email) return;
  if (!isAdmin(email)) return res.status(403).json({ error: 'Reviewers only' });
  const decision = String(req.body?.decision || ''), note = cleanText(req.body?.note, 500);
  if (!['approve', 'reject'].includes(decision)) return res.status(400).json({ error: 'bad decision' });
  if (decision === 'reject' && !note) return res.status(400).json({ error: 'Tell the developer why it was rejected.' });
  const meta = await readMeta(String(req.body?.id || ''));
  if (!meta) return res.status(404).json({ error: 'Submission not found' });
  Object.assign(meta, { status: decision === 'approve' ? 'approved' : 'rejected', note, reviewedAt: new Date().toISOString(), reviewedBy: email });
  await saveMeta(meta);
  console.log(JSON.stringify({ event: 'app_reviewed', id: meta.id, appId: meta.appId, decision, reviewer: email }));
  res.status(200).json({ status: meta.status });
}
