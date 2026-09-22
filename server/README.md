# Room server

This is what makes room codes work. It only introduces players: the host gets a six-letter code,
joiners type it in, and the server tells each game where to find the other. The games then connect
straight to each other. No gameplay traffic goes through the server, so it fits comfortably in
Cloudflare's free plan.

If two players' networks are too strict to connect directly, the mod tells them to use Tailscale.
There is no relay.

## Put it online (free, about five minutes)

You need [Node.js](https://nodejs.org) and a free [Cloudflare](https://dash.cloudflare.com/sign-up)
account. No credit card is needed.

```bash
cd server
npm install
npx wrangler login
npx wrangler deploy
```

`wrangler deploy` prints the address, something like
`https://crests-rooms.your-name.workers.dev`. Change `https` to `wss` and either:

- put it in `kDefaultRoomServer` at the top of `src/online.cpp` and rebuild, so everyone who
  installs the mod uses it, or
- set the mod's `room_server` config value to it, to point one copy of the game at it.

## Try it without deploying anything

```bash
node dev-server.mjs
node test-flow.mjs
```

`dev-server.mjs` runs the same code on your own machine at `ws://127.0.0.1:8787`. Plain `ws://` is
only allowed for localhost. `test-flow.mjs` plays a host and a joiner against it and checks they
connect. To use it from the game, set `room_server` to `ws://127.0.0.1:8787`. Run
`node dev-server.mjs --code TEST22` to give every host the same code, which is handy for scripted
tests.

## What it costs

On the free plan a join is about half a dozen small messages. A host waiting for players holds a
sleeping WebSocket, which costs next to nothing. If you ever run past the free daily limit, new joins fail
until midnight UTC. Nothing is charged, and players can still join by address.
