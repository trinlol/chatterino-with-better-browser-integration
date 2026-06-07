![chatterinoLogo](https://user-images.githubusercontent.com/41973452/272541622-52457e89-5f16-4c83-93e7-91866c25b606.png)

# Chatterino 2 - Enhanced Browser Integration Fork 🚀
=========================

Welcome to the **Chatterino 2** fork with **Better Browser Integration**! This version extends the original Chatterino desktop chat client with powerful, quality-of-life enhancements that bridge the gap between Twitch's web player and Chatterino.

---

## ✨ Custom Features in This Fork

### 📌 1. Embedded Pinned Messages
Streamers and moderators' pinned messages are seamlessly integrated into the Chatterino window!
- **Sleek Banner**: Displays pinned messages as a full-width purple header bar stuck to the top of the chat view.
- **Twitch Overlay Cleanup**: Automatically hides the original HTML pinned message box on Twitch to prevent screen clutter.
- **Easy Dismiss**: Features a close (`✕`) button to quickly dismiss the pinned message locally.

### 🪙 2. Channel Points Auto-Claimer
Never miss a chest bonus! 
- **Auto-Collector**: Periodically checks for claimable channel points bonus chests in the background (every 500ms).
- **Instant Claims**: Automatically clicks the green claim chest button immediately when it appears, with zero page reloads.

### ⚡ 3. Z-Order Flicker Fix
Provides a buttery smooth viewing experience.
- **Native Owner Windowing**: Binds the Chatterino integration window directly as an owned child of the browser window.
- **Zero Flickering**: Prevents the Chatterino overlay window from disappearing or flickering when clicking anywhere on the Twitch stream page.

### 💬 4. Smart Input Field Emotes & Click-to-Type
Enhanced chat typing and emote interactions:
- **Click-to-Add**: Clicking on emotes in the chat view automatically inserts them into your input field.
- **Inline Input Emotes**: Emotes render as actual inline images inside your text editor input box.
- **Smart Completion**: Enhanced tab-completion strategies for emotes and usernames.

---

## 🛠️ Installation & Setup

1. **Build the Client**: Compile the C++ Chatterino project (see [Building on Windows](BUILDING_ON_WINDOWS.md)).
2. **Install the Extension**: 
   - Open your browser and navigate to `chrome://extensions/` (or `edge://extensions/`).
   - Enable **Developer mode**.
   - Click **Load unpacked** and select the [`twitch-predictions-mover`](./twitch-predictions-mover/) directory in this repository.
3. Run the compiled `chatterino.exe` and enjoy the enhanced browser integration!

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

## 📜 Original Project Links

- **Wiki**: [https://wiki.chatterino.com](https://wiki.chatterino.com)
- **Original Repo**: [https://github.com/Chatterino/chatterino2](https://github.com/Chatterino/chatterino2)
- **Doxygen Documentation**: [https://doxygen.chatterino.com](https://doxygen.chatterino.com)
