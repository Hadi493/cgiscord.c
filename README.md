# cgiscord

A Discord-like chat app written in C.

## Install

```bash
sudo pacman -S gcc sqlite openssl
make
./cgiscord
```

Then open `http://localhost:7000`.

## Features

- Text channels
- Voice chat with auto voice detection (no push to talk)
- Works over the internet

## Voice

It automatically detects when you're talking and transmits. No button to hold.

If it's too sensitive (picks up background noise), increase the threshold in `src/frontend.c`:

```c
const VAD_RMS_THRESHOLD = 0.015;
```

## Host online

Run on a VPS and open port 7000. Friends connect to `http://YOUR_IP:7000`.
