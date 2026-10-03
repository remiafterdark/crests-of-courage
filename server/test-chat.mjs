

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
        next: (op, ms = 1500) =>
          new Promise((res) => {
            let waiter = null;

            const t = setTimeout(() => {
              const i = waiters.indexOf(waiter);
              if (i >= 0) waiters.splice(i, 1);
              res(null);
            }, ms);
            const look = () => {
              const i = queue.findIndex((m) => m.op === op);
              if (i >= 0) {
                clearTimeout(t);
                return res(queue.splice(i, 1)[0]);
              }
              waiter = (m) => {
                queue.push(m);
                look();
              };
              waiters.push(waiter);
            };
            look();
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
a.send({ op: "hello", v: 99, ep: "", port: 1, name: "Link", key: "0123456789abcdef0123456789abcdef" });
b.send({ op: "hello", v: 99, ep: "", port: 2, name: "FuckFace" });
await a.next("count");
await b.next("count");

a.send({ op: "chat", text: "what the fuck is this" });
const toB = await b.next("chat");
const toA = await a.next("chat");
check("everyone gets it, the sender too", toB && toA && toB.text === toA.text);
check("the server names the sender", toB && toB.name === "Link");
check("and tags them from their key", toB && /^[0-9a-f]{16}$/.test(toB.tag) && toB.tag !== "0123456789abcdef");
check("filtered", toB && toB.text === "what the **** is this");

a.send({ op: "chat", text: "again" });
check("two seconds between messages", (await a.next("chat_slow")) !== null);

b.send({ op: "chat", text: "hi" });
const fromB = await a.next("chat");
check("a bad name is replaced", fromB && fromB.name === "Player");

await new Promise((r) => setTimeout(r, 2100));
a.send({ op: "chat", text: "x".repeat(300) });
await b.next("chat");
const long = await b.next("chat");
check("cut to 100 characters", long && long.text.length === 100);

const c = await open();
c.send({ op: "hello", v: 99, ep: "", port: 3, name: "Late" });
check("no history for someone arriving later", (await c.next("history", 800)) === null);

a.send({ op: "name", name: "Zelda" });
const renamed = await c.next("rename");
check("a rename reaches everyone", renamed && renamed.name === "Zelda" && renamed.id === toB.id);
await b.next("rename");
await new Promise((r) => setTimeout(r, 2100));
await b.next("chat", 200);
a.send({ op: "name", name: "Shit" });
const badRename = await b.next("rename");
check("a bad rename is replaced", badRename && badRename.name === "Player");
await new Promise((r) => setTimeout(r, 2100));
a.send({ op: "chat", text: "renamed" });
const afterRename = await c.next("chat");
check("new lines carry the new name", afterRename && afterRename.name === "Player");

for (const p of [a, b, c]) p.close();
setTimeout(() => process.exit(), 200);
