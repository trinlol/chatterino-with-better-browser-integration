if (typeof importScripts === 'function') {
  importScripts('protocol.js');
}

const ignoredPages = new Set([
  'directory',
  'downloads',
  'friends',
  'inventory',
  'jobs',
  'messages',
  'p',
  'payments',
  'popout',
  'prime',
  'settings',
  'store',
  'subscriptions',
  'turbo',
  'videos',
  'wallet',
]);

class AttachedWindows {
  /** @param {number} winID */
  static async isAttached(winID) {
    const windows = await AttachedWindows.#load();
    return winID in windows;
  }

  /** @param {number} winID */
  static async markAttached(winID) {
    const windows = await AttachedWindows.#load();
    windows[winID] = true;
    await AttachedWindows.#save(windows);
  }

  /** @param {number} winID */
  static async markDetatched(winID) {
    const windows = await AttachedWindows.#load();
    delete windows[winID];
    await AttachedWindows.#save(windows);
  }

  /** @returns {Promise<number[]>} */
  static async detachAll() {
    const windows = await AttachedWindows.#load();
    await AttachedWindows.#save({});
    return Object.keys(windows).map(Number);
  }

  /** @returns {Promise<Record<number, boolean>} */
  static #load() {
    return chrome.storage.session
      .get('attachedWindows')
      .then(({ attachedWindows }) => attachedWindows ?? {})
      .catch(() => ({}));
  }

  /** @param {Promise<Record<number, boolean>} attachedWindows */
  static #save(attachedWindows) {
    return chrome.storage.session.set({ attachedWindows }).catch(console.warn);
  }
}

/**
 * @typedef {{
 *    replaceTwitchChat: boolean
 * }} SettingTypes
 */

class Settings {
  static #defaults = {
    replaceTwitchChat: async () => {
      const platform = await chrome.runtime.getPlatformInfo();
      return platform.os === 'win';
    },
  };

  /**
   * @template {keyof SettingTypes} T
   * @param {T} key
   * @returns {Promise<SettingTypes[T]>}
   */
  static async get(key) {
    const maybeVal = await this.#getOrUndefined(key);
    if (maybeVal === undefined) {
      return await this.#defaults[key]();
    }
    return maybeVal;
  }

  /**
   * @template {keyof SettingTypes} T
   * @param {T} key
   * @returns {Promise<SettingTypes[T] | undefined>}
   */
  static async #getOrUndefined(key) {
    try {
      const settings = await chrome.storage.local.get(key);
      return settings[key];
    } catch (e) {
      console.warn(`Failed to get ${key}`, e);
    }
    return undefined;
  }

  /**
   * @template {keyof SettingTypes} T
   * @param {T} key
   * @param {SettingTypes[T]} value
   */
  static async set(key, value) {
    try {
      await chrome.storage.local.set({ [key]: value });
    } catch (e) {
      console.warn(`Failed to set {key} to`, value);
    }
  }
}

/// return channel name if it should contain a chat
function matchChannelName(url) {
  if (!url) return undefined;

  const [, channelName] =
    url.match(/^https?:\/\/(?:www\.)?twitch\.tv\/(\w+)\/?(?:\?.*)?$/) ?? [];

  if (channelName && !ignoredPages.has(channelName)) {
    return channelName;
  }

  return undefined;
}

const appName = 'com.chatterino.chatterino';
let port = null;
let portConnectBlocked = false;
let lastPortConnectAttempt = 0;
let lastNativeError = '';
let lastNativeConnectedAt = 0;
const PORT_CONNECT_COOLDOWN_MS = 5000;

async function safeGetWindow(windowId) {
  if (windowId === undefined || windowId === chrome.windows.WINDOW_ID_NONE) {
    return null;
  }
  try {
    return await chrome.windows.get(windowId);
  } catch (error) {
    return null;
  }
}

// gets the port for communication with chatterino
function getPort() {
  if (portConnectBlocked) {
    return null;
  }
  if (port) {
    return port;
  }
  connectPort();
  return port;
}

// connect to port
function connectPort() {
  if (portConnectBlocked) {
    return;
  }
  const now = Date.now();
  if (now - lastPortConnectAttempt < PORT_CONNECT_COOLDOWN_MS) {
    return;
  }
  lastPortConnectAttempt = now;

  try {
    port = chrome.runtime.connectNative(appName);
    lastNativeError = '';
    lastNativeConnectedAt = Date.now();
  } catch (error) {
    portConnectBlocked = true;
    port = null;
    lastNativeError = error?.message || String(error);
    console.warn('[Chatterino] Native messaging connect failed:', error);
    return;
  }

  port.onMessage.addListener(msg => {
    if (typeof msg === 'object' && msg.type === 'status') {
      switch (msg.status) {
        case 'exiting-host':
          console.info(
            `Native host is exiting: '${msg.reason ?? '<unknown>'}'`,
          );
          break;
        default:
          break;
      }
      return;
    }

    if (msg?.action === 'sendNativeChat') {
      void routeSendNativeChat(msg);
    }
  });
  port.onDisconnect.addListener(() => {
    const lastError = chrome.runtime.lastError?.message ?? '';
    lastNativeError = lastError;

    port = null;

    if (
      lastError.includes('forbidden') ||
      lastError.includes('not found') ||
      lastError.includes('Specified native messaging host')
    ) {
      portConnectBlocked = true;
      console.warn(
        '[Chatterino] Native messaging is blocked. Restart Chatterino after loading this extension, or add its extension ID under Settings → General → Extra extension IDs.',
        lastError,
      );
    }
  });
}

// disconnect from port
function disconnectPort() {
  if (port) {
    port.disconnect();
    port = null;
  }
}

// tab activated
chrome.tabs.onActivated.addListener(async activeInfo => {
  const tab = await chrome.tabs.get(activeInfo.tabId).catch(() => null);
  if (!tab?.url) return;

  const window = await safeGetWindow(tab.windowId);
  if (!window?.focused) return;

  await onTabSelected(tab.url, tab);
});

// url changed
chrome.tabs.onUpdated.addListener(async (tabId, changeInfo, tab) => {
  if (!tab.highlighted) return;

  const window = await safeGetWindow(tab.windowId);
  if (!window?.focused) return;

  await onTabSelected(tab.url, tab);
});

// tab detached
chrome.tabs.onDetached.addListener(async (tabId, detachInfo) => {
  await tryDetach(detachInfo.oldWindowId);
});

// tab closed
chrome.windows.onRemoved.addListener(async windowId => {
  await tryDetach(windowId);
});

// window selected
chrome.windows.onFocusChanged.addListener(async windowId => {
  if (windowId == -1) return;

  const window = await safeGetWindow(windowId);
  if (!window) return;

  // this returns all tabs when the query fails
  const tabs = await chrome.tabs.query({
    windowId: windowId,
    highlighted: true,
  });
  if (tabs.length === 1) {
    let tab = tabs[0];

    await onTabSelected(tab.url, tab);
  }
});

// attach or detach from tab
async function onTabSelected(url, tab) {
  const channelName = matchChannelName(url);

  if (!channelName) {
    // detach from window
    await tryDetach(tab.windowId);
    return;
  }

  // A chat-resized message can be emitted while the tab is in the background.
  // Those messages are intentionally ignored below, because attaching the
  // native window to an inactive tab would put it over the wrong browser
  // window. Ask the content script to measure again once this tab is active.
  // Without this handshake, a background-loaded Twitch tab stays detached
  // until a later page focus or mouse event happens to trigger a measurement.
  if (tab.id !== undefined) {
    try {
      await chrome.tabs.sendMessage(tab.id, { action: 'requestChatRect' });
    } catch (error) {
      // The content script may not have loaded yet. Its initial measurement
      // will attach the chat once it does, so there is nothing to detach here.
    }
  }
}

async function getIntegrationHealth() {
  const [tab] = await chrome.tabs
    .query({ active: true, currentWindow: true, url: '*://*.twitch.tv/*' })
    .catch(() => []);
  let content = null;
  if (tab?.id) {
    try {
      content = await chrome.tabs.sendMessage(tab.id, {
        action: 'getIntegrationHealth',
      });
    } catch (error) {
      content = null;
    }
  }
  return {
    native: {
      connected: Boolean(port),
      blocked: portConnectBlocked,
      lastError: lastNativeError,
      connectedAt: lastNativeConnectedAt,
      protocolVersion: globalThis.ChatterinoProtocol.CURRENT_VERSION,
    },
    tab: tab
      ? { id: tab.id, channel: matchChannelName(tab.url || '') || '' }
      : null,
    content,
    checkedAt: Date.now(),
  };
}

function isFirefox() {
  // Only Firefox has browser.*
  return typeof browser !== 'undefined';
}

async function calcDisplayScaleFactor(tabId, dpr) {
  const zoom = await chrome.tabs.getZoom(tabId);
  let scaleFactor = dpr / zoom;
  // On Firefox devicePixelRatio is not just zoom * scaleFactor. There seems to
  // be some additional multiplier, which makes sure, that dimensions of the
  // CSS pixel grid are exact integers (ex. with 175% scaling on 1080p dpr is
  // 1.7647).
  //
  // To workaround that, we will assume, that display scaling is set to a
  // multiple of 25%, which is recommend in Windows, and round to that. This
  // will allow us to get the _actual_ zoom level later with that multiplier
  // included, which lines up everything nicely.
  if (isFirefox()) {
    scaleFactor = Math.round(scaleFactor / 0.25) * 0.25;
  }
  return scaleFactor;
}

function forwardNativeMessage(message) {
  let outbound;
  try {
    outbound = globalThis.ChatterinoProtocol.normalizeOutbound(message);
  } catch (error) {
    console.warn('[Chatterino] Invalid native message:', error.message, message);
    return;
  }
  const port = getPort();
  if (port) {
    port.postMessage(outbound);
    return;
  }

  chrome.runtime.sendNativeMessage(appName, outbound, () => {
    if (chrome.runtime.lastError) {
      console.warn(
        '[Chatterino] Native messaging error:',
        chrome.runtime.lastError.message,
      );
    }
  });
}

async function findTwitchTabForChannel(channelName) {
  const tabs = await chrome.tabs.query({ url: '*://*.twitch.tv/*' });
  if (channelName) {
    const normalized = channelName.toLowerCase();
    const matched = tabs.find(tab => {
      const name = matchChannelName(tab.url || '');
      return name && name.toLowerCase() === normalized;
    });
    if (matched) {
      return matched;
    }
  }

  const highlighted = tabs.filter(tab => tab.highlighted);
  return highlighted[0] || tabs[0] || null;
}

async function routeSendNativeChat(message) {
  const tab = await findTwitchTabForChannel(message.channel);
  if (!tab?.id) {
    return;
  }

  try {
    await chrome.tabs.sendMessage(tab.id, {
      action: 'sendNativeChat',
      message: message.message,
      channel: message.channel,
    });
  } catch (error) {
    console.warn('[Chatterino] Failed to route native chat send:', error);
  }
}

// receiving messages from content scripts and the popup
chrome.runtime.onMessage.addListener((message, sender, callback) => {
  if (
    message.action === 'engagement' ||
    message.action === 'prediction' ||
    message.action === 'pin' ||
    message.action === 'rewardPending' ||
    message.action === 'rewardClear'
  ) {
    forwardNativeMessage(message);
    return;
  }

  switch (message.type) {
    case 'get-integration-health':
      getIntegrationHealth().then(callback);
      return true;
    case 'get-setting':
      Settings.get(message.key).then(callback);
      return true;
    case 'set-setting':
      (async () => {
        await Settings.set(message.key, message.value);

        for (const id of await AttachedWindows.detachAll()) {
          // they're already cleared
          await sendDetach(id);
        }
        await updateBadge();
      })();
      break;
    case 'get-os':
      chrome.runtime.getPlatformInfo(info => callback(info.os));

      // We need to return true here so that `callback` will remain valid
      // after this function returns. This behavior is documented here:
      // https://developer.mozilla.org/en-US/docs/Mozilla/Add-ons/WebExtensions/API/runtime/onMessage
      return true;
      break;
    case 'location-updated':
      safeGetWindow(sender.tab.windowId).then(window => {
        if (!window?.focused) return;

        let data = {
          action: 'select',
          type: 'twitch',
          winId: sender.tab.windowId,
          version: 0,
          name: matchChannelName(sender.tab.url),
        };
        forwardNativeMessage(data);
      });
      break;
    case 'chat-resized':
      // is tab highlighted
      if (!sender.tab.highlighted) return;

      // is window focused
      safeGetWindow(sender.tab.windowId).then(async window => {
        if (!window?.focused) return;

        const dpr = message.dpr ?? 1;
        // devicePixelRatio combines both zoom and display scaling set in the
        // system. But the UI elements (sidebars) are unaffected by the zoom
        // level of the tab itself. So we need to separate the two.
        const scaleFactor = await calcDisplayScaleFactor(sender.tab.id, dpr);
        const zoom = dpr / scaleFactor;
        // adjust for sidebars and vertical tabs
        let xOffset = (message.viewportX ?? 0) / scaleFactor;
        let size = {
          x: message.rect.x * zoom + xOffset,
          pixelRatio: 1,
          width: Math.floor(message.rect.width * zoom),
          height: Math.floor(message.rect.height * zoom),
        };

        // attach to window
        await tryAttach(sender.tab.windowId, window.state == 'fullscreen', {
          name: matchChannelName(sender.tab.url),
          size: size,
        });
      });
      break;
    case 'detach':
      tryDetach(sender.tab.windowId);
      break;
  }
});

// attach chatterino to a chrome window
async function tryAttach(windowId, fullscreen, data) {
  data.action = 'select';
  if (await Settings.get('replaceTwitchChat')) {
    if (fullscreen) {
      data.attach_fullscreen = true;
    } else {
      data.attach = true;
    }
  }
  data.type = 'twitch';
  data.winId = '' + windowId;
  data.version = 0;

  let port = getPort();

  if (port) {
    port.postMessage(data);
  }

  await AttachedWindows.markAttached(windowId);
}

/**
 * Detach chatterino from a chrome window
 * @param {number} windowId
 */
async function tryDetach(windowId) {
  if (await AttachedWindows.isAttached(windowId)) {
    sendDetach(windowId);
    await AttachedWindows.markDetatched(windowId);
  }
}

function sendDetach(winID) {
  forwardNativeMessage({ action: 'detach', version: 0, winId: winID.toString() });
}

async function updateBadge() {
  chrome.action.setBadgeText({
    text: (await Settings.get('replaceTwitchChat')) ? '' : 'off',
  });
}

function getPreviousTabs() {
  return chrome.storage.session
    .get('previousTabs')
    .then(({ previousTabs }) => new Set(previousTabs ?? []))
    .catch(() => new Set());
}

async function setPreviousTabs(tabs) {
  await chrome.storage.session.set({ previousTabs: [...tabs] });
}

async function syncTabs() {
  function compareTabs(lhs, rhs) {
    if (lhs.size !== rhs.size) {
      return false;
    }

    for (const value of lhs) {
      if (!rhs.has(value)) {
        return false;
      }
    }
    return true;
  }

  let previousTabs = await getPreviousTabs();

  const tabs = await chrome.tabs.query({ url: '*://*.twitch.tv/*' });
  const currentTabs = new Set(
    tabs.map(t => matchChannelName(t.url)).filter(Boolean),
  );
  if (compareTabs(previousTabs, currentTabs)) {
    return;
  }
  previousTabs = currentTabs;

  forwardNativeMessage({ action: 'sync', twitchChannels: [...currentTabs] });

  await setPreviousTabs(previousTabs);
}
syncTabs();

chrome.tabs.onCreated.addListener(() => syncTabs());
chrome.tabs.onRemoved.addListener(() => syncTabs());
chrome.tabs.onUpdated.addListener((id, changeInfo) => {
  if ('url' in changeInfo) {
    syncTabs();
  }
});
