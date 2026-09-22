

import { createServer } from "node:http";
import { createHash } from "node:crypto";

const args = process.argv.slice(2);
const argValue = (name, fallback) => {
  const i = args.indexOf(name);
  return i >= 0 && i + 1 < args.length ? args[i + 1] : fallback;
};
const PORT = Number(argValue("--port", "8787"));
const FIXED_CODE = argValue("--code", "");

class ShimResponse {
  constructor(body, init = {}) {
    this.body = body;
    this.status = init.status ?? 200;
    this.webSocket = init.webSocket ?? null;
  }
}

class ServerSocket {
  constructor() {
    this.attachment = null;
    this.tags = [];
    this.queue = [];
    this.conn = null;
    this.closed = false;
  }
  send(text) {
    if (this.closed) throw new Error("closed");
    if (this.conn) writeFrame(this.conn, 0x1, Buffer.from(text));
    else this.queue.push(text);
  }
  close(code = 1000, reason = "") {
    if (this.closed) return;
    this.closed = true;
    const payload = Buffer.alloc(2 + Buffer.byteLength(reason));
    payload.writeUInt16BE(code, 0);
    payload.write(reason, 2);
    const finish = () => {
      writeFrame(this.conn, 0x8, payload);
      this.conn.end();
    };
    if (this.conn) finish();
    else this.pendingClose = finish;
  }
  serializeAttachment(v) {
    this.attachment = structuredClone(v);
  }
  deserializeAttachment() {
    return structuredClone(this.attachment);
  }
}

globalThis.WebSocketPair = class {
  constructor() {
    const server = new ServerSocket();
    const client = { server };
    this[0] = client;
    this[1] = server;
  }
};
globalThis.Response = ShimResponse;

const { default: worker, Room } = await import("./src/index.js");

class RoomContext {
  constructor() {
    this.sockets = new Set();
  }
  acceptWebSocket(ws, tags) {
    ws.tags = tags;
    this.sockets.add(ws);
  }
  getWebSockets(tag) {
    return [...this.sockets].filter((s) => !s.closed && (!tag || s.tags.includes(tag)));
  }
}

const rooms = new Map();
const env = {
  ROOMS: {
    idFromName: (name) => name,
    get(name) {
      if (!rooms.has(name)) {
        const ctx = new RoomContext();
        const room = new Room(ctx);
        room.__ctx = ctx;
        rooms.set(name, room);
      }
      const room = rooms.get(name);
      return { fetch: (req) => room.fetch(req).then((r) => ((r.__room = room), r)) };
    },
  },
};

function writeFrame(conn, opcode, payload) {
  if (!conn || conn.destroyed) return;
  let header;
  if (payload.length < 126) {
    header = Buffer.from([0x80 | opcode, payload.length]);
  } else if (payload.length < 65536) {
    header = Buffer.alloc(4);
    header[0] = 0x80 | opcode;
    header[1] = 126;
    header.writeUInt16BE(payload.length, 2);
  } else {
    header = Buffer.alloc(10);
    header[0] = 0x80 | opcode;
    header[1] = 127;
    header.writeBigUInt64BE(BigInt(payload.length), 2);
  }
  conn.write(Buffer.concat([header, payload]));
}

function attach(conn, ws, room) {
  ws.conn = conn;
  for (const text of ws.queue) writeFrame(conn, 0x1, Buffer.from(text));
  ws.queue = [];
  if (ws.pendingClose) ws.pendingClose();

  let buffer = Buffer.alloc(0);
  const closed = (code) => {
    if (ws.gone) return;
    ws.gone = true;
    ws.closed = true;
    room.webSocketClose(ws, code, "", true);
  };
  conn.on("data", (chunk) => {
    buffer = Buffer.concat([buffer, chunk]);
    for (;;) {
      if (buffer.length < 2) return;
      const opcode = buffer[0] & 0x0f;
      let len = buffer[1] & 0x7f;
      let at = 2;
      if (len === 126) {
        if (buffer.length < 4) return;
        len = buffer.readUInt16BE(2);
        at = 4;
      } else if (len === 127) {
        if (buffer.length < 10) return;
        len = Number(buffer.readBigUInt64BE(2));
        at = 10;
      }
      const masked = (buffer[1] & 0x80) !== 0;
      const maskAt = at;
      if (masked) at += 4;
      if (buffer.length < at + len) return;
      const payload = Buffer.from(buffer.subarray(at, at + len));
      if (masked) for (let i = 0; i < len; ++i) payload[i] ^= buffer[maskAt + (i % 4)];
      buffer = buffer.subarray(at + len);
      if (opcode === 0x1) {
        room.webSocketMessage(ws, payload.toString("utf8"));
      } else if (opcode === 0x9) {
        writeFrame(conn, 0xa, payload);
      } else if (opcode === 0x8) {
        if (!ws.closed) {
          ws.closed = true;
          writeFrame(conn, 0x8, payload);
        }
        conn.end();
        closed(payload.length >= 2 ? payload.readUInt16BE(0) : 1005);
        return;
      }
    }
  });
  conn.on("close", () => closed(1006));
  conn.on("error", () => closed(1006));
}

const http = createServer((req, res) => {
  res.end("Crests of Courage room server (dev). Connect with a WebSocket.\n");
});

http.on("upgrade", async (req, conn) => {
  const ip = (conn.remoteAddress || "").replace(/^::ffff:/, "");
  let path = req.url;
  if (FIXED_CODE && path === "/host") path = `/host?code=${FIXED_CODE}`;
  console.log(`[dev] ${ip} ${path}`);
  const headers = new Headers();
  for (const [k, v] of Object.entries(req.headers)) if (typeof v === "string") headers.set(k, v);
  headers.set("CF-Connecting-IP", ip);
  const request = new Request(`http://127.0.0.1:${PORT}${path}`, { headers });
  let response;
  try {
    response = await worker.fetch(request, env);
  } catch (e) {
    console.error("[dev] worker threw", e);
    conn.end("HTTP/1.1 500 Internal Server Error\r\n\r\n");
    return;
  }
  if (response.status !== 101 || !response.webSocket) {
    conn.end(`HTTP/1.1 ${response.status} Nope\r\nContent-Length: 0\r\n\r\n`);
    return;
  }
  const accept = createHash("sha1")
    .update(req.headers["sec-websocket-key"] + "258EAFA5-E914-47DA-95CA-C5AB0DC85B11")
    .digest("base64");
  conn.write(
    "HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\nConnection: Upgrade\r\n" +
      `Sec-WebSocket-Accept: ${accept}\r\n\r\n`,
  );
  attach(conn, response.webSocket.server, response.__room);
});

const realSend = ServerSocket.prototype.send;
ServerSocket.prototype.send = function (text) {
  console.log(`[dev] -> ${this.tags.join(",")}: ${text}`);
  return realSend.call(this, text);
};
const realMessage = Room.prototype.webSocketMessage;
Room.prototype.webSocketMessage = function (ws, text) {
  console.log(`[dev] <- ${ws.tags.join(",")}: ${text}`);
  return realMessage.call(this, ws, text);
};

http.listen(PORT, "127.0.0.1", () => {
  console.log(`[dev] room server on ws://127.0.0.1:${PORT}${FIXED_CODE ? ` (code ${FIXED_CODE})` : ""}`);
});
