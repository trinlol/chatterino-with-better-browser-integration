if (typeof importScripts === "function") {
  importScripts("protocol.js", "extension-lifecycle.js");
}

// [BACKGROUND-STARTUP] Verify background script loads
console.log('[BACKGROUND-STARTUP] Background script loaded at:', new Date().toISOString());
console.log('[BACKGROUND-STARTUP] Native messaging available:', !!chrome.runtime.connectNative);

// Unit-test and migration hosts may evaluate this composition root without
// importScripts. Keep a tiny compatible fallback; production MV3 workers load
// the dependency-light seams above.
const Lifecycle = globalThis.ChatterinoLifecycle ?? {
  createBackoffPolicy: () => ({
    reset() {},
    next() {
      return { attempt: 1, delay: 1000, retryAt: Date.now() + 1000 };
    },
  }),
  createConnectionState: () => ({
    state: "idle",
    retryAt: 0,
    retryAttempt: 0,
    transition(next) {
      this.state = next;
      return { retryAt: 0, retryAttempt: 0 };
    },
    snapshot() {
      return { state: "idle", retryAt: 0, retryAttempt: 0 };
    },
  }),
  createSessionStore: () => ({
    all: async () => ({}),
    get: async () => null,
    put: async () => {},
    remove: async () => {},
    clear: async () => {},
  }),
  createTransitionRing: () => ({
    record() {},
    snapshot: () => [],
  }),
};

const ignoredPages = new Set([
  "directory",
  "downloads",
  "friends",
  "inventory",
  "jobs",
  "messages",
  "p",
  "payments",
  "popout",
  "prime",
  "settings",
  "store",
  "subscriptions",
  "turbo",
  "videos",
  "wallet",
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
      .get("attachedWindows")
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
      return platform.os === "win";
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

const appName = "com.chatterino.chatterino";
let port = null;
let portConnectBlocked = false; // legacy health field; state remains recoverable
let lastPortConnectAttempt = 0;
let lastNativeError = "";
let lastNativeConnectedAt = 0;
const PORT_CONNECT_COOLDOWN_MS = 5000;
const PORT_RETRY_MAX_MS = 60000;
// Keep the lease comfortably longer than the one-minute reconciliation alarm
// so a delayed alarm still has time to renew an otherwise healthy session.
const NATIVE_LEASE_MS = 180000;
const ATTACH_ACK_TIMEOUT_MS = 750;
const ATTACH_MAX_ATTEMPTS = 40;
const STARTUP_REPLAY_WINDOW_MS = 15000;
const nativeConnection = Lifecycle.createConnectionState();
const nativeBackoff = Lifecycle.createBackoffPolicy({
  baseMs: PORT_CONNECT_COOLDOWN_MS,
  maxMs: PORT_RETRY_MAX_MS,
});
const sessionStore = Lifecycle.createSessionStore(
  typeof chrome !== "undefined" ? chrome.storage?.session : undefined
);
const transitionRing = Lifecycle.createTransitionRing();
let nativeRetryTimer = null;
let nativeSupportsV2 = false;
let nativeRetryAt = 0;
let nativeRetryAlarmName = "chatterino-native-reconnect";
let workerRecoveryPending = true;
// Starting Chatterino temporarily takes OS focus from Edge. Permit one recent
// readiness-triggered geometry reply to cross that focus transition.
const pendingStartupReplayTabs = new Map();
const pendingAttachRequests = new Map();
// Invalidate asynchronous measurements as soon as the visible tab changes.
const windowRevisions = new Map();
function windowRevision(windowId) {
  return windowRevisions.get(String(windowId)) || 0;
}
function invalidateWindow(windowId) {
  windowRevisions.set(String(windowId), windowRevision(windowId) + 1);
  clearPendingAttach(windowId);
}
async function isCurrentChannelTab(tabId, windowId, channel) {
  const tab = await chrome.tabs.get(tabId).catch(() => null);
  return tab?.active === true && tab.windowId === Number(windowId) &&
    matchChannelName(tab.url) === channel;
}
let nextAttachRequestId = 1;

function recordTransition(event, details = {}) {
  transitionRing.record("background", event, {
    ...details,
    state: nativeConnection.state,
  });
}

function setNativeState(next, details = {}) {
  const previous = nativeConnection.state;
  const transition = nativeConnection.transition(next, details);
  nativeRetryAt = transition.retryAt || 0;
  portConnectBlocked = next === "blocked";
  recordTransition("connection-state", {
    from: previous,
    to: next,
    ...details,
  });
  void persistRecoveryState();
}

async function persistRecoveryState() {
  try {
    await chrome.storage.session.set({
      nativeLifecycle: {
        state: nativeConnection.state,
        retryAt: nativeRetryAt,
        retryAttempt: nativeConnection.retryAttempt,
        lastError: lastNativeError,
      },
    });
  } catch {
    // The worker can still operate if session storage is temporarily absent.
  }
}

async function rehydrateRecoveryState() {
  try {
    const { nativeLifecycle } =
      await chrome.storage.session.get("nativeLifecycle");
    if (nativeLifecycle?.retryAt > Date.now()) {
      nativeRetryAt = nativeLifecycle.retryAt;
      lastNativeError = String(nativeLifecycle.lastError || "");
      setNativeState("backoff", {
        retryAt: nativeRetryAt,
        retryAttempt: nativeLifecycle.retryAttempt || 0,
        reason: "worker-rehydrated",
      });
    }
  } catch {
    // Reconciliation below remains the source of truth when storage is empty.
  }
}

function scheduleNativeReconnect(reason = "retry", immediate = false) {
  if (nativeRetryTimer !== null) {
    clearTimeout(nativeRetryTimer);
    nativeRetryTimer = null;
  }
  const retry = immediate
    ? { attempt: nativeConnection.retryAttempt, delay: 0, retryAt: Date.now() }
    : nativeBackoff.next(reason);
  nativeRetryAt = retry.retryAt;
  setNativeState(reason === "configuration" ? "blocked" : "backoff", {
    retryAt: retry.retryAt,
    retryAttempt: retry.attempt,
    reason,
  });

  if (chrome.alarms?.create) {
    try {
      chrome.alarms.create(nativeRetryAlarmName, {
        when: Math.max(Date.now() + 50, retry.retryAt),
      });
    } catch {
      // setTimeout below is the in-process fallback.
    }
  }
  nativeRetryTimer = setTimeout(
    () => {
      nativeRetryTimer = null;
      if (Date.now() >= nativeRetryAt) connectPort(true);
    },
    Math.max(0, retry.delay)
  );
}

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

async function requestActiveTwitchChatRect() {
  const [tab] = await chrome.tabs
    .query({
      active: true,
      currentWindow: true,
      url: "*://*.twitch.tv/*",
    })
    .catch(() => []);
  if (!tab?.url) return;

  await onTabSelected(tab.url, tab, { startupReplay: true });
}

function clearPendingAttach(windowId) {
  const key = String(windowId);
  const pending = pendingAttachRequests.get(key);
  if (!pending) return;

  clearTimeout(pending.timerId);
  pendingAttachRequests.delete(key);
}

function createSessionId() {
  if (typeof globalThis.crypto?.randomUUID !== "function") {
    throw new Error("secure session identifiers are unavailable");
  }
  return `browser:${globalThis.crypto.randomUUID()}`;
}

async function ensureDesiredSession(windowId, tabId, channel) {
  const sessions = await sessionStore.all();
  const existing = Object.values(sessions).find(
    (candidate) =>
      Number(candidate.windowId) === Number(windowId) &&
      Number(candidate.tabId) === Number(tabId)
  );
  const normalizedChannel = String(channel || "").toLowerCase();
  const channelChanged =
    existing &&
    String(existing.channel || "").toLowerCase() !== normalizedChannel;
  const newAttachment = channelChanged || existing?.desired === false;
  const sessionId = existing?.sessionId || createSessionId();
  const session = {
    sessionId,
    windowId: Number(windowId),
    tabId: Number.isInteger(tabId) ? tabId : undefined,
    channel: normalizedChannel,
    generation: existing
      ? Number(existing.generation || 0) + (newAttachment ? 1 : 0)
      : 1,
    desired: true,
    attached: newAttachment ? false : existing?.attached === true,
    retryAt: existing?.retryAt || 0,
    retryAttempt: existing?.retryAttempt || 0,
    leaseExpiresAt: newAttachment ? 0 : existing?.leaseExpiresAt || 0,
  };
  await sessionStore.put(session);
  return session;
}

async function sessionsForWindow(windowId) {
  const sessions = await sessionStore.all();
  return Object.values(sessions).filter(
    (session) => String(session.windowId) === String(windowId)
  );
}

async function sendOverlayState(session, state, reason = "") {
  if (!session?.tabId) return;
  try {
    await chrome.tabs.sendMessage(session.tabId, {
      action: "nativeAttachState",
      state,
      reason,
      sessionId: session.sessionId,
      generation: session.generation,
      leaseExpiresAt: session.leaseExpiresAt || 0,
    });
  } catch {
    // A closed or navigating tab will be reconciled from persisted desired state.
  }
}

async function markSessionLost(message, reason) {
  const sessions = await sessionStore.all();
  const candidates = Object.values(sessions).filter((session) => {
    if (message.sessionId && session.sessionId !== message.sessionId)
      return false;
    if (
      message.generation !== undefined &&
      session.generation !== message.generation
    )
      return false;
    if (
      message.tabId !== undefined &&
      Number(session.tabId) !== Number(message.tabId)
    )
      return false;
    if (
      message.browserWindowId !== undefined &&
      Number(session.windowId) !== Number(message.browserWindowId)
    )
      return false;
    if (
      message.winId !== undefined &&
      String(session.windowId) !== String(message.winId)
    )
      return false;
    return true;
  });
  for (const session of candidates) {
    session.attached = false;
    session.leaseExpiresAt = 0;
    await sessionStore.put(session);
    await sendOverlayState(session, "revealed", reason);
  }
}

function scheduleAttachRetry(windowId, requestId) {
  const key = String(windowId);
  const pending = pendingAttachRequests.get(key);
  if (!pending || pending.requestId !== requestId) return;

  pending.timerId = setTimeout(() => {
    const current = pendingAttachRequests.get(key);
    if (!current || current.requestId !== requestId) return;
    if (current.attempts >= ATTACH_MAX_ATTEMPTS) {
      pendingAttachRequests.delete(key);
      return;
    }

    current.attempts += 1;
    const nativePort = getPort();
    if (nativePort) {
      nativePort.postMessage({
        ...current.message,
        browserWindowFocused: false,
        startupReplay: true,
      });
    }
    scheduleAttachRetry(windowId, requestId);
  }, ATTACH_ACK_TIMEOUT_MS);
}

function postAttachUntilAcknowledged(windowId, message, sessionMetadata = {}) {
  clearPendingAttach(windowId);

  // Protocol v2 uses one correlation ID end-to-end. The native host prefers
  // requestId and mirrors it into attachRequestId in its acknowledgement, so
  // generating a second ID here makes every valid acknowledgement look stale.
  const requestId =
    message.requestId ||
    message.attachRequestId ||
    `${Date.now()}-${nextAttachRequestId++}`;
  const correlatedMessage = { ...message, attachRequestId: requestId };
  if (Number(message.protocolVersion) >= 2) {
    correlatedMessage.requestId = requestId;
  }
  const pending = {
    attempts: 1,
    message: correlatedMessage,
    requestId,
    sessionId: sessionMetadata.sessionId || message.sessionId || null,
    tabId: sessionMetadata.tabId ?? message.tabId,
    generation: sessionMetadata.generation ?? message.generation,
    revision: sessionMetadata.revision ?? windowRevision(windowId),
    timerId: null,
  };
  pendingAttachRequests.set(String(windowId), pending);

  if (pending.sessionId) {
    void sessionStore.put({
      sessionId: pending.sessionId,
      windowId: Number(windowId),
      tabId: pending.tabId,
      channel: message.name,
      generation: pending.generation,
      desired: true,
      attached: false,
      retryAt: 0,
      retryAttempt: 0,
      leaseExpiresAt: 0,
    });
  }

  const nativePort = getPort();
  if (nativePort) {
    nativePort.postMessage(pending.message);
  }
  scheduleAttachRetry(windowId, requestId);
}

function acknowledgeAttachedWindow(message) {
  console.log('[ATTACH-DEBUG] acknowledgeAttachedWindow called');
  const key = String(message.winId ?? message.browserWindowId ?? "");
  console.log('[ATTACH-DEBUG] Window key:', key);
  const pending = pendingAttachRequests.get(key);
  console.log('[ATTACH-DEBUG] Pending request:', pending);
  console.log('[ATTACH-DEBUG] All pending keys:', Array.from(pendingAttachRequests.keys()));

  const responseRequestId = message.attachRequestId ?? message.requestId;
  console.log('[ATTACH-DEBUG] Response request ID:', responseRequestId);
  console.log('[ATTACH-DEBUG] Expected request ID:', pending?.requestId);

  if (!pending || pending.requestId !== responseRequestId) {
    console.error('[ATTACH-DEBUG] ACKNOWLEDGEMENT REJECTED!', {
      reason: !pending ? 'no pending request' : 'request ID mismatch',
      expected: pending?.requestId,
      received: responseRequestId
    });
    return;
  }

  console.log('[ATTACH-DEBUG] Acknowledgement accepted, proceeding...');

  void (async () => {
    if (!(await isCurrentChannelTab(pending.tabId, key, pending.message.name)) ||
        pendingAttachRequests.get(key) !== pending ||
        pending.revision !== windowRevision(key)) return;
    clearPendingAttach(key);
    await AttachedWindows.markAttached(key);
    const session = message.sessionId
      ? await sessionStore.get(message.sessionId)
      : pending.sessionId
        ? await sessionStore.get(pending.sessionId)
        : null;
    if (session) {
      if (!session.desired || pending.revision !== windowRevision(key)) return;
      if (
        message.generation !== undefined &&
        message.generation !== session.generation
      ) {
        return;
      }
      session.attached = true;
      session.leaseExpiresAt = nativeSupportsV2
        ? Number(message.leaseExpiresAt) || Date.now() + NATIVE_LEASE_MS
        : 0;
      await sessionStore.put(session);
      if (pending.revision !== windowRevision(key)) return;
      console.log('[ATTACH-DEBUG] About to send overlay state "attached" to tab:', session.tabId, 'session:', session);
      await sendOverlayState(session, "attached");
      console.log('[ATTACH-DEBUG] Sent overlay state "attached"');
    }
  })();
}

// gets the port for communication with chatterino
function getPort() {
  if (port && nativeConnection.state === "connected") {
    return port;
  }
  if (nativeRetryAt > Date.now()) {
    return null;
  }
  connectPort();
  return port;
}

// connect to port
function connectPort(fromRetry = false) {
  if (port || nativeConnection.state === "connecting") {
    return;
  }
  const now = Date.now();
  if (!fromRetry && now - lastPortConnectAttempt < PORT_CONNECT_COOLDOWN_MS) {
    return;
  }
  lastPortConnectAttempt = now;
  setNativeState("connecting", { reason: fromRetry ? "retry" : "demand" });

  console.log('[ATTACH-DEBUG] Attempting to connect to native host:', appName);
  try {
    port = chrome.runtime.connectNative(appName);
    console.log('[ATTACH-DEBUG] Native host connection successful');
    lastNativeError = "";
    lastNativeConnectedAt = Date.now();
    nativeBackoff.reset();
    nativeRetryAt = 0;
    setNativeState("connected", { reason: "connected" });
  } catch (error) {
    port = null;
    lastNativeError = error?.message || String(error);
    console.error("[ATTACH-DEBUG] Native messaging connect failed:", error);
    console.warn("[Chatterino] Native messaging connect failed:", error);
    scheduleNativeReconnect(
      /forbidden|not found|specified native messaging host/i.test(
        lastNativeError
      )
        ? "configuration"
        : "connect-failed"
    );
    return;
  }

  port.onMessage.addListener((msg) => {
    if (msg?.action === "nativeChatResult") {
      void routeNativeChatResult(msg);
      return;
    }
    if (typeof msg === "object" && msg.type === "status") {
      if (
        msg.status === "nativeChatResult" ||
        msg.action === "nativeChatResult"
      ) {
        void routeNativeChatResult(msg);
        return;
      }
      switch (msg.status) {
        case "native-host-ready":
        case "desktop-ready":
          nativeSupportsV2 =
            Number(msg.protocolVersion) >= 2 ||
            msg.capabilities?.includes?.("sessions") === true;
          console.log(
            "[ATTACH-DEBUG] Handshake received:",
            msg.status,
            "protocolVersion:",
            msg.protocolVersion,
            "=> nativeSupportsV2:",
            nativeSupportsV2
          );
          if (nativeSupportsV2) {
            recordTransition("native-capabilities", {
              protocolVersion: msg.protocolVersion,
            });
          }
          void requestActiveTwitchChatRect();
          break;
        case "chat-attached":
          console.log('[ATTACH-DEBUG] Native sent chat-attached:', {
            winId: msg.winId,
            browserWindowId: msg.browserWindowId,
            attachRequestId: msg.attachRequestId,
            requestId: msg.requestId,
            sessionId: msg.sessionId,
            generation: msg.generation,
            leaseExpiresAt: msg.leaseExpiresAt
          });
          acknowledgeAttachedWindow(msg);
          break;
        case "lease-renewed":
          void (async () => {
            const session = msg.sessionId
              ? await sessionStore.get(msg.sessionId)
              : null;
            const leaseExpiresAt = Number(msg.leaseExpiresAt);
            if (
              !session ||
              (msg.generation !== undefined &&
                Number(msg.generation) !== Number(session.generation)) ||
              !Number.isFinite(leaseExpiresAt) ||
              leaseExpiresAt <= Date.now()
            ) {
              return;
            }
            session.attached = true;
            session.leaseExpiresAt = leaseExpiresAt;
            await sessionStore.put(session);
            await sendOverlayState(session, "attached");
          })();
          break;
        case "attachment-lost":
        case "attachment-rejected":
          void markSessionLost(msg, msg.reason || msg.status);
          break;
        case "reconcile":
          void reconcileDesiredSessions();
          break;
        case "exiting-host":
          console.info(
            `Native host is exiting: '${msg.reason ?? "<unknown>"}'`
          );
          break;
        default:
          break;
      }
      return;
    }

    if (msg?.action === "sendNativeChat") {
      void routeSendNativeChat(msg);
    }
  });
  port.onDisconnect.addListener(() => {
    const lastError = chrome.runtime.lastError?.message ?? "";
    lastNativeError = lastError;

    console.error(
      "[ATTACH-DEBUG] Native port DISCONNECTED. lastError:",
      lastError || "(none)",
      "| connected for",
      Date.now() - lastNativeConnectedAt,
      "ms"
    );

    port = null;
    nativeSupportsV2 = false;
    void markSessionLost({}, "native-disconnected");
    scheduleNativeReconnect(
      /forbidden|not found|specified native messaging host/i.test(lastError)
        ? "configuration"
        : "disconnect"
    );
  });
}

// disconnect from port
function disconnectPort() {
  if (port) {
    port.disconnect();
    port = null;
  }
}

async function reconcileDesiredSessions() {
  const sessions = await sessionStore.all();
  const tabs = await chrome.tabs
    .query({ url: "*://*.twitch.tv/*" })
    .catch(() => []);
  const liveTabIds = new Set(tabs.map((tab) => tab.id));
  for (const session of Object.values(sessions)) {
    if (!session.desired) continue;
    if (!liveTabIds.has(session.tabId)) {
      await sessionStore.remove(session.sessionId);
      continue;
    }
    // A service-worker restart invalidates the in-memory port and any pending
    // acknowledgement. Reveal first; a fresh matching ack may hide it again.
    if (workerRecoveryPending && session.attached) {
      session.attached = false;
      await sessionStore.put(session);
      await sendOverlayState(session, "revealed", "worker-recovery");
    }
    if (session.leaseExpiresAt && session.leaseExpiresAt <= Date.now()) {
      session.attached = false;
      session.leaseExpiresAt = 0;
      await sessionStore.put(session);
      await sendOverlayState(session, "revealed", "lease-expired");
    } else if (
      nativeSupportsV2 &&
      session.attached &&
      session.leaseExpiresAt &&
      session.leaseExpiresAt - Date.now() < NATIVE_LEASE_MS / 2
    ) {
      forwardNativeMessage({
        action: "leaseRenew",
        protocolVersion: 2,
        sessionId: session.sessionId,
        browserWindowId: Number(session.windowId),
        tabId: session.tabId,
        generation: session.generation,
        name: session.channel,
        leaseExpiresAt: Date.now() + NATIVE_LEASE_MS,
      });
    }
    try {
      await chrome.tabs.sendMessage(session.tabId, {
        action: "requestChatRect",
        sessionId: session.sessionId,
        generation: session.generation,
      });
    } catch {
      // The content script may still be loading; onTabSelected will retry.
    }
  }
  workerRecoveryPending = false;
  if (!port && Date.now() >= nativeRetryAt) getPort();
}

if (chrome.alarms?.onAlarm?.addListener) {
  chrome.alarms.onAlarm.addListener((alarm) => {
    if (alarm.name === nativeRetryAlarmName) {
      nativeRetryTimer = null;
      if (Date.now() >= nativeRetryAt) connectPort(true);
    }
    if (alarm.name === "chatterino-reconcile") {
      void reconcileDesiredSessions();
    }
  });
  try {
    chrome.alarms.create("chatterino-reconcile", { periodInMinutes: 1 });
  } catch {
    // Firefox/older test harnesses may not expose alarms.
  }
}

if (chrome.runtime?.onStartup?.addListener) {
  chrome.runtime.onStartup.addListener(() => {
    workerRecoveryPending = true;
    void rehydrateRecoveryState().then(reconcileDesiredSessions);
  });
}
void rehydrateRecoveryState().then(reconcileDesiredSessions);

// tab activated
chrome.tabs.onActivated.addListener(async (activeInfo) => {
  if (activeInfo.windowId !== undefined) invalidateWindow(activeInfo.windowId);
  const tab = await chrome.tabs.get(activeInfo.tabId).catch(() => null);
  if (!tab?.active) return;

  // `tab.url` is empty whenever we lack host permission for the activated
  // tab's origin, which is the normal case for every non-Twitch page. Bailing
  // out here used to skip the detach entirely, leaving the native overlay
  // drawn on top of the newly activated tab. An unreadable URL is itself proof
  // that this is not a Twitch channel, so fall through to onTabSelected and
  // let it detach.
  await onTabSelected(tab.url || tab.pendingUrl || "", tab);
});


// url changed
chrome.tabs.onUpdated.addListener(async (tabId, changeInfo, tab) => {
  if (!tab.active) return;
  if (changeInfo.url || changeInfo.status === "loading") {
    await tryDetach(tab.windowId);
  }
  await onTabSelected(tab.url, tab);
});

// tab detached
chrome.tabs.onDetached.addListener(async (tabId, detachInfo) => {
  await tryDetach(detachInfo.oldWindowId);
});

// tab closed
chrome.windows.onRemoved.addListener(async (windowId) => {
  await tryDetach(windowId);
});

// window selected
chrome.windows.onFocusChanged.addListener(async (windowId) => {
  if (windowId == -1) return;

  const window = await safeGetWindow(windowId);
  if (!window) return;

  // this returns all tabs when the query fails
  const tabs = await chrome.tabs.query({
    windowId: windowId,
    active: true,
  });
  if (tabs.length === 1) {
    let tab = tabs[0];

    await onTabSelected(tab.url, tab);
  }
});

// attach or detach from tab
async function onTabSelected(url, tab, { startupReplay = false } = {}) {
  const channelName = matchChannelName(url);

  if (!channelName) {
    // detach from window
    await tryDetach(tab.windowId);
    return;
  }

  const sessions = await sessionsForWindow(tab.windowId);
  if (sessions.some((session) => session.desired &&
      (session.tabId !== tab.id || session.channel !== channelName))) {
    await tryDetach(tab.windowId);
  }

  // A chat-resized message can be emitted while the tab is in the background.
  // Those messages are intentionally ignored below, because attaching the
  // native window to an inactive tab would put it over the wrong browser
  // window. Ask the content script to measure again once this tab is active.
  // Without this handshake, a background-loaded Twitch tab stays detached
  // until a later page focus or mouse event happens to trigger a measurement.
  if (tab.id !== undefined) {
    if (startupReplay) {
      pendingStartupReplayTabs.set(
        tab.id,
        Date.now() + STARTUP_REPLAY_WINDOW_MS
      );
    }

    try {
      const request = { action: "requestChatRect" };
      if (nativeSupportsV2) {
        request.sessionId = (
          await ensureDesiredSession(
            tab.windowId,
            tab.id,
            matchChannelName(tab.url || "")
          )
        ).sessionId;
      }
      await chrome.tabs.sendMessage(tab.id, request);
    } catch (error) {
      if (startupReplay) {
        pendingStartupReplayTabs.delete(tab.id);
      }
      // The content script may not have loaded yet. Its initial measurement
      // will attach the chat once it does, so there is nothing to detach here.
    }
  }
}

async function getIntegrationHealth() {
  const [tab] = await chrome.tabs
    .query({ active: true, currentWindow: true, url: "*://*.twitch.tv/*" })
    .catch(() => []);
  let content = null;
  if (tab?.id) {
    try {
      content = await chrome.tabs.sendMessage(tab.id, {
        action: "getIntegrationHealth",
      });
    } catch (error) {
      content = null;
    }
  }
  return {
    native: {
      connected: Boolean(port),
      blocked: portConnectBlocked,
      state: nativeConnection.state,
      retryAt: nativeRetryAt,
      retryAttempt: nativeConnection.retryAttempt,
      lastError: lastNativeError,
      connectedAt: lastNativeConnectedAt,
      protocolVersion: nativeSupportsV2
        ? globalThis.ChatterinoProtocol.V2_VERSION
        : globalThis.ChatterinoProtocol.CURRENT_VERSION,
    },
    tab: tab
      ? { id: tab.id, channel: matchChannelName(tab.url || "") || "" }
      : null,
    content,
    transitions: transitionRing.snapshot(),
    checkedAt: Date.now(),
  };
}

function isFirefox() {
  // Only Firefox has browser.*
  return typeof browser !== "undefined";
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
    console.warn(
      "[Chatterino] Invalid native message:",
      error.message,
      message
    );
    return;
  }
  const port = getPort();
  if (port) {
    console.log('[ATTACH-DEBUG] Sending message to native host:', outbound);
    port.postMessage(outbound);
    return;
  }

  console.log('[ATTACH-DEBUG] No persistent port, using sendNativeMessage:', outbound);
  chrome.runtime.sendNativeMessage(appName, outbound, () => {
    if (chrome.runtime.lastError) {
      console.warn(
        "[Chatterino] Native messaging error:",
        chrome.runtime.lastError.message
      );
    }
  });
}

async function findTwitchTabForChannel(channelName) {
  const tabs = await chrome.tabs.query({ url: "*://*.twitch.tv/*" });
  if (channelName) {
    const normalized = channelName.toLowerCase();
    const matches = tabs.filter((tab) => {
      const name = matchChannelName(tab.url || "");
      return name && name.toLowerCase() === normalized;
    });
    if (matches.length === 1) {
      return matches[0];
    }
    // A legacy channel-only response is ambiguous when the same channel is
    // open in more than one tab. Never guess and duplicate a chat action.
    return null;
  }

  const highlighted = tabs.filter((tab) => tab.highlighted);
  return highlighted.length === 1 ? highlighted[0] : null;
}

async function routeSendNativeChat(message) {
  const isV2 = Number(message?.protocolVersion) >= 2;
  const boundedReason = (value, fallback) => {
    const reason = typeof value === "string" ? value.trim() : "";
    return reason.length <= 96 && /^[a-z0-9][a-z0-9._-]*$/i.test(reason)
      ? reason
      : fallback;
  };
  const postV2Result = (status, reason) => {
    forwardNativeMessage({
      action: "nativeChatResult",
      protocolVersion: 2,
      sessionId: message.sessionId,
      browserWindowId: message.browserWindowId,
      tabId: message.tabId,
      generation: message.generation,
      requestId: message.requestId,
      status,
      reason: boundedReason(reason, "unknown"),
    });
  };

  if (isV2) {
    if (
      typeof message.sessionId !== "string" ||
      typeof message.requestId !== "string" ||
      !/^[a-z0-9._:-]{1,128}$/i.test(message.requestId) ||
      !Number.isInteger(message.generation) ||
      message.generation < 0 ||
      !Number.isInteger(message.tabId) ||
      message.tabId < 0 ||
      (!Number.isInteger(message.browserWindowId) &&
        !(
          typeof message.browserWindowId === "string" &&
          /^(?:0|[1-9][0-9]*)$/.test(message.browserWindowId)
        ))
    ) {
      return;
    }
    // Version 2 commands are session-exact. In particular, never fall back to
    // a channel lookup: a channel may be open in more than one tab/window.
    const session = await sessionStore.get(message.sessionId);
    if (!session) {
      postV2Result("rejected", "unknown-session");
      return;
    }
    if (Number(message.generation) !== Number(session.generation)) {
      postV2Result("rejected", "stale-generation");
      return;
    }
    if (
      Number(message.browserWindowId) !== Number(session.windowId) ||
      Number(message.tabId) !== Number(session.tabId) ||
      String(message.channel || "").toLowerCase() !==
        String(session.channel || "").toLowerCase() ||
      session.attached !== true ||
      Number(session.leaseExpiresAt) <= Date.now()
    ) {
      postV2Result("rejected", "identity-mismatch");
      return;
    }

    const targetTab = await chrome.tabs.get(session.tabId).catch(() => null);
    if (
      !targetTab ||
      Number(targetTab.windowId) !== Number(session.windowId) ||
      String(matchChannelName(targetTab.url || "") || "").toLowerCase() !==
        String(session.channel || "").toLowerCase()
    ) {
      postV2Result("rejected", "tab-context-mismatch");
      return;
    }

    try {
      const response = await chrome.tabs.sendMessage(session.tabId, {
        action: "sendNativeChat",
        message: message.message,
        sessionId: session.sessionId,
        browserWindowId: session.windowId,
        tabId: session.tabId,
        generation: session.generation,
        requestId: message.requestId,
      });
      if (response?.ok === true) {
        postV2Result("accepted", "delivered");
      } else if (response?.uncertain === true) {
        postV2Result(
          "uncertain",
          boundedReason(response.reason, "delivery-uncertain")
        );
      } else {
        postV2Result(
          "rejected",
          boundedReason(response?.reason || response?.error, "rejected")
        );
      }
    } catch (error) {
      // A disconnected tab may have handled the message immediately before
      // Chrome reported the delivery error, so do not claim a definitive reject.
      console.warn("[Chatterino] Failed to route native chat send:", error);
      postV2Result("uncertain", "tab-send-exception");
    }
    return;
  }
  // v0/v1 attachment remains compatible, but channel-only chat delivery is a
  // state-changing command over an unauthenticated shared IPC queue. Reject it
  // instead of allowing a local queue writer to choose a Twitch target.
}

// receiving messages from content scripts and the popup
chrome.runtime.onMessage.addListener((message, sender, callback) => {
  if (!message || typeof message !== "object") return;
  if (
    message.action === "engagement" ||
    message.action === "prediction" ||
    message.action === "pin" ||
    message.action === "rewardPending" ||
    message.action === "rewardClear"
  ) {
    if (
      !sender.tab?.id ||
      !/^https:\/\/([a-z0-9-]+\.)*twitch\.tv\//i.test(sender.tab.url || "")
    ) {
      return;
    }
    forwardNativeMessage(message);
    return;
  }

  switch (message.type) {
    case "get-integration-health":
      getIntegrationHealth().then(callback);
      return true;
    case "get-setting":
      Settings.get(message.key).then(callback);
      return true;
    case "set-setting":
      (async () => {
        await Settings.set(message.key, message.value);

        if (message.key === "replaceTwitchChat" && message.value === false) {
          // Turning the integration off must fully undo it, not just tell the
          // desktop to let go. tryDetach also clears the stored sessions and
          // tells each overlay to reveal Twitch's own chat again; without that
          // the chat stayed hidden and the integration still looked loaded.
          const windowIds = new Set([
            ...(await AttachedWindows.detachAll()),
            ...Object.values(await sessionStore.all())
              .map((session) => Number(session.windowId))
              .filter((id) => Number.isInteger(id)),
          ]);
          for (const id of windowIds) {
            await tryDetach(id);
          }
        } else {
          for (const id of await AttachedWindows.detachAll()) {
            // they're already cleared
            await sendDetach(id);
          }
        }
        await updateBadge();
      })();
      break;
    case "get-os":
      chrome.runtime.getPlatformInfo((info) => callback(info.os));

      // We need to return true here so that `callback` will remain valid
      // after this function returns. This behavior is documented here:
      // https://developer.mozilla.org/en-US/docs/Mozilla/Add-ons/WebExtensions/API/runtime/onMessage
      return true;
      break;
    case "location-updated":
      safeGetWindow(sender.tab.windowId).then((window) => {
        if (!window?.focused) return;

        let data = {
          action: "select",
          type: "twitch",
          winId: sender.tab.windowId,
          version: 0,
          name: matchChannelName(sender.tab.url),
        };
        forwardNativeMessage(data);
      });
      break;
    case "chat-resized":
      const startupReplayDeadline = pendingStartupReplayTabs.get(
        sender.tab?.id
      );
      pendingStartupReplayTabs.delete(sender.tab?.id);
      const isStartupReplay =
        startupReplayDeadline !== undefined &&
        startupReplayDeadline >= Date.now();

      // Multi-selected tabs are highlighted too; only the visible tab may attach.
      if (!sender.tab?.active) return;
      const measurementRevision = windowRevision(sender.tab.windowId);
      const measurementChannel = matchChannelName(sender.tab.url);
      if (!measurementChannel) return;

      // is window focused
      safeGetWindow(sender.tab.windowId).then(async (window) => {
        if (!window) return;
        // Chatterino's attach contract is foreground-window based. Never send
        // a geometry replay while some other application owns focus: doing so
        // can bind the overlay to that unrelated HWND and break move/resize
        // tracking. Native readiness still requests fresh geometry; the next
        // focused browser event performs the attach.
        if (!window.focused) return;

        const dpr = message.dpr ?? 1;
        // devicePixelRatio combines both zoom and display scaling set in the
        // system. But the UI elements (sidebars) are unaffected by the zoom
        // level of the tab itself. So we need to separate the two.
        const scaleFactor = await calcDisplayScaleFactor(sender.tab.id, dpr);
        if (measurementRevision !== windowRevision(sender.tab.windowId) ||
            !(await isCurrentChannelTab(sender.tab.id, sender.tab.windowId, measurementChannel))) return;
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
        const session = await ensureDesiredSession(
          sender.tab.windowId,
          sender.tab.id,
          matchChannelName(sender.tab.url)
        );
        await sendOverlayState(session, "prepare");
        if (measurementRevision !== windowRevision(sender.tab.windowId)) return;
        await tryAttach(sender.tab.windowId, window.state == "fullscreen", {
          name: matchChannelName(sender.tab.url),
          size: size,
          browserWindowFocused: window.focused,
          startupReplay: isStartupReplay,
          tabId: sender.tab.id,
          sessionId: session.sessionId,
          generation: session.generation,
        }, measurementRevision);
      });
      break;
    case "detach":
      // A background Twitch tab may finish navigating after another tab won.
      chrome.tabs.get(sender.tab.id).then((tab) => {
        if (tab.active) return tryDetach(tab.windowId);
      }).catch(() => {});
      break;
  }
});

// attach chatterino to a chrome window
async function tryAttach(windowId, fullscreen, data, revision = windowRevision(windowId)) {
  data.action = "select";
  if (await Settings.get("replaceTwitchChat")) {
    if (fullscreen) {
      data.attach_fullscreen = true;
    } else {
      data.attach = true;
    }
  }
  data.type = "twitch";
  data.winId = "" + windowId;
  data.version = 0;

  if (nativeSupportsV2 && data.sessionId) {
    data.protocolVersion = 2;
    data.browserWindowId = Number(windowId);
    data.leaseExpiresAt = Date.now() + NATIVE_LEASE_MS;
    data.requestId =
      data.attachRequestId || `${Date.now()}-${nextAttachRequestId++}`;
  }

  const outboundData = { ...data };
  if (!(await isCurrentChannelTab(data.tabId, windowId, data.name)) ||
      revision !== windowRevision(windowId)) return;
  if (!nativeSupportsV2) {
    for (const field of [
      "sessionId",
      "generation",
      "tabId",
      "browserWindowId",
      "requestId",
      "protocolVersion",
      "leaseExpiresAt",
    ]) {
      delete outboundData[field];
    }
  }

  if (outboundData.attach || outboundData.attach_fullscreen) {
    postAttachUntilAcknowledged(windowId, outboundData, {
      sessionId: data.sessionId,
      tabId: data.tabId,
      generation: data.generation,
      revision,
    });
  } else {
    const nativePort = getPort();
    if (nativePort) nativePort.postMessage(outboundData);
  }
}

/**
 * Detach chatterino from a chrome window
 * @param {number} windowId
 */
async function tryDetach(windowId) {
  invalidateWindow(windowId);
  const sessions = (await sessionsForWindow(windowId)).filter(
    (session) => session.desired || session.attached
  );
  for (const session of sessions) {
    session.desired = false;
    session.attached = false;
    session.leaseExpiresAt = 0;
    await sessionStore.put(session);
    await sendOverlayState(session, "revealed", "detach");
  }

  // `AttachedWindows` is only written by acknowledgeAttachedWindow, which needs
  // an in-memory pending request. After a service-worker restart that bookkeeping
  // is gone while Chatterino is still drawing, so gating the native detach on it
  // left the overlay pinned over unrelated tabs. A live session is equally good
  // evidence that something is attached and must be torn down.
  const wasAttached = await AttachedWindows.isAttached(windowId);
  if (wasAttached || sessions.length > 0) {
    await sendDetach(windowId, sessions);
  }
  if (wasAttached) {
    await AttachedWindows.markDetatched(windowId);
  }
}


async function sendDetach(winID, knownSessions) {
  // tryDetach has already cleared its sessions by the time it calls here, so it
  // passes them in explicitly; other callers look them up.
  const sessions = knownSessions ?? (await sessionsForWindow(winID));
  const base = {
    action: "detach",
    version: 0,
    winId: winID.toString(),
  };
  if (!nativeSupportsV2 || sessions.length === 0) {
    forwardNativeMessage(base);
    return;
  }
  // One window can own several sessions (channel change, or a second Twitch
  // tab). Detaching only the first left the rest attached.
  for (const session of sessions) {
    forwardNativeMessage({
      ...base,
      protocolVersion: 2,
      sessionId: session.sessionId,
      browserWindowId: Number(session.windowId),
      tabId: session.tabId,
      generation: session.generation,
      // The desktop v2 parser requires a complete attachment identity
      // (non-empty channel included); without it every detach is rejected as
      // malformed-message and the overlay stays pinned.
      channel: session.channel,
    });
  }
}


async function routeNativeChatResult(message) {
  if (!message.sessionId) return;
  const session = await sessionStore.get(message.sessionId);
  if (!session || Number(message.generation) !== Number(session.generation)) {
    return;
  }
  try {
    await chrome.tabs.sendMessage(session.tabId, {
      action: "nativeChatResult",
      sessionId: session.sessionId,
      requestId: message.requestId,
      generation: session.generation,
      status: message.status,
      reason: message.reason,
    });
  } catch {
    // Navigation is reconciled by the desired session on the next wakeup.
  }
}

async function updateBadge() {
  chrome.action.setBadgeText({
    text: (await Settings.get("replaceTwitchChat")) ? "" : "off",
  });
}

function getPreviousTabs() {
  return chrome.storage.session
    .get("previousTabs")
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

  const tabs = await chrome.tabs.query({ url: "*://*.twitch.tv/*" });
  const currentTabs = new Set(
    tabs.map((t) => matchChannelName(t.url)).filter(Boolean)
  );
  if (compareTabs(previousTabs, currentTabs)) {
    return;
  }
  previousTabs = currentTabs;

  forwardNativeMessage({ action: "sync", twitchChannels: [...currentTabs] });

  await setPreviousTabs(previousTabs);
}
syncTabs();

chrome.tabs.onCreated.addListener(() => syncTabs());
chrome.tabs.onRemoved.addListener(() => syncTabs());
chrome.tabs.onUpdated.addListener((id, changeInfo) => {
  if ("url" in changeInfo) {
    syncTabs();
  }
});
