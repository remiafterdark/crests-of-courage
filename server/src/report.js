

const MAX_BODY_BYTES = 1536 * 1024;
const MAX_TEXT = 1500;
const CODE_ALPHABET = "ABCDEFGHJKLMNPQRSTUVWXYZ23456789";
const CODE_RE = /^R-[A-Z2-9]{4}$/;

const recent = new Map();
const WINDOW_MS = 10 * 60 * 1000;
const PER_WINDOW = 12;

function limited(ip) {
  const now = Date.now();
  const list = (recent.get(ip) || []).filter((t) => now - t < WINDOW_MS);
  if (list.length >= PER_WINDOW) {
    recent.set(ip, list);
    return true;
  }
  list.push(now);
  recent.set(ip, list);
  if (recent.size > 5000) recent.clear();
  return false;
}

function newCode() {
  const bytes = new Uint8Array(4);
  crypto.getRandomValues(bytes);
  let out = "R-";
  for (const b of bytes) out += CODE_ALPHABET[b % CODE_ALPHABET.length];
  return out;
}

function clip(value, max) {
  const s = typeof value === "string" ? value : "";
  return s.length > max ? s.slice(0, max) + "…" : s;
}

function safe(s) {
  return s.replace(/@/g, "@​").replace(/\x60/g, "'");
}

function shortHash(text) {
  let h = 2166136261;
  for (let i = 0; i < text.length; i++) {
    h ^= text.charCodeAt(i);
    h = Math.imul(h, 16777619);
  }
  return ((h >>> 0) & 0xffff).toString(16).padStart(4, "0");
}

function privateV4(a, b) {
  return a === 10 || a === 127 || a === 0 || (a === 172 && b >= 16 && b <= 31) ||
    (a === 192 && b === 168) || (a === 169 && b === 254) || (a === 100 && b >= 64 && b <= 127);
}

export function scrub(text) {
  return text
    .replace(/\b(\d{1,3})\.(\d{1,3})\.(\d{1,3})\.(\d{1,3})\b/g, (m, a, b) =>
      privateV4(Number(a), Number(b)) ? m : `ip-${shortHash(m)}`)
    .replace(/\[([0-9A-Fa-f]{0,4}(?::[0-9A-Fa-f]{0,4}){2,7})\]/g, (m, a) =>
      /^(::1|fe80:|fc|fd)/i.test(a) ? m : `[ip6-${shortHash(a.toLowerCase())}]`)
    .replace(/([\\/](?:Users|home)[\\/])[^\\/\r\n"']+/g, "$1~")
    .replace(/(\[ONLINE\] room |joining room |no room )[^\s.]+/g, "$1~")
    .replace(/(\/(?:join|host)\/)[^\s/]+/g, "$1~");
}

function json(obj, status = 200) {
  return new Response(JSON.stringify(obj), {
    status,
    headers: { "Content-Type": "application/json" },
  });
}

export async function handleReport(request, env) {
  if (request.method !== "POST") return json({ error: "POST only" }, 405);
  if (!env.REPORT_WEBHOOK) return json({ error: "reports are not set up" }, 503);
  const length = Number(request.headers.get("Content-Length") || 0);
  if (length > MAX_BODY_BYTES) return json({ error: "too large" }, 413);
  const ip = request.headers.get("CF-Connecting-IP") || "unknown";
  if (limited(ip)) return json({ error: "too many" }, 429);

  let body;
  try {
    const text = await request.text();
    if (text.length > MAX_BODY_BYTES) return json({ error: "too large" }, 413);
    body = JSON.parse(text);
  } catch {
    return json({ error: "bad request" }, 400);
  }

  const kind = body.kind === "log" ? "log" : "report";
  let code;
  if (kind === "log") {
    code = typeof body.code === "string" && CODE_RE.test(body.code) ? body.code : "";
    if (!code) return json({ error: "bad code" }, 400);
  } else {
    code = newCode();
    if (!clip(body.text, MAX_TEXT).trim()) return json({ error: "empty report" }, 400);
  }

  const player = safe(clip(body.player, 40)) || "?";
  const platform = safe(clip(body.platform, 20)) || "?";
  const device = safe(clip(body.device, 120));
  const mod = safe(clip(body.mod, 20));
  const session = scrub(clip(body.session, 4000));
  const log = scrub(clip(body.log, MAX_BODY_BYTES));

  let content;
  if (kind === "report") {
    const what = safe(clip(body.text, MAX_TEXT));
    const when = safe(clip(body.when, 60));
    content =
      `**Bug report ${code}** from **${player}** (${platform}${device ? `, ${device}` : ""}, v${mod})\n` +
      (when ? `When: ${when}\n` : "") +
      what.split("\n").map((l) => `> ${l}`).join("\n");
  } else {
    content = `↳ ${code}: log from **${player}** (${platform}${device ? `, ${device}` : ""}, v${mod})`;
  }
  content = clip(content, 1990);

  const file =
    `${kind === "report" ? "Report" : "Log"} ${code}\n` +
    `player: ${clip(body.player, 40)}\nplatform: ${clip(body.platform, 20)}\n` +
    `device: ${clip(body.device, 120)}\nmod: ${clip(body.mod, 20)}\n` +
    (kind === "report" ? `when: ${clip(body.when, 60)}\n\nwhat happened:\n${clip(body.text, MAX_TEXT)}\n` : "") +
    `\n--- session ---\n${session}\n\n--- co-op log ---\n${log}`;

  const payload = JSON.stringify({ content, allowed_mentions: { parse: [] } });
  const form = new FormData();
  form.append("payload_json", payload);
  const name = `${code}-${player.replace(/[^A-Za-z0-9_-]/g, "_").slice(0, 24) || "player"}.txt`;
  form.append("files[0]", new Blob([file], { type: "text/plain" }), name);

  const sent = await fetch(env.REPORT_WEBHOOK, { method: "POST", body: form });
  if (!sent.ok) return json({ error: "could not deliver" }, 502);

  if (env.REPORT_WEBHOOK_PUBLIC && kind === "report") {
    await fetch(env.REPORT_WEBHOOK_PUBLIC, {
      method: "POST",
      headers: { "Content-Type": "application/json" },
      body: payload,
    });
  }
  return json({ code });
}
