# Running the always-on world

The world lives on a server. Your phone only ever holds a small window of it (~4 KB per frame),
so it uses almost no storage.

## What you need
- A host that can run one small container 24/7 with a persistent disk (the world is ~1.5 MB; 1 GB is plenty).
- A secret key. Anyone with the key can edit your world: `openssl rand -hex 24`.

## Option A: Fly.io (example)
Fly's pricing changes; check it before you commit. A 256 MB shared CPU machine plus a 1 GB volume is small.

    fly launch --no-deploy --copy-config      # accept fly.toml, pick a unique app name
    fly volumes create loam_data --size 1
    fly secrets set LOAM_KEY="$(openssl rand -hex 24)"
    fly deploy

Open `https://<your-app>.fly.dev/`, enter that URL and your key. Fly gives you HTTPS automatically.

## Option B: any Docker host / VPS / Raspberry Pi
    docker build -f cloud/Dockerfile -t loam .
    docker run -d --restart=always --name loam -p 8080:8080 -v loam_data:/data -e LOAM_KEY=<secret> loam

Put HTTPS in front (Caddy or Cloudflare Tunnel). The Android app refuses plain http.

## Safety
- Without `LOAM_KEY` the server refuses to start.
- The world saves every 10 s (atomically) and on shutdown; a daily backup is kept for 7 days in `/data/backups`.
- To roll back: stop the server, copy a backup over `/data/world.bin`, start it.
- "Never die" means the *world* never stops. Individual wisps still starve; that is what keeps it alive.
