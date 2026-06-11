# Chatterino Native Host

Browser extension for Chrome, Edge, and Firefox that replaces Twitch chat with Chatterino Better Browser and reports the watched channel and window geometry via native messaging.

This is a fork of the [official Chatterino browser extension](https://github.com/Chatterino/chatterino-browser-ext) with performance and reliability fixes for development builds.

## Installation (Chrome / Edge)

1. Open `chrome://extensions` or `edge://extensions`
2. Enable **Developer mode**
3. Click **Load unpacked**
4. Select the [`src`](./src) folder in this directory

## Installation (Firefox)

```shell
cd chatterino-browser-ext
pnpm install
pnpm build
```

Then load the `build/firefox` folder via `about:debugging` → **Load Temporary Add-on**.

## Native messaging

This extension communicates with **Chatterino Better Browser** through the `com.chatterino.chatterino` native messaging host.

If you load an unpacked development build, its extension ID may differ from the Chrome Web Store version. Either:

- Restart Chatterino Better Browser after loading the extension (the app registers allowed extension IDs automatically), or
- Add your extension ID under **Settings → General → Extra extension IDs** in Chatterino

## Features

- Replace Twitch's native chat with Chatterino (Windows only)
- Provide the currently watched channel for the `/watching` split
- Safe window lookup and native messaging connect backoff (prevents CPU/process storms)
