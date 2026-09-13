import { createHash, randomBytes, timingSafeEqual } from 'node:crypto';

export const json = (res, status, body) => {
  res.status(status).setHeader('Cache-Control', 'no-store');
  res.setHeader('Content-Type', 'application/json; charset=utf-8');
  res.end(JSON.stringify(body));
};

export const sha256 = (value) => createHash('sha256').update(value, 'utf8').digest('hex');
export const normalizeKey = (value) => String(value ?? '').replace(/[^a-z0-9]/gi, '').toUpperCase();
export const normalizeEmail = (value) => String(value ?? '').trim().toLowerCase();
export const emailHash = (value) => sha256(normalizeEmail(value));
export const keyHash = (value) => sha256(normalizeKey(value));

export const newSellerKey = () => {
  const raw = randomBytes(20).toString('base64url').toUpperCase();
  return `SLYCE-${raw.match(/.{1,5}/g).join('-')}`;
};

export function hasValidAdminSecret(request) {
  const supplied = request.headers.authorization?.replace(/^Bearer\s+/i, '') ?? '';
  const expected = process.env.SLYCE_ADMIN_SECRET ?? '';
  if (!supplied || !expected || supplied.length !== expected.length) return false;
  return timingSafeEqual(Buffer.from(supplied), Buffer.from(expected));
}

function kvConfig() {
  const url = process.env.KV_REST_API_URL?.replace(/\/$/, '');
  const token = process.env.KV_REST_API_TOKEN;
  if (!url || !token) throw new Error('License storage is not configured.');
  return { url, token };
}

export async function kv(...command) {
  const { url, token } = kvConfig();
  const path = command.map((part) => encodeURIComponent(String(part))).join('/');
  const response = await fetch(`${url}/${path}`, {
    headers: { Authorization: `Bearer ${token}` },
  });
  if (!response.ok) throw new Error('License storage request failed.');
  const body = await response.json();
  if (body.error) throw new Error('License storage request failed.');
  return body.result;
}

export async function readRequestBody(request) {
  if (typeof request.body === 'object' && request.body !== null) return request.body;
  if (typeof request.body === 'string') {
    const body = request.body.trim();
    if (body.startsWith('{')) return JSON.parse(body);
    return Object.fromEntries(new URLSearchParams(body));
  }
  return {};
}

export async function enforceDeviceLimit(licenseId, machine, maxDevices) {
  const setKey = `slyce:devices:${licenseId}`;
  if (await kv('SISMEMBER', setKey, machine)) return { ok: true, newDevice: false };
  const count = Number(await kv('SCARD', setKey));
  if (count >= maxDevices) return { ok: false };
  await kv('SADD', setKey, machine);
  return { ok: true, newDevice: true };
}
