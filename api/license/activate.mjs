import {
  emailHash, enforceDeviceLimit, json, keyHash, kv, normalizeEmail,
  normalizeKey, readRequestBody,
} from './_lib.mjs';

const MAX_DEVICES = 3;

async function verifyGumroad(key, email) {
  const productId = process.env.GUMROAD_PRODUCT_ID;
  if (!productId) throw new Error('Gumroad verification is not configured.');
  const body = new URLSearchParams({ product_id: productId, license_key: key, increment_uses_count: 'false' });
  const response = await fetch('https://api.gumroad.com/v2/licenses/verify', {
    method: 'POST', headers: { 'Content-Type': 'application/x-www-form-urlencoded' }, body,
  });
  if (!response.ok) return { ok: false, message: 'Gumroad could not verify this key.' };
  const data = await response.json();
  const purchase = data.purchase ?? {};
  if (!data.success || purchase.refunded || purchase.chargebacked)
    return { ok: false, message: 'This Gumroad key is not active.' };
  if (normalizeEmail(purchase.email) !== email)
    return { ok: false, message: 'That e-mail does not match the Gumroad receipt.' };
  return { ok: true, email: normalizeEmail(purchase.email) };
}

export default async function handler(request, response) {
  if (request.method !== 'POST') return json(response, 405, { ok: false, message: 'POST required.' });
  try {
    const { key: rawKey, email: rawEmail, machine } = await readRequestBody(request);
    const key = normalizeKey(rawKey);
    const email = normalizeEmail(rawEmail);
    if (!key || !email || !/^[a-f0-9]{64}$/i.test(String(machine ?? '')))
      return json(response, 400, { ok: false, message: 'Invalid activation request.' });
    const id = keyHash(key);
    let record;
    if (key.startsWith('SLYCE')) {
      record = await kv('GET', `slyce:license:${id}`);
      record = record ? JSON.parse(record) : null;
      if (!record || record.revoked || record.emailHash !== emailHash(email))
        return json(response, 403, { ok: false, message: 'This seller-issued key is not active for that e-mail.' });
    } else {
      const gumroad = await verifyGumroad(rawKey, email);
      if (!gumroad.ok) return json(response, 403, gumroad);
      record = { source: 'gumroad', emailHash: emailHash(email), maxDevices: MAX_DEVICES, revoked: false };
    }
    const device = await enforceDeviceLimit(id, String(machine).toLowerCase(), record.maxDevices ?? MAX_DEVICES);
    if (!device.ok) return json(response, 403, { ok: false, message: 'This license has reached its device limit.' });
    return json(response, 200, { ok: true, email, message: 'License verified.' });
  } catch (error) {
    console.error('License activation failed', error);
    return json(response, 503, { ok: false, message: 'License service is temporarily unavailable.' });
  }
}
