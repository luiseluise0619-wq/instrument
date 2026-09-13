import { emailHash, hasValidAdminSecret, json, keyHash, kv, newSellerKey, normalizeEmail, readRequestBody } from './_lib.mjs';

export default async function handler(request, response) {
  if (request.method !== 'POST') return json(response, 405, { ok: false, message: 'POST required.' });
  if (!hasValidAdminSecret(request)) return json(response, 401, { ok: false, message: 'Unauthorized.' });
  try {
    const { email: rawEmail, maxDevices = 3 } = await readRequestBody(request);
    const email = normalizeEmail(rawEmail);
    const devices = Number(maxDevices);
    if (!email || !Number.isInteger(devices) || devices < 1 || devices > 10)
      return json(response, 400, { ok: false, message: 'A valid e-mail and device limit are required.' });
    const key = newSellerKey();
    const record = { source: 'seller', emailHash: emailHash(email), maxDevices: devices, revoked: false, issuedAt: new Date().toISOString() };
    await kv('SET', `slyce:license:${keyHash(key)}`, JSON.stringify(record));
    return json(response, 201, { ok: true, key, email, maxDevices: devices });
  } catch (error) {
    console.error('License issue failed', error);
    return json(response, 503, { ok: false, message: 'License service is temporarily unavailable.' });
  }
}
