![chatterinoLogo](https://user-images.githubusercontent.com/41973452/272541622-52457e89-5f16-4c83-93e7-91866c25b606.png)

# Chatterino 2 - Browser Integration Fork 🚀


A fork of Chatterino 2 focused on making the browser integration actually feel native and complete. It adds quality-of-life features that integrate Twitch web features directly into your Chatterino splits.

---

## ✨ Features

### 📌 1. Pinned Messages in Chat
Moderator and streamer pinned messages now show up directly in Chatterino:
- Shows pinned messages as a purple banner at the top of the chat split.
- Automatically hides Twitch's default web pinned message to save screen space.
- Includes a close (`✕`) button to dismiss the pin.

### 🪙 2. Channel Points Auto-Claimer
Never miss claiming channel points:
- Automatically clicks the green "Claim Bonus" chest as soon as it appears.
- Zero page reloads or manual clicking needed.

### ⚡ 3. Fixed Window Flickering & Disappearing
Fixes the annoying window bugs when clicking the browser:
- The Chatterino overlay stays pinned in front of the browser without disappearing when you click on Twitch.
- No flickering or rendering lag when clicking.
- Properly hides behind other applications (like Discord or games) when you tab out.

### 💬 4. Emote Click-to-Type & Rich Input
- Clicking an emote in chat automatically copies it to your text input.
- Emotes are rendered as inline images inside the text input field.

---

## 🛠️ Installation & Setup

1. **Build the Client**: Compile the C++ Chatterino project (see [Building on Windows](BUILDING_ON_WINDOWS.md)).
2. **Install the Extension**: 
   - Open your browser and navigate to `chrome://extensions/` (or `edge://extensions/`).
   - Enable **Developer mode**.
   - Click **Load unpacked** and select the [`twitch-predictions-mover`](./twitch-predictions-mover/) directory in this repository.
3. Run the compiled `chatterino.exe`!

---

## 📖 Building & Contribution Guidelines

The source code includes required submodules. Run the following to clone:

```shell
git clone --recurse-submodules https://github.com/trinlol/chatterino-with-better-browser-integration.git
```

For platform-specific compilation instructions:
- [Building on Windows](BUILDING_ON_WINDOWS.md)
- [Building on Windows with vcpkg](BUILDING_ON_WINDOWS_WITH_VCPKG.md)
- [Building on Linux](BUILDING_ON_LINUX.md)
- [Building on macOS](BUILDING_ON_MAC.md)
- [Building on FreeBSD](BUILDING_ON_FREEBSD.md)

---

## 📖 Original Project Links

- **Wiki**: [https://wiki.chatterino.com](https://wiki.chatterino.com)
- **Original Repo**: [https://github.com/Chatterino/chatterino2](https://github.com/Chatterino/chatterino2)
- **Doxygen Documentation**: [https://doxygen.chatterino.com](https://doxygen.chatterino.com)
