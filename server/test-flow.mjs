

import dgram from "node:dgram";
import { randomBytes } from "node:crypto";

const SERVER = process.argv[2] || "ws://127.0.0.1:8787";

function stun(sock, servers) {
  return new Promise((resolve) => {
    if (servers.length === 0) return resolve("");
    const txns = new Map();
    const onMsg = (msg) => {
      if (msg.length < 20 || msg.readUInt16BE(0) !== 0x0101) return;
      if (!txns.has(msg.subarray(8, 20).toString("hex"))) return;
      for (let at = 20; at + 4 <= msg.length; ) {
        const type = msg.readUInt16BE(at);
        const len = msg.readUInt16BE(at + 2);
        if (type === 0x0020 && msg[at + 5] === 1) {
          const port = msg.readUInt16BE(at + 6) ^ 0x2112;
          const ip = [0, 1, 2, 3].map((i) => msg[at + 8 + i] ^ [0x21, 0x12, 0xa4, 0x42][i]);
          sock.off("message", onMsg);
          return resolve(`${ip.join(".")}:${port}`);
        }
        at += 4 + ((len + 3) & ~3);
      }
    };
    sock.on("message", onMsg);
    for (const s of servers) {
      const txn = randomBytes(12);
      txns.set(txn.toString("hex"), s);
      const req = Buffer.concat([Buffer.from([0, 1, 0, 0, 0x21, 0x12, 0xa4, 0x42]), txn]);
      const [ip, port] = s.split(":");
      sock.send(req, Number(port), ip);
    }
    setTimeout(() => resolve(""), 2500);
  });
}

function punch(sock, token, eps, label) {
  return new Promise((resolve, reject) => {
    const tok = Buffer.from(token, "hex").reverse();
    const packet = (kind) => Buffer.concat([Buffer.from("CPH1"), tok, Buffer.from([kind])]);
    const onMsg = (msg, rinfo) => {
      if (msg.length !== 13 || msg.subarray(0, 4).toString() !== "CPH1") return;
      if (!msg.subarray(4, 12).equals(tok)) return;
      if (msg[12] === 1) sock.send(packet(2), rinfo.port, rinfo.address);
      clearInterval(timer);
      sock.off("message", onMsg);
      resolve(`${rinfo.address}:${rinfo.port}`);
    };
    sock.on("message", onMsg);
    const timer = setInterval(() => {
      for (const ep of eps) {
        const [ip, port] = ep.split(":");
        sock.send(packet(1), Number(port), ip);
      }
    }, 100);
    setTimeout(() => {
      clearInterval(timer);
      reject(new Error(`${label}: punch timed out`));
    }, 8000);
  });
}

function open(url) {
  return new Promise((resolve, reject) => {
    const ws = new WebSocket(url);
    const inbox = [];
    const waiters = [];
    ws.onmessage = (e) => {
      const msg = JSON.parse(e.data);
      const w = waiters.shift();
      if (w) w(msg);
      else inbox.push(msg);
    };
    ws.next = () =>
      new Promise((res) => (inbox.length ? res(inbox.shift()) : waiters.push(res)));
    ws.onopen = () => resolve(ws);
    ws.onerror = (e) => reject(e);
  });
}

async function bindUdp() {
  const sock = dgram.createSocket("udp4");
  await new Promise((r) => sock.bind(0, "0.0.0.0", r));
  return sock;
}

const hostUdp = await bindUdp();
const host = await open(`${SERVER}/host`);
const welcome = await host.next();
console.log("host welcome:", welcome);
const hostMapped = await stun(hostUdp, welcome.stun);
console.log("host STUN says:", hostMapped || "(no answer)");
host.send(JSON.stringify({ op: "hello", v: 51, ep: hostMapped, port: hostUdp.address().port, upnp: "" }));

const bad = await open(`${SERVER}/join/${welcome.code}`);
await bad.next();
bad.send(JSON.stringify({ op: "hello", v: 50, ep: "", port: 1234 }));
console.log("old-version joiner told:", await bad.next());

const namedUdp = await bindUdp();
const named = await open(`${SERVER}/host?code=myRoom${Date.now() % 100000}&strict=1`);
const namedWelcome = await named.next();
console.log("named host got room:", namedWelcome.code);
const rival = await open(`${SERVER}/host?code=${namedWelcome.code.toLowerCase()}&strict=1`);
console.log("second host asking for the same name told:", await rival.next());
named.close();
namedUdp.close();

const nobody = await open(`${SERVER}/join/ZZZZZZ`);
console.log("wrong-code joiner told:", await nobody.next());

const joinUdp = await bindUdp();
const joiner = await open(`${SERVER}/join/${welcome.code.toLowerCase()}`);
const jw = await joiner.next();
const joinMapped = await stun(joinUdp, jw.stun);
joiner.send(JSON.stringify({ op: "hello", v: 51, ep: joinMapped, port: joinUdp.address().port }));
const [toHost, toJoiner] = await Promise.all([host.next(), joiner.next()]);
console.log("host introduced to:", toHost);
console.log("joiner introduced to:", toJoiner);
if (toHost.token !== toJoiner.token) throw new Error("tokens differ");

const [a, b] = await Promise.all([
  punch(hostUdp, toHost.token, toHost.eps, "host"),
  punch(joinUdp, toJoiner.token, toJoiner.eps, "joiner"),
]);
console.log(`PUNCHED: host reached joiner at ${a}, joiner reached host at ${b}`);
host.close();
joiner.close();
hostUdp.close();
joinUdp.close();
process.exit(0);
