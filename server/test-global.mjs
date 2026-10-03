

const SERVER = process.argv[2] || "ws://127.0.0.1:8787";

function open() {
  return new Promise((resolve, reject) => {
    const ws = new WebSocket(`${SERVER}/global`);
    const queue = [];
    const waiters = [];
    ws.onmessage = (e) => {
      const msg = JSON.parse(e.data);
      if (waiters.length) waiters.shift()(msg);
      else queue.push(msg);
    };
    ws.onerror = reject;
    ws.onopen = () =>
      resolve({
        send: (o) => ws.send(JSON.stringify(o)),
        next: (ms = 2000) =>
          queue.length
            ? Promise.resolve(queue.shift())
            : new Promise((res) => {
                const t = setTimeout(() => res(null), ms);
                waiters.push((m) => (clearTimeout(t), res(m)));
              }),
        close: () => ws.close(),
      });
  });
}

function check(what, ok) {
  console.log(`${ok ? "ok  " : "FAIL"} ${what}`);
  if (!ok) process.exitCode = 1;
}

const a = await open();
const b = await open();
const c = await open();
const wa = await a.next();
const wb = await b.next();
const wc = await c.next();
check("welcome with an id", wa.op === "welcome" && wb.id !== wa.id && wc.id !== wb.id);

for (const [p, port] of [[a, 1001], [b, 1002], [c, 1003]]) {
  p.send({ op: "hello", v: 99, ep: `203.0.113.${port % 250}:${port}`, port, lan: "192.168.1.5" });
  await p.next();
}
a.send({ op: "area", stage: "F_SP103" });
await a.next();
b.send({ op: "area", stage: "F_SP103" });
const toB = await b.next();
const toA = await a.next();
check("same area: introduced to each other", toB.op === "peer" && toA.op === "peer" &&
  toB.id === wa.id && toA.id === wb.id && toA.key === toB.key && /^[0-9a-f]{64}$/.test(toA.key));
check("a home address only goes to the same network", toA.sameNet || !toA.eps.some((e) => e.startsWith("192.168.")));
await b.next();

c.send({ op: "area", stage: "F_SP104" });
const toC = await c.next();
check("other area: nobody introduced", toC.op === "count" && toC.here === 1 && toC.total === 3);

b.send({ op: "area", stage: "F_SP104" });
const goneA = await a.next();
const msgsB = [await b.next(), await b.next(), await b.next()];
check("walking out: the old area hears gone", goneA && goneA.op === "gone" && goneA.id === wb.id);
check("walking in: introduced to the new area",
  msgsB.some((m) => m && m.op === "peer" && m.id === wc.id));

b.send({ op: "block", id: wc.id });
let cGone = await c.next();
while (cGone && cGone.op !== "gone") cGone = await c.next();
const blockMsgs = [await b.next(), cGone];
check("blocking: both hear gone", blockMsgs[0] && blockMsgs[0].op === "gone" && blockMsgs[0].id === wc.id &&
  blockMsgs[1] && blockMsgs[1].op === "gone" && blockMsgs[1].id === wb.id);
b.send({ op: "unblock", id: wc.id });
const unblockMsg = await b.next();
check("unblocking: introduced again", unblockMsg && unblockMsg.op === "peer" && unblockMsg.id === wc.id);
await c.next();

c.close();
const goneC = await b.next();
check("closing: the area hears gone", goneC && goneC.op === "gone" && goneC.id === wc.id);

a.send({ op: "area", stage: "../etc" });
const bad = await a.next();
check("a bad area name is no area", bad && bad.op === "count" && bad.here === 0);

a.close();
b.close();
setTimeout(() => process.exit(), 200);
