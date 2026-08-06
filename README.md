<div align="center">

![chatterinoLogo](https://user-images.githubusercontent.com/41973452/272541622-52457e89-5f16-4c83-93e7-91866c25b606.png)

# ✨ Chatterino Better Browser

[![Release](https://img.shields.io/github/v/release/trinlol/chatterino-with-better-browser-integration?style=for-the-badge&color=9146FF&label=Release)](https://github.com/trinlol/chatterino-with-better-browser-integration/releases/latest)
[![Platform](https://img.shields.io/badge/Platform-Windows-0078D6?style=for-the-badge&logo=windows&logoColor=white)](https://github.com/trinlol/chatterino-with-better-browser-integration)
[![Extension](https://img.shields.io/badge/Extension-Chrome%20%7C%20Edge-4285F4?style=for-the-badge&logo=googlechrome&logoColor=white)](./chatterino-extension/)
[![Twitch](https://img.shields.io/badge/Twitch-Native%20Overlay-9146FF?style=for-the-badge&logo=twitch&logoColor=white)](https://www.twitch.tv)

**A sleek Chatterino 2 fork that elevates Twitch browser integration into a complete, native experience.**

</div>

---

> [!TIP]
> **Zero Extra Setup:** Runs via **one browser extension** loaded in Chrome or Edge. No separate Native Host binaries or companion applications required.

> [!IMPORTANT]
> Chatterino Better Browser stores settings locally in `%APPDATA%\Chatterino2\` and communicates only with Twitch and its associated chat services. Its updater and release notes point exclusively to this fork's [GitHub Releases](https://github.com/trinlol/chatterino-with-better-browser-integration/releases). Removing the unpacked browser extension and deleting the extracted folder will stop using the fork while leaving your original Chatterino settings untouched.

---

## 📸 Showcase

Chatterino Better Browser on a live Twitch channel — full chat overlay, synced pins, and toolbar integration beside the player.

<table>
  <tr>
    <td rowspan="2" valign="top" align="center" width="42%">
      <img src="./docs/showcase/chatterino-overlay.gif" width="300" alt="Full Chatterino chat overlay on Twitch with pinned banner, replies, and emotes">
      <br>
      <sub><b>Full chat preview</b> — pinned messages, inline input emotes, and ctrl+click to input emotes directly from user messages.</sub>
    </td>
    <td valign="top" align="center" width="58%">
      <img src="./docs/showcase/emote-picker.gif" width="300" alt="Emote favourites picker with channel, 7TV, and search tabs">
      <br>
      <sub><b>Emote favourites</b> — save per-channel, global, and 7TV emotes with search and Tab completion.</sub>
    </td>
  </tr>
  <tr>
    <td valign="top" align="center">
      <table width="100%">
        <tr>
          <td width="50%" valign="top" align="center">
            <img src="./docs/showcase/twitch-toolbar.gif" width="300" alt="Channel points menu beside the Twitch player toolbar">
            <br>
            <sub><b>Channel points</b> — balance and rewards menu beside the player.</sub>
          </td>
        </tr>
      </table>
    </td>
  </tr>
</table>

<p align="center">
  <sub>Showcase captured on <a href="https://www.twitch.tv/alveussanctuary">alveussanctuary</a> for demonstration only.</sub>
</p>

---

## 🚀 Quick start (prebuilt)

1. Download the latest **Chatterino Better Browser Windows** archive from [Releases](https://github.com/trinlol/chatterino-with-better-browser-integration/releases/latest).
2. **Uninstall** the old Chatterino application from Windows (your settings will be preserved).
3. Extract the ZIP package to your preferred directory (e.g., `C:\Apps\Chatterino Better Browser\`).
4. Right-click **`Chatterino Better Browser.exe`** → **Send to** → **Desktop (create shortcut)** (or pin it to Start/Taskbar).
5. Launch the application from your new shortcut.
6. Install the required browser extension (see [Browser extension](#-browser-extension--one-install)).
7. Restart the app after loading the extension to register native messaging.

> [!NOTE]
> **Already use Chatterino?** Your existing settings, accounts, and window layout load automatically. Uninstalling the old app does not remove your data. See [Settings & migration](#-settings--migration).

---

## 🎯 Features

### 🌐 Browser integration

| Feature | Description |
| --- | --- |
| **Pinned messages** | Moderator pins display as a compact purple banner with clickable links and a dismiss button. |
| **Clickable polls & predictions** | Voting controls appear beside the Twitch player without needing native chat open. |
| **Accurate prediction banner** | Prediction banner follows Twitch's real lock deadline instead of freezing at `2:00`. |
| **Independent activity state** | Polls and predictions update independently without overwriting each other. |
| **Channel points auto-claim** | Automatically claims bonus chests and provides a scrollable rewards menu. |
| **Chat identity button** | Badge picker in player toolbar opens Twitch's Chat Identity menu even when chat is hidden. |
| **Fullscreen-safe controls** | Moved toolbar elements automatically hide during video fullscreen mode. |
| **Stable browser overlay** | Fixes flickering, disappearing chat, and high CPU usage from native messaging. |
| **Fork-only updates** | Update prompts and release notes route exclusively to this repository. |

### 💬 Better emote input

| Feature | Description |
| --- | --- |
| **7TV-only autocomplete** | Tab completion and the `:` menu filter exclusively for 7TV emotes. |
| **Reliable Tab cycling** | Press **Tab** or **Shift+Tab** to cycle forward or backward through matching 7TV emotes. |
| **Visual emote menu** | Type `:` at the start of a word to browse 7TV emotes with live previews. |
| **Inline emotes in input** | Typed and selected 7TV emotes render as inline images in the text box. |
| **Emote favourites tab** | **Ctrl+click** any emote in chat to add it to your Favourites list. |
| **Click-to-type emotes** | Click an emote in chat to insert it directly into your message box. |

### 👤 User card

| Feature | Description |
| --- | --- |
| **Badges under avatar** | Clicking a user displays their badges in a grid under their avatar. |
| **Paginated message history** | Loads recent channel messages first, fetching older logs on demand. |

---

## 📋 Todo

- [x] Fix text spacing on pinned announcements
- [ ] Add in-chat channel point messages (for message-based redemptions)
- [ ] Theme presets
- [x] Usercard — badges and paginated message history from channel logs

---

## 📁 Settings & migration

### ❓ Does this share config with regular Chatterino?

**Yes.** Chatterino Better Browser uses the standard settings directory:

| Mode | Config location |
| --- | --- |
| **Installed / ZIP (default)** | `%APPDATA%\Chatterino2\` |
| **Portable** (empty `portable` file next to `.exe`) | Executable directory |

This folder contains all configuration files: `Settings/settings.json`, accounts, window layout, favourite emotes, plugins, themes, and logs.

### 🔄 Switching from official Chatterino (recommended)

You do not need the official Chatterino client installed to run this fork. Your settings reside in `%APPDATA%\Chatterino2\` and are not deleted when uninstalling the official client.

1. **Close** Chatterino completely.
2. **Uninstall** the official Chatterino app (Settings → Apps → Chatterino → Uninstall).
   - Your splits, accounts, favourite emotes, themes, and layouts remain saved in `%APPDATA%\Chatterino2\`.
3. Download and extract **Chatterino Better Browser** to any folder.
4. **Create a shortcut** on your Desktop, Start Menu, or Taskbar.
5. Launch from your shortcut — all existing settings will load automatically.

### 📦 Portable vs installed

| Feature | Portable ZIP | Installed |
| --- | --- | --- |
| Config | `%APPDATA%\Chatterino2\` (shared) | `%APPDATA%\Chatterino2\` (shared) |
| Updates | Re-download ZIP | Installer overwrites executable |
| Best for | Quick testing / no admin access | Daily usage & Start Menu shortcut |

---

## 🧩 Browser extension — one install

All features run through a single browser extension.

1. Open `chrome://extensions` or `edge://extensions` in your browser.
2. Enable **Developer mode**.
3. Click **Load unpacked**.
4. Select `browser-extension` from the downloaded release ZIP, or [`chatterino-extension`](./chatterino-extension/) when developing from source.
5. Remove any previously installed Native Host or Companion extensions.

See [`chatterino-extension/README.md`](./chatterino-extension/README.md) for extension settings and troubleshooting.

> [!TIP]
> **Tip:** After loading the extension, restart **Chatterino Better Browser** so the native messaging host registers with your browser.

---

## 🛠️ Build from source (Windows)

### 📋 Prerequisites

- Visual Studio 2022 or later with **Desktop development with C++**
- [Qt 6.8+](https://www.qt.io/download-open-source) (MSVC 64-bit kit)
- [Conan 2](https://conan.io/downloads.html)

Full details: [BUILDING_ON_WINDOWS.md](BUILDING_ON_WINDOWS.md)

### 🔨 Build commands

Open **x64 Native Tools Command Prompt for VS**, then run:

```cmd
git clone --recurse-submodules https://github.com/trinlol/chatterino-with-better-browser-integration.git
cd chatterino-with-better-browser-integration
mkdir build
cd build
conan install .. -s build_type=Release -c tools.cmake.cmaketoolchain:generator="NMake Makefiles" --build=missing --output-folder=.
cmake -G"NMake Makefiles" -DCMAKE_BUILD_TYPE=Release -DCMAKE_TOOLCHAIN_FILE="conan_toolchain.cmake" -DCMAKE_PREFIX_PATH="C:\Qt\6.8.0\msvc2022_64" ..
cmake --build . --config Release
```

Output binary: `build/bin/Chatterino Better Browser.exe`

Deploy Qt runtimes:

```cmd
windeployqt "bin/Chatterino Better Browser.exe" --release --no-compiler-runtime --no-translations --no-opengl-sw --dir bin/
```

---

## 📂 Repository layout

| Path | Description |
| --- | --- |
| [`src/`](./src/) | Chatterino Better Browser C++ application source |
| [`chatterino-extension/`](./chatterino-extension/) | Unified browser extension |
| [`lib/`](./lib/) | Vendored dependencies (git submodules) |
| [`resources/`](./resources/) | Icons, themes, and assets |
| [`docs/showcase/`](./docs/showcase/) | README preview images and media |

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

## Fork Curated By

<table>
  <tr>
    <td align="center" valign="top" width="200">
      <a href="https://chatgpt.com"><img src="https://img.shields.io/badge/ChatGPT-412991?style=for-the-badge&logo=openai&logoColor=white" alt="ChatGPT by OpenAI" /></a><br />
      <sub><b>ChatGPT by OpenAI</b> — development, debugging, and documentation assistance</sub>
    </td>
  </tr>
</table>

</div>
