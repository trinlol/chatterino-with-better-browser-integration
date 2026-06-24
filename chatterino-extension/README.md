# 🧩 Chatterino Better Browser Extension

One browser extension for Chrome and Edge — chat overlay, pinned messages, channel points, chat identity badges, and predictions, all in a single install.

## 📥 Installation (Chrome / Edge)

1. Open `chrome://extensions` or `edge://extensions`
2. Enable **Developer mode**
3. Click **Load unpacked**
4. Select this folder (`chatterino-extension` from the repository root)
5. Start **Chatterino Better Browser**, then restart it once after loading the extension

> 🔁 **Upgrading?** Remove any old **Native Host** or **Companion** extensions — you only need this one.

## ✨ What it does

| Feature | How |
|---------|-----|
| 💬 Chat overlay | Replaces Twitch chat with Chatterino on Windows |
| 🪟 Window sync | Reports channel + geometry to the desktop app |
| 📌 Pinned messages | Forwards moderator pins to Chatterino |
| 🎲 Predictions & polls | Syncs live prediction UI to the toolbar |
| 🪙 Channel points | Auto-claims bonus chests; keeps points UI working after chat wipe |
| 🏅 Chat identity | Badge button in the toolbar; opens Chat Identity menu when native chat is hidden |
| 🛡️ Anti-wipe | Restores hidden Twitch DOM after overlay replaces chat |

## ⚙️ Popup settings

Click the extension icon:

- **Replace Twitch chat** — toggle overlay (Windows only, page reload required)
- **Channel points claimer** — auto-claim green chests

## 🔌 Native messaging

Communicates with **Chatterino Better Browser** via `com.chatterino.chatterino`.

If native messaging fails after loading unpacked:

1. Restart **Chatterino Better Browser**
2. Or add your extension ID under **Settings → General → Extra extension IDs**

This extension uses a stable manifest `key`, so its Chrome ID should be `bogfpdfoagkaebimmlcbgmfmanhbhhlm` (already allowed by the app).
