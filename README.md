<p align="left">
  <img src="./chatterino-extension/icon.png" width="112" alt="Chatterino Better Browser logo">
</p>

# ✨ Chatterino Better Browser

<p>
  <a href="https://github.com/trinlol/chatterino-with-better-browser-integration/releases/latest"><img src="https://img.shields.io/github/v/release/trinlol/chatterino-with-better-browser-integration?style=flat-square&color=9146ff&label=release" alt="Latest release"></a>
  <a href="https://github.com/trinlol/chatterino-with-better-browser-integration/releases"><img src="https://img.shields.io/github/downloads/trinlol/chatterino-with-better-browser-integration/total?style=flat-square&color=2ea44f" alt="Total downloads"></a>
  <a href="https://github.com/trinlol/chatterino-with-better-browser-integration/stargazers"><img src="https://img.shields.io/github/stars/trinlol/chatterino-with-better-browser-integration?style=flat-square&color=f0b429" alt="GitHub stars"></a>
  <img src="https://img.shields.io/badge/platform-Windows%20x64-0078d4?style=flat-square" alt="Windows x64">
  <img src="https://img.shields.io/badge/extension-Firefox%20%7C%20Chrome%20%7C%20Edge-9146ff?style=flat-square" alt="Firefox, Chrome, and Edge extension">
  <a href="./LICENSE"><img src="https://img.shields.io/github/license/trinlol/chatterino-with-better-browser-integration?style=flat-square" alt="License"></a>
</p>

Current release: **2.7.6**

A fork of [Chatterino 2](https://github.com/Chatterino/chatterino2) that makes Twitch browser integration feel native and complete - Chatterino chat overlaid on Twitch, plus pinned messages, predictions, channel points, chat identity badges, inline 7TV emotes, and built-in themes.

> 🧩 **One extension only** - works in **Mozilla Firefox**, **Chrome**, and **Edge** using [`chatterino-extension/`](./chatterino-extension/). No separate Native Host or Companion install.

> [!IMPORTANT]
> Chatterino Better Browser stores settings locally in `%APPDATA%\Chatterino2\` and communicates only with Twitch and the services used for chat features. Its updater and first-run release notes point exclusively to this fork's [GitHub Releases](https://github.com/trinlol/chatterino-with-better-browser-integration/releases). Remove the unpacked browser extension and delete the extracted app folder to stop using the fork; your Chatterino settings remain untouched.

---

## 🆚 Why switch from stock Chatterino?

When stock Chatterino replaces Twitch's chat column, Twitch features living inside that column disappear. **Chatterino Better Browser** brings them back natively without leaving Chatterino:

| Feature | Stock Chatterino | Chatterino Better Browser |
| --- | :---: | :---: |
| **Twitch chat overlay** | Basic overlay | Flicker-free DWM alignment, clean tab-switch detach, popup z-order fixes |
| **Moderator pinned messages** | Missing | Compact purple pin banner with clickable links & dismiss |
| **Polls & predictions** | Missing | Live banner with real lock deadline + clickable voting controls beside player |
| **Channel points & bonus chests** | Missing | Auto-claim bonus chests + scrollable rewards menu beside player |
| **Chat identity / badge picker** | Missing | Native toolbar badge button even while Twitch chat is hidden |
| **7TV emote input & favourites** | Text-only | Inline input emotes, `:` visual picker, Ctrl+click favourites (instant save) |
| **Built-in themes** | Basic light/dark | 12 presets (Catppuccin, Dracula, Nord, Tokyo Night, etc.) + Themes Studio |
| **Browser extension setup** | Separate Native Host | Single unified MV3 extension for Mozilla Firefox, Chrome & Edge |

---

## 📸 Showcase

Chatterino Better Browser on a live Twitch channel - full chat in the overlay, synced pins, toolbar integration beside the player, and live extension health diagnostics.

<p align="center">
  <img src="./docs/amo-screenshots/01-chat-overlay-and-emotes.png" width="100%" alt="Native Chatterino chat docked inside Twitch with emote picker">
</p>

<table>
  <tr>
    <td width="50%" align="center" valign="top">
      <img src="./docs/amo-screenshots/02-channel-points-and-toolbar.png" width="100%" alt="Keep Twitch Power-Ups, Channel Points rewards, and badges working">
      <br>
      <sub><b>Channel points &amp; toolbar</b> - Power-Ups, rewards, watch streak, and Chat Identity stay accessible beside the player.</sub>
    </td>
    <td width="50%" align="center" valign="top">
      <img src="./docs/amo-screenshots/03-extension-popup.png" width="100%" alt="Extension popup with chat overlay toggle, auto-claimer, and live integration health">
      <br>
      <sub><b>Extension popup</b> - toggles and live health monitor for Mozilla Firefox, Chrome, and Edge.</sub>
    </td>
  </tr>
</table>

<details>
<summary><b>Animated GIF demos (chat overlay, emote favourites, channel points)</b></summary>
<br>

<table>
  <tr>
    <td rowspan="2" valign="top" align="center" width="42%">
      <img src="./docs/showcase/chatterino-overlay.gif" width="300" alt="Full Chatterino chat overlay on Twitch with pinned banner, replies, and emotes">
      <br>
      <sub><b>Full chat preview</b> - pinned messages, inline input emotes, and ctrl+click to input emotes directly from user messages.</sub>
    </td>
    <td valign="top" align="center" width="58%">
      <img src="./docs/showcase/emote-picker.gif" width="300" alt="Emote favourites picker with channel, 7TV, and search tabs">
      <br>
      <sub><b>Emote favourites</b> - save per-channel, global, and 7TV emotes with search and Tab completion.</sub>
    </td>
  </tr>
  <tr>
    <td valign="top" align="center">
      <table width="100%">
        <tr>
          <td width="50%" valign="top" align="center">
            <img src="./docs/showcase/twitch-toolbar.gif" width="300" alt="Channel points menu beside the Twitch player toolbar">
            <br>
            <sub><b>Channel points</b> - balance and rewards menu beside the player.</sub>
          </td>
        </tr>
      </table>
    </td>
  </tr>
</table>

</details>

<p align="center">
  <sub>Showcase captured on <a href="https://www.twitch.tv/alveussanctuary">alveussanctuary</a> for demonstration only.</sub>
  <br>
  <sub>Chat and toolbar captures originally recorded on v1.0.1 - newer releases include additional UI refinements and themes.</sub>
</p>

---

## 🚀 Quick start (prebuilt)

1. 📦 Download the latest **Chatterino Better Browser Windows** archive from [Releases](https://github.com/trinlol/chatterino-with-better-browser-integration/releases/latest)
2. 🗑️ **Uninstall** the old Chatterino app from Windows if you have it _(your settings are safe - see below)_
3. 📂 Extract the zip **anywhere you like** (e.g. `C:\Apps\Chatterino Better Browser\`)
4. 🔗 Right-click **`Chatterino Better Browser.exe`** → **Send to** → **Desktop (create shortcut)** _(or pin to Start / taskbar)_
5. ▶️ Launch from your new shortcut
6. 🧩 Install the **one** browser extension below _(required)_
7. 🔄 Restart the app after loading the extension so native messaging registers

> 💡 **Already use Chatterino?** Your existing settings, accounts, and window layout load automatically - uninstalling the old app does **not** delete them. See [Settings & migration](#-settings--migration) below.

---

## 🎯 Features

### 🌐 Browser integration

<p align="center">
  <img src="docs/hero.svg" alt="How Chatterino Better Browser works - Twitch in your browser communicates with Chatterino desktop via the extension" width="100%">
</p>

- 📌 **Pinned messages** - moderator pins show as a compact purple banner in Chatterino with clickable links and a dismiss button
- 🗳️ **Clickable polls & predictions** - voting controls appear beside the Twitch player so you can vote without reopening native Twitch chat
- 🟢 **Accurate prediction banner** - the green Chatterino banner follows Twitch's real lock deadline instead of getting stuck at `2:00`
- 🧭 **Independent activity state** - polls and predictions update separately, with clearer titles and no cross-event overwrites
- 🪙 **Channel points auto-claim** - claims bonus chests automatically; scrollable rewards menu
- 🏅 **Chat identity button** - badge picker in the toolbar (left of channel points); opens Twitch's Chat Identity menu even when the native chat is hidden
- 🖥️ **Fullscreen-safe controls** - moved toolbar elements automatically hide while the Twitch video is fullscreen
- ⚡ **Stable browser overlay** - fixes flicker, tab-switch leaks, disappearing chat, and runaway CPU from native messaging
- 🔄 **Fork-only updates** - update prompts and first-run release notes use this repository, never the upstream Chatterino download site

### 💬 Better emote input

- 7️⃣ **7TV-only autocomplete** - both Tab completion and the visual `:` menu exclude Twitch, BTTV, FFZ, and emoji entries
- ⌨️ **Reliable Tab cycling** - press **Tab** or **Shift+Tab** to cycle forward or backward through matching 7TV emotes without losing your typed text
- 🔎 **Visual emote menu** - type `:` at the start of a word to browse 7TV emotes with previews; choosing one inserts editable text
- 🖼️ **Inline emotes in the input** - typed, completed, and clicked 7TV emotes render as images; Tab and Shift+Tab still cycle safely
- ⭐ **Emote favourites tab** - **Ctrl+click** any emote in chat or **Shift+click** in the emote menu to save it to Favourites (persisted immediately to disk)
- 👆 **Click-to-type emotes** - click a single emote from another user's message to insert it straight into your input

### 🎨 Themes & user card

- 🎨 **12 built-in theme presets & Themes Studio** - Dracula, Catppuccin (Mocha, Macchiato, Frappé, Latte), Nord, Tokyo Night, Gruvbox Dark, One Dark, Solarized Dark/Light, and Synthwave '84
- 🏅 **Badges under avatar** - clicking a user shows their badges in a compact grid below the profile image
- 📜 **Paginated message history** - loads today's messages first, then older days on demand (skips empty days automatically)

---

## 📁 Settings & migration

### ❓ Does this share config with regular Chatterino?

**Yes.** Chatterino Better Browser uses the **same settings location** as the official Chatterino 2 installer and portable builds:

| Mode                                                    | Config location          |
| ------------------------------------------------------- | ------------------------ |
| **Installed / zip (default)**                           | `%APPDATA%\Chatterino2\` |
| **Portable** (empty `portable` file next to the `.exe`) | Next to the executable   |

That folder contains everything: `Settings/settings.json`, accounts, window layout, favourite emotes, plugins, themes, and logs.

### 🔄 Switching from official Chatterino (recommended)

You do **not** need the official Chatterino installed to run this fork. Your settings live in `%APPDATA%\Chatterino2\` - **not** inside the old install folder - so uninstalling Chatterino is safe.

1. ⏹️ **Close** Chatterino completely
2. 🗑️ **Uninstall** the official Chatterino app from Windows _(Settings → Apps → Chatterino → Uninstall)_
   - ✅ Your splits, accounts, favourite emotes, themes, and window layout **stay saved** in `%APPDATA%\Chatterino2\`
3. 📥 Download and extract **Chatterino Better Browser** to **any folder** you want
4. 🔗 **Create a shortcut** - Desktop, Start, or taskbar
5. ▶️ Launch from your shortcut - everything loads from the same `settings.json` as before

### 📦 Portable vs installed

|          | Portable zip                      | Installed (future)                |
| -------- | --------------------------------- | --------------------------------- |
| Config   | `%APPDATA%\Chatterino2\` (shared) | `%APPDATA%\Chatterino2\` (shared) |
| Updates  | Re-download zip                   | Installer overwrites `.exe` only  |
| Best for | Trying the fork, no admin rights  | Daily driver, Start Menu shortcut |

---

## 🧩 Browser extension - one install

Everything runs through a **single** extension for **Mozilla Firefox**, **Chrome**, and **Edge**. Chat overlay, pins, channel points, chat identity, and predictions - no second extension needed.

### Mozilla Firefox

1. Install **Chatterino Better Browser** from Mozilla Add-ons (or build locally with `npm run package:extension:firefox` and load `dist/chatterino-browser-extension-firefox/manifest.json` in `about:debugging`)
2. Open the extension popup and click **Enable on Twitch** if prompted for Twitch site access
3. Restart **Chatterino Better Browser** once so the Firefox native messaging host is registered

### Chrome / Edge

1. 🌐 Open `chrome://extensions` or `edge://extensions`
2. 🔧 Enable **Developer mode**
3. 📂 Click **Load unpacked**
4. ✅ Select `chatterino-extension` from the downloaded Windows archive (or [`chatterino-extension`](./chatterino-extension/) when developing from source)
5. 🧹 Remove any old **Native Host** or **Companion** extensions if you still have them loaded

See [`chatterino-extension/README.md`](./chatterino-extension/README.md) for popup settings and troubleshooting.

> ⚠️ **Tip:** After loading the extension for the first time, restart **Chatterino Better Browser** so the native messaging host manifest is registered with your browser.

---

## 🛠️ Build from source (Windows)

### 📋 Prerequisites

- Visual Studio 2022 or later with **Desktop development with C++**
- [Qt 6.8+](https://www.qt.io/download-open-source) (MSVC 64-bit kit)
- [Conan 2](https://conan.io/downloads.html)

Full details: [BUILDING_ON_WINDOWS.md](BUILDING_ON_WINDOWS.md)

### 🔨 Build commands

Open **x64 Native Tools Command Prompt for VS**, then:

```cmd
git clone --recurse-submodules https://github.com/trinlol/chatterino-with-better-browser-integration.git
cd chatterino-with-better-browser-integration
mkdir build
cd build
conan install .. -s build_type=Release -c tools.cmake.cmaketoolchain:generator="NMake Makefiles" --build=missing --output-folder=.
cmake -G"NMake Makefiles" -DCMAKE_BUILD_TYPE=Release -DCMAKE_TOOLCHAIN_FILE="conan_toolchain.cmake" -DCMAKE_PREFIX_PATH="C:\Qt\6.8.0\msvc2022_64" ..
cmake --build . --config Release
```

Output: `build/bin/Chatterino Better Browser.exe`

Deploy Qt runtimes:

```cmd
windeployqt "bin/Chatterino Better Browser.exe" --release --no-compiler-runtime --no-translations --no-opengl-sw --dir bin/
```

---

## 📂 Repository layout

| Path                                               | Description                                            |
| -------------------------------------------------- | ------------------------------------------------------ |
| [`src/`](./src/)                                   | 🖥️ Chatterino Better Browser C++ application           |
| [`chatterino-extension/`](./chatterino-extension/) | 🧩 Unified browser extension _(the only one you need)_ |
| [`lib/`](./lib/)                                   | 📚 Vendored dependencies (git submodules)              |
| [`resources/`](./resources/)                       | 🎨 Icons, themes, assets                               |
| [`docs/showcase/`](./docs/showcase/)               | 📸 README screenshots                                  |

---

## 📥 Clone

```shell
git clone --recurse-submodules https://github.com/trinlol/chatterino-with-better-browser-integration.git
```

---

## 🔗 Upstream links

- [Chatterino 2](https://github.com/Chatterino/chatterino2)
- [Chatterino Wiki](https://wiki.chatterino.com)
- [Original browser extension](https://github.com/Chatterino/chatterino-browser-ext)

---

<div align="center">

If you find this fork useful, consider giving it a ⭐ on GitHub!

</div>
