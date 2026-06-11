![chatterinoLogo](https://user-images.githubusercontent.com/41973452/272541622-52457e89-5f16-4c83-93e7-91866c25b606.png)

# Chatterino Better Browser

A fork of [Chatterino 2](https://github.com/Chatterino/chatterino2) focused on making Twitch browser integration feel native and complete. It overlays Chatterino chat on Twitch and adds companion features for pinned messages, predictions, channel points, and more.

---

## Quick start (prebuilt)

1. Download **Chatterino-Better-Browser-Windows-x64-v1.0.0.zip** from [Releases](https://github.com/trinlol/chatterino-with-better-browser-integration/releases).
2. Extract the zip and run **`Chatterino Better Browser.exe`**.
3. Install both browser extensions below (required for full integration).
4. Restart the app after loading extensions so native messaging registers.

---

## Features

- **Pinned messages** — moderator pins show as a purple banner in Chatterino with a dismiss button
- **Predictions & polls** — live prediction/poll UI integrated into the Twitch toolbar
- **Channel points auto-claim** — claims bonus chests automatically
- **Stable browser overlay** — fixes flicker, disappearing chat, and runaway CPU from native messaging
- **Emote click-to-type** — click emotes in chat to insert them into your input

---

## Browser extensions (required)

You need **both** extensions for full functionality:

### 1. Chatterino Native Host

Replaces Twitch chat with the Chatterino overlay and reports window geometry to the app.

1. Open `chrome://extensions` or `edge://extensions`
2. Enable **Developer mode**
3. Click **Load unpacked**
4. Select the [`chatterino-browser-ext/src`](./chatterino-browser-ext/src) folder

See [`chatterino-browser-ext/README.md`](./chatterino-browser-ext/README.md) for Firefox build steps.

### 2. Chatterino Companion

Handles pinned messages, channel points, predictions, and anti-wipe restoration.

1. On the same extensions page, click **Load unpacked** again
2. Select the [`chatterino-companion`](./chatterino-companion/) folder

See [`chatterino-companion/README.md`](./chatterino-companion/README.md) for details.

> **Tip:** After loading extensions for the first time, restart **Chatterino Better Browser** so the native messaging host manifest is registered with your browser.

---

## Build from source (Windows)

### Prerequisites

- Visual Studio 2022 or later with **Desktop development with C++**
- [Qt 6.8+](https://www.qt.io/download-open-source) (MSVC 64-bit kit)
- [Conan 2](https://conan.io/downloads.html)

Full details: [BUILDING_ON_WINDOWS.md](BUILDING_ON_WINDOWS.md)

### Build commands

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

## Repository layout

| Path | Description |
|------|-------------|
| [`src/`](./src/) | Chatterino Better Browser C++ application |
| [`chatterino-browser-ext/`](./chatterino-browser-ext/) | Native Host browser extension (chat overlay) |
| [`chatterino-companion/`](./chatterino-companion/) | Companion extension (pins, points, predictions) |
| [`lib/`](./lib/) | Vendored dependencies (git submodules) |
| [`resources/`](./resources/) | Icons, themes, assets |

---

## Clone

```shell
git clone --recurse-submodules https://github.com/trinlol/chatterino-with-better-browser-integration.git
```

---

## Upstream links

- [Chatterino 2](https://github.com/Chatterino/chatterino2)
- [Chatterino Wiki](https://wiki.chatterino.com)
- [Original browser extension](https://github.com/Chatterino/chatterino-browser-ext)
