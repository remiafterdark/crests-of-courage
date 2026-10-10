

import { handleReport } from "./report.js";
import { cleanChat, cleanName } from "./filter.js";

const CODE_ALPHABET = "ABCDEFGHJKLMNPQRSTUVWXYZ23456789";
const CODE_LENGTH = 6;

const NAME_MIN = 4;
const NAME_MAX = 24;
const MAX_JOINERS_WAITING = 8;

const HOST_KEY = /^[0-9a-f]{16,64}$/;
const MAX_MESSAGE_BYTES = 1024;

const STUN_HOSTS = [
  ["stun.l.google.com", 19302],
  ["stun1.l.google.com", 19302],
  ["stun.cloudflare.com", 3478],
];

function newCode() {
  const bytes = new Uint8Array(CODE_LENGTH);
  crypto.getRandomValues(bytes);
  let out = "";
  for (const b of bytes) out += CODE_ALPHABET[b % CODE_ALPHABET.length];
  return out;
}

function normalizeCode(text) {
  if (!text) return "";
  const code = text.toUpperCase().replace(/[^A-Z0-9]/g, "");
  if (code.length < NAME_MIN || code.length > NAME_MAX) return "";
  return code;
}

function newKey() {
  const bytes = new Uint8Array(32);
  crypto.getRandomValues(bytes);
  return [...bytes].map((b) => b.toString(16).padStart(2, "0")).join("");
}

function newToken() {
  const bytes = new Uint8Array(8);
  crypto.getRandomValues(bytes);
  return [...bytes].map((b) => b.toString(16).padStart(2, "0")).join("");
}

const IPV4 = /^(25[0-5]|2[0-4]\d|1\d\d|[1-9]?\d)(\.(25[0-5]|2[0-4]\d|1\d\d|[1-9]?\d)){3}$/;
const isIpv4 = (s) => typeof s === "string" && IPV4.test(s);

const isPrivateIpv4 = (s) =>
  isIpv4(s) && (/^10\./.test(s) || /^192\.168\./.test(s) || /^172\.(1[6-9]|2\d|3[01])\./.test(s));

const isTailscaleIpv4 = (s) => isIpv4(s) && /^100\.(6[4-9]|[7-9]\d|1[01]\d|12[0-7])\./.test(s);

function isEndpoint(s) {
  if (typeof s !== "string") return false;
  const at = s.lastIndexOf(":");
  if (at < 0) return false;
  const port = Number(s.slice(at + 1));
  return isIpv4(s.slice(0, at)) && Number.isInteger(port) && port > 0 && port < 65536;
}

const isPort = (n) => Number.isInteger(n) && n > 0 && n < 65536;

let stunCache = { at: 0, list: [] };

async function stunServers() {
  if (stunCache.list.length > 0 && Date.now() - stunCache.at < 3600_000) return stunCache.list;
  const list = [];
  await Promise.all(
    STUN_HOSTS.map(async ([name, port]) => {
      try {
        const r = await fetch(
          `https://cloudflare-dns.com/dns-query?name=${name}&type=A`,
          { headers: { accept: "application/dns-json" } },
        );
        const body = await r.json();
        const a = (body.Answer || []).find((x) => x.type === 1 && isIpv4(x.data));
        const ep = a ? `${a.data}:${port}` : "";
        if (ep && !list.includes(ep)) list.push(ep);
      } catch {

      }
    }),
  );
  if (list.length > 0) stunCache = { at: Date.now(), list };
  return list;
}

function send(ws, obj) {
  try {
    ws.send(JSON.stringify(obj));
  } catch {

  }
}

function fail(ws, why, detail) {
  send(ws, { op: "error", why, detail });
  try {
    ws.close(4000, why);
  } catch {

  }
}

function candidates(info, sameNet = false, otherLan = "") {
  const out = [];
  const add = (ep) => {
    if (!out.includes(ep)) out.push(ep);
  };
  const lan = sameNet && isPrivateIpv4(info.lan) && isPort(info.port);
  if (lan) add(`${info.lan}:${info.port}`);
  if (isEndpoint(info.ep)) add(info.ep);
  if (isIpv4(info.ip) && isPort(info.port)) add(`${info.ip}:${info.port}`);
  if (sameNet && isPort(info.port) && (!lan || info.lan === otherLan)) add(`127.0.0.1:${info.port}`);

  if (isTailscaleIpv4(info.ts) && isPort(info.port)) add(`${info.ts}:${info.port}`);
  return out;
}

export default {
  async fetch(request, env) {
    const url = new URL(request.url);
    if (url.pathname === "/report") return handleReport(request, env);
    if (request.headers.get("Upgrade") !== "websocket") {
      return new Response("Crests of Courage room server. Nothing to see here.\n");
    }

    if (url.pathname === "/host") {

      const named = url.searchParams.get("strict") === "1";
      const hostKey = HOST_KEY.test(url.searchParams.get("key") || "") ? url.searchParams.get("key") : "";
      const reclaim = normalizeCode(url.searchParams.get("code"));
      if (named) {
        if (!reclaim) return new Response("That is not a room name", { status: 400 });
        const room = env.ROOMS.get(env.ROOMS.idFromName(reclaim));
        return room.fetch(
          new Request(`https://room/host?code=${reclaim}&strict=1&key=${hostKey}`, request),
        );
      }

      for (let attempt = 0; attempt < 8; ++attempt) {
        const code = attempt === 0 && reclaim ? reclaim : newCode();
        const room = env.ROOMS.get(env.ROOMS.idFromName(code));
        const response = await room.fetch(
          new Request(`https://room/host?code=${code}&key=${hostKey}`, request),
        );
        if (response.status !== 409) return response;
      }
      return new Response("Could not find a free room code", { status: 503 });
    }

    if (url.pathname === "/global") {
      const lobby = env.GLOBAL.get(env.GLOBAL.idFromName("hyrule-online"));
      return lobby.fetch(new Request("https://global/", request));
    }

    const join = url.pathname.match(/^\/join\/([A-Za-z0-9-]+)$/);
    if (join) {
      const code = normalizeCode(join[1]);
      if (!code) return new Response("That is not a room code", { status: 400 });
      const room = env.ROOMS.get(env.ROOMS.idFromName(code));
      return room.fetch(new Request(`https://room/join?code=${code}`, request));
    }

    return new Response("Not found", { status: 404 });
  },
};

export class Room {
  constructor(ctx) {
    this.ctx = ctx;
  }

  host(except = null) {
    for (const ws of this.ctx.getWebSockets("host")) {
      if (ws === except) continue;
      const info = ws.deserializeAttachment() || {};
      if (info.role === "host") return ws;
    }
    return null;
  }

  getOtherHost(except) {
    return this.host(except);
  }

  takeOver(key) {
    if (!HOST_KEY.test(key || "")) return false;
    const old = this.host();
    if (!old) return false;
    const info = old.deserializeAttachment() || {};
    if (info.key !== key) return false;
    try {

      old.serializeAttachment({ ...info, role: "replaced" });
      old.close(4001, "replaced by the same player");
    } catch {

    }
    return true;
  }

  async fetch(request) {
    const url = new URL(request.url);
    const role = url.pathname === "/host" ? "host" : "join";
    const code = url.searchParams.get("code");
    const ip = request.headers.get("CF-Connecting-IP") || "";

    const strict = url.searchParams.get("strict") === "1";
    const key = url.searchParams.get("key") || "";
    if (role === "host") this.takeOver(key);
    if (role === "host" && this.host() && !strict) {
      return new Response("Room taken", { status: 409 });
    }

    const pair = new WebSocketPair();
    const [client, server] = Object.values(pair);

    this.ctx.acceptWebSocket(server, [role]);

    server.serializeAttachment({ role, ip, hello: null, key: HOST_KEY.test(key) ? key : "" });

    if (role === "host" && strict && this.getOtherHost(server)) {

      server.serializeAttachment({ role: "rejected", ip, hello: null });
      fail(server, "taken");
    } else if (role === "join") {
      if (!this.host()) {
        fail(server, "no_room");
      } else if (this.ctx.getWebSockets("join").length > MAX_JOINERS_WAITING) {
        fail(server, "busy");
      } else {
        send(server, { op: "welcome", ip, stun: await stunServers() });
      }
    } else {
      send(server, { op: "welcome", code, ip, stun: await stunServers() });
    }
    return new Response(null, { status: 101, webSocket: client });
  }

  async webSocketMessage(ws, message) {
    if (typeof message !== "string" || message.length > MAX_MESSAGE_BYTES) {
      return fail(ws, "bad_message");
    }
    let msg;
    try {
      msg = JSON.parse(message);
    } catch {
      return fail(ws, "bad_message");
    }
    const me = ws.deserializeAttachment();

    if (me.role === "host") {
      if (msg.op === "hello") {
        me.hello = {
          v: Number(msg.v) || 0,
          ep: isEndpoint(msg.ep) ? msg.ep : "",
          port: isPort(msg.port) ? msg.port : 0,
          upnp: isEndpoint(msg.upnp) ? msg.upnp : "",
          lan: isPrivateIpv4(msg.lan) ? msg.lan : "",
          ts: isTailscaleIpv4(msg.ts) ? msg.ts : "",
        };
        ws.serializeAttachment(me);

        for (const j of this.ctx.getWebSockets("join")) {
          const info = j.deserializeAttachment();
          if (info.hello && !info.paired) this.pair(ws, me, j, info);
        }
      } else if (msg.op === "update" && me.hello) {
        me.hello.upnp = isEndpoint(msg.upnp) ? msg.upnp : "";
        ws.serializeAttachment(me);
      }
      return;
    }

    if (msg.op !== "hello" || me.hello) return fail(ws, "bad_message");
    me.hello = {
      v: Number(msg.v) || 0,
      ep: isEndpoint(msg.ep) ? msg.ep : "",
      port: isPort(msg.port) ? msg.port : 0,
      lan: isPrivateIpv4(msg.lan) ? msg.lan : "",
      ts: isTailscaleIpv4(msg.ts) ? msg.ts : "",
    };
    ws.serializeAttachment(me);
    const host = this.host();
    if (!host) return fail(ws, "no_room");
    const hostInfo = host.deserializeAttachment();
    if (hostInfo.hello) this.pair(host, hostInfo, ws, me);
  }

  pair(hostWs, hostInfo, joinWs, joinInfo) {
    if (hostInfo.hello.v !== joinInfo.hello.v) {

      send(hostWs, { op: "joiner_version", v: String(joinInfo.hello.v) });
      return fail(joinWs, "version", String(hostInfo.hello.v));
    }
    const token = newToken();

    const key = newKey();

    const outside = (ep) => (isEndpoint(ep) ? ep.slice(0, ep.lastIndexOf(":")) : "");
    const sameNet =
      (hostInfo.ip !== "" && hostInfo.ip === joinInfo.ip) ||
      (outside(hostInfo.hello.ep) !== "" &&
        outside(hostInfo.hello.ep) === outside(joinInfo.hello.ep));
    send(hostWs, {
      op: "peer",
      token,
      key,
      eps: candidates({ ...joinInfo.hello, ip: joinInfo.ip }, sameNet, hostInfo.hello.lan),
      sameNet,
    });
    send(joinWs, {
      op: "peer",
      token,
      key,
      eps: candidates({ ...hostInfo.hello, ip: hostInfo.ip }, sameNet, joinInfo.hello.lan),
      upnp: hostInfo.hello.upnp,
      sameNet,
    });
    joinInfo.paired = true;
    joinWs.serializeAttachment(joinInfo);
  }

  async webSocketClose(ws, code) {
    try {
      ws.close(code === 1005 ? 1000 : code);
    } catch {

    }
    const me = ws.deserializeAttachment();

    if (me && me.role === "host") {
      for (const j of this.ctx.getWebSockets("join")) fail(j, "no_room");
    }
  }

  async webSocketError(ws) {
    await this.webSocketClose(ws, 1011);
  }
}

const GLOBAL_PEERS = 15;
const GLOBAL_MAX = 400;
const GLOBAL_AREA = /^[A-Za-z0-9_]{0,8}$/;
const GLOBAL_AREA_CHANGES = 30;

const GLOBAL_CHAT_MAX = 100;
const GLOBAL_CHAT_GAP_MS = 2000;
const GLOBAL_RENAMES = 6;

const GLOBAL_BLOCKS = 30;

function addressTag(ip) {
  let h = 2166136261;
  for (let i = 0; i < ip.length; i++) {
    h ^= ip.charCodeAt(i);
    h = Math.imul(h, 16777619);
  }
  return (h >>> 0).toString(16).padStart(8, "0");
}

async function tagOf(key) {
  if (typeof key !== "string" || !/^[0-9a-f]{32}$/.test(key)) return "";
  const digest = await crypto.subtle.digest("SHA-256", new TextEncoder().encode(key));
  return [...new Uint8Array(digest)].slice(0, 8).map((b) => b.toString(16).padStart(2, "0")).join("");
}

function printable(text, max) {
  return String(text || "").replace(/[\u0000-\u001f\u007f]/g, " ").trim().slice(0, max);
}

export class Global {
  constructor(ctx, env) {
    this.ctx = ctx;
    this.env = env || {};
  }

  banned(info) {
    const list = String(this.env.GLOBAL_BANS || "").split(",").map((x) => x.trim()).filter(Boolean);
    return list.includes(addressTag(info.ip || ""));
  }

  async chat(ws, info, text) {
    const now = Date.now();
    if (this.banned(info)) return send(ws, { op: "chat_banned" });
    if (now - (info.lastChat || 0) < GLOBAL_CHAT_GAP_MS) return send(ws, { op: "chat_slow" });
    const line = cleanChat(printable(text, GLOBAL_CHAT_MAX));
    if (!line) return;
    info.lastChat = now;
    ws.serializeAttachment(info);
    console.log(`chat ${addressTag(info.ip || "")} ${info.name}: ${line}`);
    const msg = { op: "chat", id: info.id, tag: info.tag || "", name: info.name, text: line };
    this.everyone(msg);
  }

  rename(ws, info, name) {
    const now = Date.now();
    if (now - (info.renameWindow || 0) > 60000) {
      info.renameWindow = now;
      info.renames = 0;
    }
    const clean = cleanName(printable(name, 16)) || "Player";
    if (clean === info.name || ++info.renames > GLOBAL_RENAMES) {
      ws.serializeAttachment(info);
      return;
    }
    info.name = clean;
    ws.serializeAttachment(info);
    this.everyone({ op: "rename", id: info.id, name: clean });
  }

  unpair(ws, info, otherId) {
    if (!(info.peers || []).includes(otherId)) return null;
    info.peers = info.peers.filter((id) => id !== otherId);
    const other = this.byId(otherId);
    if (!other) return null;
    const theirs = other.deserializeAttachment();
    theirs.peers = (theirs.peers || []).filter((id) => id !== info.id);
    other.serializeAttachment(theirs);
    send(other, { op: "gone", id: info.id });
    send(ws, { op: "gone", id: otherId });
    return other;
  }

  block(ws, info, id, on) {
    if (!Number.isInteger(id) || id <= 0) return;
    const now = Date.now();
    if (now - (info.blockWindow || 0) > 60000) {
      info.blockWindow = now;
      info.blockCount = 0;
    }
    if (++info.blockCount > GLOBAL_BLOCKS) {
      ws.serializeAttachment(info);
      return;
    }
    const blocks = (info.blocks || []).filter((b) => b !== id);
    if (on) blocks.push(id);
    info.blocks = blocks.slice(-200);
    const other = on ? this.unpair(ws, info, id) : null;
    this.introduce(ws, info);
    ws.serializeAttachment(info);
    if (other) {
      const theirs = other.deserializeAttachment();
      this.introduce(other, theirs, ws);
      other.serializeAttachment(theirs);
    }
  }

  everyone(msg) {
    for (const other of this.all()) {
      const theirs = other.deserializeAttachment();
      if (theirs && theirs.hello) send(other, msg);
    }
  }

  all() {
    return this.ctx.getWebSockets("global");
  }

  byId(id) {
    for (const ws of this.all()) {
      const info = ws.deserializeAttachment();
      if (info && info.id === id) return ws;
    }
    return null;
  }

  newId() {
    const used = new Set(this.all().map((ws) => (ws.deserializeAttachment() || {}).id));
    for (;;) {
      const id = crypto.getRandomValues(new Uint32Array(1))[0] & 0x7fffffff;
      if (id !== 0 && !used.has(id)) return id;
    }
  }

  counts(area, v = 0) {
    let total = 0;
    let here = 0;
    let same = 0;
    for (const ws of this.all()) {
      const info = ws.deserializeAttachment();
      if (!info || !info.hello) continue;
      ++total;
      if (area && info.area === area) {
        ++here;
        if (info.v === v) ++same;
      }
    }
    return { total, here, same };
  }

  part(ws, info) {
    const freed = [];
    for (const otherId of info.peers || []) {
      const other = this.byId(otherId);
      if (!other) continue;
      const theirs = other.deserializeAttachment();
      theirs.peers = (theirs.peers || []).filter((id) => id !== info.id);
      other.serializeAttachment(theirs);
      send(other, { op: "gone", id: info.id });
      send(ws, { op: "gone", id: otherId });
      freed.push(other);
    }
    info.peers = [];
    ws.serializeAttachment(info);
    for (const other of freed) {
      const theirs = other.deserializeAttachment();
      if (!theirs) continue;
      this.introduce(other, theirs, ws);
      other.serializeAttachment(theirs);
    }
  }

  introduce(ws, info, except = null) {
    if (!info.area || !info.hello) return;
    for (const other of this.all()) {
      if ((info.peers || []).length >= GLOBAL_PEERS) break;
      if (other === ws || other === except) continue;
      const theirs = other.deserializeAttachment();
      if (!theirs || !theirs.hello || theirs.area !== info.area || theirs.v !== info.v) continue;
      if ((theirs.peers || []).length >= GLOBAL_PEERS || (theirs.peers || []).includes(info.id)) continue;
      if ((info.blocks || []).includes(theirs.id) || (theirs.blocks || []).includes(info.id)) continue;
      const token = newToken();
      const key = newKey();
      const outside = (ep) => (isEndpoint(ep) ? ep.slice(0, ep.lastIndexOf(":")) : "");
      const sameNet = (info.ip !== "" && info.ip === theirs.ip) ||
        (outside(info.ep) !== "" && outside(info.ep) === outside(theirs.ep));
      send(ws, { op: "peer", id: theirs.id, tag: theirs.tag || "", token, key, sameNet,
        eps: candidates(theirs, sameNet, info.lan) });
      send(other, { op: "peer", id: info.id, tag: info.tag || "", token, key, sameNet,
        eps: candidates(info, sameNet, theirs.lan) });
      theirs.peers = [...(theirs.peers || []), info.id];
      other.serializeAttachment(theirs);
      info.peers = [...(info.peers || []), theirs.id];
    }
  }

  async fetch(request) {
    if (this.all().length >= GLOBAL_MAX) {
      return new Response("Hyrule Online is full", { status: 503 });
    }
    const pair = new WebSocketPair();
    const [client, server] = Object.values(pair);
    this.ctx.acceptWebSocket(server, ["global"]);
    const id = this.newId();
    server.serializeAttachment({
      id, ip: request.headers.get("CF-Connecting-IP") || "", hello: false, v: 0, ep: "", port: 0,
      lan: "", area: "", peers: [], window: Date.now(), changes: 0,
    });
    send(server, { op: "welcome", id, stun: await stunServers(), total: this.counts("").total });
    return new Response(null, { status: 101, webSocket: client });
  }

  async webSocketMessage(ws, message) {
    if (typeof message !== "string" || message.length > 512) return fail(ws, "bad_message");
    let msg;
    try {
      msg = JSON.parse(message);
    } catch {
      return fail(ws, "bad_message");
    }
    const info = ws.deserializeAttachment();
    if (!info) return;
    if (msg.op === "hello" && !info.hello) {
      info.hello = true;
      info.v = Number(msg.v) || 0;
      info.ep = isEndpoint(msg.ep) ? msg.ep : "";
      info.port = isPort(msg.port) ? msg.port : 0;
      info.lan = isPrivateIpv4(msg.lan) ? msg.lan : "";
      info.name = cleanName(printable(msg.name, 16)) || "Player";
      info.tag = await tagOf(msg.key);
      this.introduce(ws, info);
      ws.serializeAttachment(info);
      send(ws, { op: "count", ...this.counts(info.area, info.v) });
      return;
    }
    if (msg.op === "area") {
      const now = Date.now();
      if (now - info.window > 60000) {
        info.window = now;
        info.changes = 0;
      }
      if (++info.changes > GLOBAL_AREA_CHANGES) return fail(ws, "too_fast");
      const area = typeof msg.stage === "string" && GLOBAL_AREA.test(msg.stage) ? msg.stage : "";
      if (area !== info.area) {
        this.part(ws, info);
        info.area = area;
        this.introduce(ws, info);
      }
      ws.serializeAttachment(info);
      send(ws, { op: "count", ...this.counts(info.area, info.v) });
      return;
    }
    if (msg.op === "chat" && info.hello) {
      await this.chat(ws, info, msg.text);
      return;
    }
    if (msg.op === "name" && info.hello) {
      this.rename(ws, info, msg.name);
      return;
    }
    if ((msg.op === "block" || msg.op === "unblock") && info.hello) {
      this.block(ws, info, Number(msg.id), msg.op === "block");
      return;
    }
    return fail(ws, "bad_message");
  }

  async webSocketClose(ws, code) {
    try {
      ws.close(code === 1005 ? 1000 : code);
    } catch {

    }
    const info = ws.deserializeAttachment();
    if (info) this.part(ws, info);
  }

  async webSocketError(ws) {
    await this.webSocketClose(ws, 1011);
  }
}
