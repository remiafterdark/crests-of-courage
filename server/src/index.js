

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

function newToken() {
  const bytes = new Uint8Array(8);
  crypto.getRandomValues(bytes);
  return [...bytes].map((b) => b.toString(16).padStart(2, "0")).join("");
}

const IPV4 = /^(25[0-5]|2[0-4]\d|1\d\d|[1-9]?\d)(\.(25[0-5]|2[0-4]\d|1\d\d|[1-9]?\d)){3}$/;
const isIpv4 = (s) => typeof s === "string" && IPV4.test(s);

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

function candidates(info, sameNet = false) {
  const out = [];
  const add = (ep) => {
    if (!out.includes(ep)) out.push(ep);
  };
  if (isEndpoint(info.ep)) add(info.ep);
  if (isIpv4(info.ip) && isPort(info.port)) add(`${info.ip}:${info.port}`);
  if (sameNet && isPort(info.port)) add(`127.0.0.1:${info.port}`);
  return out;
}

export default {
  async fetch(request, env) {
    const url = new URL(request.url);
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
    };
    ws.serializeAttachment(me);
    const host = this.host();
    if (!host) return fail(ws, "no_room");
    const hostInfo = host.deserializeAttachment();
    if (hostInfo.hello) this.pair(host, hostInfo, ws, me);
  }

  pair(hostWs, hostInfo, joinWs, joinInfo) {
    if (hostInfo.hello.v !== joinInfo.hello.v) {
      return fail(joinWs, "version", String(hostInfo.hello.v));
    }
    const token = newToken();

    const outside = (ep) => (isEndpoint(ep) ? ep.slice(0, ep.lastIndexOf(":")) : "");
    const sameNet =
      (hostInfo.ip !== "" && hostInfo.ip === joinInfo.ip) ||
      (outside(hostInfo.hello.ep) !== "" &&
        outside(hostInfo.hello.ep) === outside(joinInfo.hello.ep));
    send(hostWs, {
      op: "peer",
      token,
      eps: candidates({ ...joinInfo.hello, ip: joinInfo.ip }, sameNet),
      sameNet,
    });
    send(joinWs, {
      op: "peer",
      token,
      eps: candidates({ ...hostInfo.hello, ip: hostInfo.ip }, sameNet),
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
