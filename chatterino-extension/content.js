(function () {
  "use strict";

  const BANNER_SELECTORS = [
    '[data-test-selector="community-prediction-banner"]',
    ".prediction-banner",
    ".gamba-prediction-status-banner",
  ];

  const POLL_BANNER_SELECTORS = [
    '[data-test-selector="community-poll-banner"]',
    '[data-a-target="community-poll-banner"]',
    '[data-test-selector*="poll-banner"]',
    ".community-poll-banner",
  ];

  const VOTING_BANNER_SELECTOR = [
    ...BANNER_SELECTORS,
    ...POLL_BANNER_SELECTORS,
  ].join(",");
  const VOTING_CONTROL_SELECTOR = [
    "button",
    "input",
    "select",
    '[role="button"]',
    '[role="radio"]',
    '[role="option"]',
  ].join(",");

  const PINNED_SELECTORS = [
    '[data-a-target="chat-pinned-message"]',
    ".pinned-chat__highlight-card",
    ".pinned-chat__container",
    ".pinned-chat-list-item",
    ".community-highlight-stack__card",
    ".community-highlight-stack",
    ".community-highlight",
  ];

  // Twitch community highlights include gift subs/resubs — those already arrive
  // in Chatterino via IRC USERNOTICE and must not fill the announcement banner.
  const SUBSCRIPTION_HIGHLIGHT_PATTERNS = [
    /\bgifted\s+(?:\d+\s+months?\s+of\s+(?:a\s+)?)?tier\s+\d/i,
    /\bgifted\s+a\s+tier\s+\d/i,
    /\bgifted\s+\d+\s+tier\s+\d/i,
    /\bis\s+gifting\s+\d+/i,
    /\banonymous\s+user\s+gifted/i,
    /\ban\s+anonymous\s+user\s+gifted/i,
    /\bjust\s+subscribed\b/i,
    /\bhas\s+subscribed\b/i,
    /\bsubscribed\s+at\s+tier\b/i,
    /\bsubscribed\s+with\s+a\s+tier\b/i,
    /\bsubscribed\s+for\s+\d+\s+months/i,
    /\bresubscribed\b/i,
    /\bgift\s+sub\b/i,
    /\bprime\s+sub\b/i,
    /\bcommunity\s+gift\b/i,
    /\bsub\s+mystery\s+gift\b/i,
  ];

  const DOM_FAIL_THRESHOLD_MS = 2000;
  const SAFETY_POLL_MS = 2000;

  const POINTS_SELECTORS = [
    '[data-test-selector="community-points-summary"]',
    '[data-a-target="community-points-summary"]',
    '[class*="community-points-summary"]',
    'button[aria-label*="Channel Points" i]',
    'button[aria-label*="Bits and Points" i]',
    'button[aria-label*="Points Balance" i]',
    'button[aria-label*="Power-ups" i]',
    'button[aria-label*="Rewards" i]',
    '[data-a-target="chat-rewards-button"]',
    '[data-test-selector="chat-rewards-button"]',
  ];

  const BADGE_SELECTORS = [
    ".chat-input__badge-carousel",
    '[class*="chat-input"] [data-a-target="chat-badge-carousel"]',
    '[data-a-target="chat-badge-carousel-badge-icon"]',
  ];

  const CLAIM_BONUS_SELECTORS = [
    'button[aria-label="Claim Bonus"]',
    'button[aria-label*="Claim Bonus" i]',
  ];

  let autoClaimEnabled = true;
  let lastPredictionTitle = "";
  let isMinimized = false;
  let lastPinFingerprint = "";
  let pollIntervalId = null;
  let syncIntervalId = null;
  let currentChannel = "";
  let gqlState = null;
  const activityStore = new window.ChatterinoActivity.ActivityStore();
  let companionActive = document.documentElement.classList.contains(
    "chatterino-companion-active"
  );
  let domPointsLastSeen = 0;
  let domBadgesLastSeen = 0;
  let domPredictionLastSeen = 0;
  let activeBanner = null;
  let activePredictionSource = null;
  let activePollSource = null;
  let syncScheduled = false;
  let syncPending = false;
  let syncTimer = null;
  let lastSyncAt = 0;
  let syncInProgress = false;
  let observerAttached = false;
  const MIN_SYNC_INTERVAL_MS = 250;
  // Set once the extension context is invalidated (extension reloaded or
  // updated). The orphaned script must stop all of its timers and observers:
  // every chrome.* call would throw from then on.
  let contextTornDown = false;

  // chrome.runtime.sendMessage throws synchronously once the context is
  // invalidated and returns a rejecting promise when the service worker has
  // no receiver. Neither may escape into the sync loop or a Twitch event.
  function safeSendMessage(message) {
    if (contextTornDown || isContextInvalidated()) {
      return false;
    }
    try {
      const result = chrome.runtime.sendMessage(message);
      result?.catch?.(() => {});
      return true;
    } catch (_err) {
      isContextInvalidated();
      return false;
    }
  }

  function recordObserverCallback(startedAt) {
    const metrics = window.ChatterinoPerformanceMetrics;
    if (!metrics?.recordObserverCallback) return;
    const now = window.performance?.now?.() ?? Date.now();
    metrics.recordObserverCallback(Math.max(0, now - startedAt));
  }

  function measureObserverWork(callback) {
    const startedAt = window.performance?.now?.() ?? Date.now();
    try {
      return callback();
    } finally {
      recordObserverCallback(startedAt);
    }
  }

  let rewardPendingActive = false;

  const NATIVE_CHAT_INPUT_SELECTORS = [
    '[data-a-target="chat-input"]',
    'textarea[data-a-target="chat-input"]',
    '[data-test-selector="chat-input"]',
    'div[data-a-target="chat-input"] [contenteditable="true"]',
    '[contenteditable="true"][data-slate-editor="true"]',
    'textarea[placeholder*="chat" i]',
  ];

  const NATIVE_CHAT_SEND_SELECTORS = [
    '[data-a-target="chat-send-button"]',
    'button[aria-label="Send Message"]',
    'button[aria-label*="Send" i]',
  ];

  function setRewardPendingUi(active) {
    rewardPendingActive = active;
    document.documentElement.classList.toggle(
      "chatterino-reward-pending",
      active
    );
    if (active) {
      unrestoreNativeChatInput();
    }
  }

  function unrestoreNativeChatInput() {
    for (const selector of NATIVE_CHAT_INPUT_SELECTORS) {
      const input = document.querySelector(selector);
      if (!input) {
        continue;
      }
      let node = input;
      while (node && node !== document.documentElement) {
        node.classList.remove("chatterino-cc-restored");
        node = node.parentElement;
      }
    }
  }

  function findNativeChatInput() {
    for (const selector of NATIVE_CHAT_INPUT_SELECTORS) {
      const el = document.querySelector(selector);
      if (el) {
        return el;
      }
    }
    const footer = document.querySelector('[class*="chat-input"]');
    if (footer) {
      return footer.querySelector('[contenteditable="true"], textarea');
    }
    return null;
  }

  function findNativeChatSendButton() {
    for (const selector of NATIVE_CHAT_SEND_SELECTORS) {
      const el = document.querySelector(selector);
      if (el && !el.disabled) {
        return el;
      }
    }
    return null;
  }

  function setNativeInputValue(input, text) {
    if (!input) {
      return false;
    }
    if (
      input instanceof HTMLTextAreaElement ||
      input instanceof HTMLInputElement
    ) {
      const proto =
        Object.getOwnPropertyDescriptor(
          window.HTMLTextAreaElement.prototype,
          "value"
        ) ||
        Object.getOwnPropertyDescriptor(
          window.HTMLInputElement.prototype,
          "value"
        );
      if (proto?.set) {
        proto.set.call(input, text);
      } else {
        input.value = text;
      }
      input.dispatchEvent(new Event("input", { bubbles: true }));
      input.dispatchEvent(new Event("change", { bubbles: true }));
      return true;
    }
    if (input.isContentEditable) {
      input.focus();
      input.textContent = text;
      input.dispatchEvent(
        new InputEvent("input", {
          bubbles: true,
          data: text,
          inputType: "insertText",
        })
      );
      return true;
    }
    return false;
  }

  function sendNativeChatMessage(message) {
    const text = String(message || "").trim();
    if (!text) {
      return { ok: false, error: "empty message" };
    }
    unrestoreNativeChatInput();
    const input = findNativeChatInput();
    if (!input) {
      return { ok: false, error: "chat input not found" };
    }
    input.focus();
    if (!setNativeInputValue(input, text)) {
      return { ok: false, error: "failed to set input value" };
    }
    const sendBtn = findNativeChatSendButton();
    if (sendBtn) {
      sendBtn.click();
      return { ok: true };
    }
    input.dispatchEvent(
      new KeyboardEvent("keydown", {
        key: "Enter",
        code: "Enter",
        bubbles: true,
        cancelable: true,
      })
    );
    input.dispatchEvent(
      new KeyboardEvent("keypress", {
        key: "Enter",
        code: "Enter",
        bubbles: true,
        cancelable: true,
      })
    );
    return { ok: true };
  }

  function forwardRewardPending(detail) {
    const channelName = getTwitchChannelName();
    if (!channelName) {
      return;
    }
    setRewardPendingUi(true);
    safeSendMessage({
      action: "rewardPending",
      channel: channelName,
      rewardId: detail?.rewardId || "",
      title: detail?.title || "",
      prompt: detail?.prompt || "",
    });
  }

  function forwardRewardClear(reason) {
    setRewardPendingUi(false);
    const channelName = getTwitchChannelName();
    if (!channelName) {
      return;
    }
    safeSendMessage({
      action: "rewardClear",
      channel: channelName,
      reason: reason || "",
    });
  }

  const REWARD_INPUT_BANNER_SELECTORS = [
    '[data-test-selector="chat-input-reward-banner"]',
    '[data-a-target="chat-input-reward-banner"]',
    '[class*="chat-input__reward-banner"]',
    '[class*="chat-input-reward-banner"]',
    '[class*="chat-input__reward"]',
    '[class*="reward-banner"]',
    '[data-test-selector*="chat-input-reward"]',
    '[data-a-target*="chat-input-reward"]',
    'button[aria-label*="Cancel Reward" i]',
    'button[aria-label*="Cancel redemption" i]',
    '[data-a-target="cancel-reward-button"]',
    '[data-test-selector="cancel-reward-button"]',
  ];

  function checkNativeRewardPending() {
    let activeBannerEl = null;
    for (const sel of REWARD_INPUT_BANNER_SELECTORS) {
      const el = document.querySelector(sel);
      if (el && !el.closest("#chatterino-toolbar-portal")) {
        activeBannerEl = el;
        break;
      }
    }

    if (activeBannerEl) {
      if (!rewardPendingActive) {
        const title =
          activeBannerEl.querySelector('[data-a-target*="title" i]')?.textContent ||
          activeBannerEl.querySelector('strong, b, [class*="title" i]')?.textContent ||
          activeBannerEl.textContent ||
          "Reward";
        const prompt =
          findNativeChatInput()?.getAttribute("placeholder") || "";
        forwardRewardPending({
          rewardId: activeBannerEl.getAttribute("data-reward-id") || "",
          title: title.trim().slice(0, 80),
          prompt: prompt.trim().slice(0, 160),
        });
      }
    } else if (rewardPendingActive) {
      const hasPendingRewardIndicator =
        document.querySelector('[class*="reward-banner"]') !== null;
      if (!hasPendingRewardIndicator) {
        forwardRewardClear("banner-removed");
      }
    }
  }

  function scheduleSync() {
    if (contextTornDown) {
      return;
    }
    syncPending = true;
    if (syncTimer !== null) {
      return;
    }
    const flush = () => {
      syncTimer = null;
      if (!syncPending) {
        return;
      }
      const delay = Math.max(
        0,
        MIN_SYNC_INTERVAL_MS - (Date.now() - lastSyncAt)
      );
      if (delay > 0) {
        syncTimer = setTimeout(flush, delay);
        return;
      }
      syncPending = false;
      lastSyncAt = Date.now();
      runSync();
    };
    syncTimer = setTimeout(flush, 0);
  }

  const findTargetButton = () => {
    const selectors = [
      '[data-a-target="follow-button"]',
      '[data-a-target="unfollow-button"]',
      'button[aria-label*="Follow" i]',
      'button[aria-label*="Following" i]',
      'button[aria-label*="Unfollow" i]',
      'button[aria-label*="Suivre" i]',
      'button[aria-label*="Suivi" i]',
      'button[aria-label*="Se désabonner" i]',
      '[data-a-target="subscribe-button"]',
      'button[aria-label*="Subscribe" i]',
      'button[aria-label*="S\'abonner" i]',
      '[data-a-target="stream-info-card"] button',
      ".stream-info button",
    ];
    for (const selector of selectors) {
      const btn = document.querySelector(selector);
      if (btn) {
        return btn;
      }
    }
    return null;
  };

  // The native points element must stay in its original spot inside the React
  // tree — moving it out of the React root silently breaks Twitch's delegated
  // click handlers. We render our own replica button in the toolbar and forward
  // clicks to the (possibly visually hidden) native button instead.
  function findPointsSummary() {
    for (const selector of POINTS_SELECTORS) {
      for (const el of document.querySelectorAll(selector)) {
        if (el.closest("#chatterino-toolbar-portal")) {
          continue;
        }
        return (
          el.closest('[data-test-selector="community-points-summary"]') ||
          el.closest('[class*="community-points"]') ||
          el
        );
      }
    }
    return null;
  }

  function findNativePointsButton() {
    const summary = findPointsSummary();
    if (summary) {
      const btn = summary.querySelector("button") || summary.closest("button");
      if (btn) {
        return btn;
      }
    }
    const fallbackSelectors = [
      'button[aria-label*="Channel Points" i]',
      'button[aria-label*="Bits and Points" i]',
      'button[aria-label*="Points Balance" i]',
      'button[aria-label*="Power-ups" i]',
      'button[aria-label*="Rewards" i]',
      '[data-a-target="chat-rewards-button"]',
      '[data-test-selector="chat-rewards-button"]',
    ];
    for (const selector of fallbackSelectors) {
      for (const btn of document.querySelectorAll(selector)) {
        if (!btn.closest("#chatterino-toolbar-portal")) {
          return btn;
        }
      }
    }
    return null;
  }

  function handlePointsReplicaClick(replica) {
    const nativeBtn = findNativePointsButton();
    if (!nativeBtn) {
      return;
    }
    intentionalRewardDialogOpen = true;
    // One watcher at a time: each click used to stack another 250 ms interval
    // (each living up to 120 s), and an older one could clear the flag for a
    // newer click.
    stopIntentionalRewardWatcher();
    intentionalRewardWatchId = setInterval(() => {
      if (!findNativeRewardDialog()) {
        intentionalRewardDialogOpen = false;
        stopIntentionalRewardWatcher();
      }
    }, 250);
    intentionalRewardWatchTimeout = setTimeout(
      stopIntentionalRewardWatcher,
      120000
    );
    // Programmatic click bubbles through the React root, so Twitch's own
    // handler opens/closes the reward center even if the button is hidden.
    nativeBtn.click();
    const attemptPosition = (triesLeft) => {
      if (positionNativeRewardDialog(replica)) {
        startRewardDialogWatcher(replica);
        return;
      }
      // While a prediction is accepting entries Twitch answers the points
      // button with the bet prompt instead of the reward centre.
      if (positionNativePredictionDialog(replica)) {
        startPredictionDialogWatcher(replica);
        return;
      }
      if (triesLeft > 0) {
        requestAnimationFrame(() => attemptPosition(triesLeft - 1));
      }
    };
    requestAnimationFrame(() => attemptPosition(120));
  }

  function ensurePointsReplica() {
    const summary = findPointsSummary();
    if (!summary) {
      return document.getElementById("chatterino-points-replica");
    }

    let replica = document.getElementById("chatterino-points-replica");
    if (!replica) {
      replica = document.createElement("div");
      replica.id = "chatterino-points-replica";
      // Forward real user clicks to the native (hidden) button so Twitch's
      // own React handler opens the reward center.
      replica.addEventListener(
        "click",
        (e) => {
          e.preventDefault();
          e.stopPropagation();
          handlePointsReplicaClick(replica);
        },
        true
      );
    }

    // Mirror the native element 1:1 so Twitch's own CSS renders it
    // identically — same icon, same number, same hover styles.
    const sourceHtml = summary.outerHTML;
    if (replica.__ccSourceHtml !== sourceHtml) {
      replica.__ccSourceHtml = sourceHtml;
      replica.className = summary.parentElement?.className || "";
      replica.replaceChildren(summary.cloneNode(true));
      replica
        .querySelectorAll("[id]")
        .forEach((el) => el.removeAttribute("id"));
      replica.querySelectorAll("button").forEach((btn) => {
        btn.setAttribute("tabindex", "-1");
      });
      // Strip transient "+N" point-gain animations so they don't freeze
      // inside the clone.
      replica.querySelectorAll("span, div").forEach((el) => {
        if (
          /^\+[\d,.]+[KMB]?$/i.test(el.textContent.trim()) &&
          !el.querySelector("button")
        ) {
          el.remove();
        }
      });
    }

    mountInSlot(replica, 3);
    document.documentElement.classList.add("chatterino-points-replica-active");
    return replica;
  }

  function removePointsReplica() {
    document.getElementById("chatterino-points-replica")?.remove();
    document.documentElement.classList.remove(
      "chatterino-points-replica-active"
    );
  }

  // Chat badge carousel — same replica pattern as channel points. The native
  // element stays in the React tree; we mirror it in the toolbar and forward
  // clicks so Twitch opens the Chat Identity popover.
  function findChatInputRoot() {
    return (
      document
        .querySelector('[data-a-target="chat-input"]')
        ?.closest('[class*="chat-input"]') ||
      document
        .querySelector('[class*="chat-input__textarea"]')
        ?.closest('[class*="chat-input"]') ||
      document.querySelector(".chat-input") ||
      document.querySelector('[class*="chat-input"]')
    );
  }

  function findChatBadgeCarousel() {
    const searchRoot = findChatInputRoot() || document;
    for (const selector of BADGE_SELECTORS) {
      for (const el of searchRoot.querySelectorAll(selector)) {
        if (el.closest("#chatterino-toolbar-portal")) {
          continue;
        }
        return (
          el.closest(".chat-input__badge-carousel") ||
          el.closest('[data-a-target="chat-badge-carousel"]') ||
          el
        );
      }
    }
    return null;
  }

  function findNativeChatBadgeButton() {
    const carousel = findChatBadgeCarousel();
    if (!carousel) {
      return null;
    }
    const btn =
      carousel.querySelector(
        '[data-a-target="chat-badge-carousel-badge-icon"]'
      ) ||
      carousel.querySelector('button[aria-label="ChatBadgeCarousel"]') ||
      carousel.querySelector("button");
    if (btn && !btn.closest("#chatterino-toolbar-portal")) {
      return btn;
    }
    return null;
  }

  const BADGE_REPLICA_VERSION = "3";

  const BADGE_REPLICA_MARKUP = `
<button type="button" class="chatterino-badge-replica-btn" aria-hidden="true" tabindex="-1">
  <span class="chatterino-badge-replica-btn__icon" aria-hidden="true">
    <svg width="32" height="32" viewBox="0 0 24 24" focusable="false" role="presentation">
      <path fill="currentColor" d="M12 1.25 3.5 5v7c0 5.45 3.85 10.35 8.5 12 4.65-1.65 8.5-6.55 8.5-12V5L12 1.25z"/>
      <path fill="#efeff1" d="M12 6.5l2.1 4.25 4.7.68-3.4 3.31.8 4.68L12 17.2l-4.2 2.42.8-4.68-3.4-3.31 4.7-.68L12 6.5z"/>
    </svg>
  </span>
</button>`;

  function applyBadgeReplicaMarkup(replica) {
    replica.innerHTML = BADGE_REPLICA_MARKUP;
    replica.dataset.ccBadgeVersion = BADGE_REPLICA_VERSION;
  }

  function buildBadgeReplicaShell() {
    const replica = document.createElement("div");
    replica.id = "chatterino-badge-replica";
    replica.setAttribute("role", "button");
    replica.setAttribute("tabindex", "0");
    replica.setAttribute("aria-label", "Chat Identity");
    replica.setAttribute("title", "Chat Identity");
    applyBadgeReplicaMarkup(replica);

    const handleActivate = (e) => {
      e.preventDefault();
      e.stopPropagation();
      handleBadgeReplicaClick(replica);
    };

    replica.addEventListener("click", handleActivate, true);
    replica.addEventListener("keydown", (e) => {
      if (e.key === "Enter" || e.key === " ") {
        handleActivate(e);
      }
    });
    return replica;
  }

  let identityDialogWatchId = null;
  let identityDialogWatchTimeout = null;

  function findNativeChatIdentityDialog() {
    return (
      document.querySelector(
        'div[role="dialog"][aria-label="Chat Identity"]'
      ) ||
      document.querySelector('div[role="dialog"]:has(.chat-identity-menu)') ||
      document.querySelector(".chat-identity-menu")?.closest('[role="dialog"]')
    );
  }

  function ensureChatIdentityDialogScrollable(popover) {
    const scrollSelectors = [
      '[class*="scrollable"]',
      '[class*="Scrollable"]',
      ".chat-identity-menu",
      ".chat-identity-menu__content",
      ".simplebar-content-wrapper",
      ".simplebar-scrollable-node",
      "[data-simplebar-scrollable]",
    ];
    for (const selector of scrollSelectors) {
      popover.querySelectorAll(selector).forEach((el) => {
        el.style.setProperty("pointer-events", "auto", "important");
        el.style.setProperty("visibility", "visible", "important");
        el.style.setProperty("touch-action", "pan-y", "important");
        const overflowY = getComputedStyle(el).overflowY;
        if (overflowY === "hidden" || overflowY === "clip") {
          el.style.setProperty("overflow-y", "auto", "important");
        }
      });
    }
  }

  function markChatIdentityDialog(popover) {
    popover.classList.add("chatterino-native-chat-identity-dialog");
    const settingsRoot =
      popover.querySelector(".chat-settings__popover") ||
      popover.closest(".chat-settings__popover");
    if (settingsRoot && settingsRoot !== popover) {
      settingsRoot.classList.add("chatterino-native-chat-identity-dialog");
    }
    ensureChatIdentityDialogScrollable(popover);
    if (settingsRoot && settingsRoot !== popover) {
      ensureChatIdentityDialogScrollable(settingsRoot);
    }
  }

  function unmarkChatIdentityDialog(popover) {
    if (!popover) {
      return;
    }
    popover.classList.remove("chatterino-native-chat-identity-dialog");
    popover
      .querySelector(".chat-settings__popover")
      ?.classList.remove("chatterino-native-chat-identity-dialog");
  }

  function positionNativeChatIdentityDialog(anchorEl) {
    const popover = findNativeChatIdentityDialog();
    if (!popover || !anchorEl) {
      return false;
    }

    markChatIdentityDialog(popover);
    popover.style.setProperty("z-index", "2147483647", "important");

    const anchorRect = anchorEl.getBoundingClientRect();
    const popRect = popover.getBoundingClientRect();
    if (popRect.width === 0 || popRect.height === 0) {
      return false;
    }

    const prev = popover.__ccTranslate || { x: 0, y: 0 };
    const baseLeft = popRect.left - prev.x;
    const baseTop = popRect.top - prev.y;

    const desiredLeft = Math.max(
      8,
      Math.min(anchorRect.left, window.innerWidth - popRect.width - 8)
    );
    let desiredTop = anchorRect.bottom + 8;
    if (desiredTop + popRect.height > window.innerHeight - 8) {
      desiredTop = anchorRect.top - popRect.height - 8;
    }
    desiredTop = Math.max(
      8,
      Math.min(desiredTop, window.innerHeight - popRect.height - 8)
    );

    const dx = Math.round(desiredLeft - baseLeft);
    const dy = Math.round(desiredTop - baseTop);
    if (dx !== prev.x || dy !== prev.y) {
      popover.__ccTranslate = { x: dx, y: dy };
      popover.style.setProperty(
        "transform",
        `translate(${dx}px, ${dy}px)`,
        "important"
      );
    }
    return true;
  }

  // The 60 s cap timeout is tracked with the interval: an untracked cap from
  // an earlier opening used to stop a watcher started for a later opening.
  function stopChatIdentityDialogWatcher() {
    if (identityDialogWatchId) {
      clearInterval(identityDialogWatchId);
      identityDialogWatchId = null;
    }
    if (identityDialogWatchTimeout) {
      clearTimeout(identityDialogWatchTimeout);
      identityDialogWatchTimeout = null;
    }
  }

  function startChatIdentityDialogWatcher(anchorEl) {
    stopChatIdentityDialogWatcher();
    identityDialogWatchId = setInterval(() => {
      const dialog = findNativeChatIdentityDialog();
      if (!dialog) {
        stopChatIdentityDialogWatcher();
        return;
      }
      positionNativeChatIdentityDialog(anchorEl);
    }, 100);

    identityDialogWatchTimeout = setTimeout(
      stopChatIdentityDialogWatcher,
      60000
    );
  }

  function handleBadgeReplicaClick(replica) {
    const nativeBtn = findNativeChatBadgeButton();
    if (!nativeBtn) {
      return;
    }
    nativeBtn.click();
    const attemptPosition = (triesLeft) => {
      if (positionNativeChatIdentityDialog(replica)) {
        startChatIdentityDialogWatcher(replica);
        return;
      }
      if (triesLeft > 0) {
        requestAnimationFrame(() => attemptPosition(triesLeft - 1));
      }
    };
    requestAnimationFrame(() => attemptPosition(20));
  }

  function ensureBadgeReplica() {
    const nativeBtn = findNativeChatBadgeButton();
    let replica = document.getElementById("chatterino-badge-replica");

    if (!nativeBtn && !replica) {
      return null;
    }

    if (!replica) {
      replica = buildBadgeReplicaShell();
    } else if (replica.dataset.ccBadgeVersion !== BADGE_REPLICA_VERSION) {
      applyBadgeReplicaMarkup(replica);
    }

    mountInSlot(replica, 2);
    document.documentElement.classList.add("chatterino-badge-replica-active");
    return replica;
  }

  function removeBadgeReplica() {
    document.getElementById("chatterino-badge-replica")?.remove();
    document.documentElement.classList.remove(
      "chatterino-badge-replica-active"
    );
    stopChatIdentityDialogWatcher();
    unmarkChatIdentityDialog(findNativeChatIdentityDialog());
  }

  // When the Chatterino integration wipes the chat, the native summary dies
  // and the clone can no longer mirror it. Keep the displayed balance fresh
  // from the GQL stream instead. The points balance is the last numeric text
  // node in the cloned markup (the first is the bits balance).
  function updateReplicaBalanceFromGql() {
    const replica = document.getElementById("chatterino-points-replica");
    const balance = gqlState?.channelPoints?.balance;
    if (!replica || balance == null || balance === "") {
      return;
    }
    const walker = document.createTreeWalker(replica, NodeFilter.SHOW_TEXT);
    let lastNumeric = null;
    let node;
    while ((node = walker.nextNode())) {
      if (/^[\d.,]+[KMB]?$/i.test(node.textContent.trim())) {
        lastNumeric = node;
      }
    }
    if (lastNumeric && lastNumeric.textContent.trim() !== String(balance)) {
      lastNumeric.textContent = String(balance);
    }
  }

  let rewardDialogWatchId = null;
  let rewardDialogWatchTimeout = null;
  let predictionDialogWatchId = null;
  let predictionDialogWatchTimeout = null;
  let intentionalRewardWatchId = null;
  let intentionalRewardWatchTimeout = null;
  let intentionalRewardDialogOpen = false;

  function stopIntentionalRewardWatcher() {
    clearInterval(intentionalRewardWatchId);
    clearTimeout(intentionalRewardWatchTimeout);
    intentionalRewardWatchId = null;
    intentionalRewardWatchTimeout = null;
  }
  let unintendedDismissTimer = null;
  let lastSuccessfulClaimAt = 0;
  let claimAttemptTimer = null;
  let claimBootstrapTimers = [];
  const AUTO_CLAIM_MIN_INTERVAL_MS = 30000;
  const CLAIM_BOOTSTRAP_DELAYS_MS = [0, 300, 750, 1500, 3000, 6000, 12000];

  function isOverlayOrBackdrop(el) {
    if (!el) {
      return false;
    }
    const className = String(el.className?.baseVal ?? el.className ?? "");
    if (
      /\b(?:ReactModal__Overlay|tw-backdrop|modal-backdrop|overlay|backdrop|scrim|mask)\b/i.test(
        className
      ) ||
      className.includes("Overlay") ||
      className.includes("backdrop")
    ) {
      return true;
    }
    if (typeof el.getAttribute === "function") {
      const target = String(el.getAttribute("data-a-target") || "");
      if (target.includes("backdrop") || target.includes("overlay")) {
        return true;
      }
    }
    return false;
  }

  function unmarkRewardDialog(el) {
    if (!el) {
      return;
    }
    el.classList.remove("chatterino-native-reward-dialog");
    el.style.removeProperty("z-index");
    if (el.__ccTranslate) {
      el.style.removeProperty("transform");
      delete el.__ccTranslate;
    }
  }

  function unmarkPredictionDialog(el) {
    if (!el) {
      return;
    }
    el.classList.remove("chatterino-native-prediction-dialog");
    el.style.removeProperty("z-index");
    if (el.__ccTranslate) {
      el.style.removeProperty("transform");
      delete el.__ccTranslate;
    }
  }

  function isNativeRewardDialogCandidate(dialog) {
    if (!dialog || !dialog.isConnected) {
      return false;
    }
    if (dialog.closest("#chatterino-toolbar-portal")) {
      return false;
    }
    if (isOverlayOrBackdrop(dialog)) {
      return false;
    }
    if (window.ChatterinoVotingUi?.isVideoPlayerUi?.(dialog)) {
      return false;
    }
    if (
      dialog.tagName === "BUTTON" ||
      dialog.matches?.(
        'button, [role="button"], [data-a-target="chat-rewards-button"], [data-test-selector="chat-rewards-button"]'
      )
    ) {
      return false;
    }
    const rect = dialog.getBoundingClientRect?.();
    if (rect) {
      if (rect.width === 0 && rect.height === 0) {
        return false;
      }
      if (
        rect.width >= (window.innerWidth || 1920) * 0.85 &&
        rect.height >= (window.innerHeight || 1080) * 0.85
      ) {
        return false;
      }
    }
    const hasRewardContent = Boolean(
      dialog.classList?.contains("reward-center__content") ||
        dialog.querySelector?.(
          '.reward-center__content, [data-test-selector="reward-center"], .rewards-popover-header, [data-a-target="rewards-list"], [data-test-selector="chat-rewards-list"], [aria-labelledby="channel-points-reward-center-header"], [data-test-selector="community-points-reward-center"]'
        )
    );
    return hasRewardContent;
  }

  function findNativeRewardDialog() {
    const marked = document.querySelector(".chatterino-native-reward-dialog");
    if (marked?.isConnected) {
      if (isNativeRewardDialogCandidate(marked)) {
        return marked;
      }
      unmarkRewardDialog(marked);
    }

    const contentSelectors = [
      ".reward-center__content",
      '[data-test-selector="reward-center"]',
      ".rewards-popover-header",
      '[class*="rewards-popover"]',
      '[class*="reward-center"]',
      '[aria-labelledby="channel-points-reward-center-header"]',
      '[data-test-selector="community-points-reward-center"]',
      '[data-a-target="chat-rewards-popover"]',
      '[data-a-target="rewards-list"]',
      '[data-test-selector="chat-rewards-list"]',
      '[class*="reward-details"]',
      '[data-test-selector*="reward-detail"]',
    ];

    for (const selector of contentSelectors) {
      for (const el of document.querySelectorAll(selector)) {
        if (el.closest("#chatterino-toolbar-portal")) {
          continue;
        }
        let dialog = el.closest(
          '[role="dialog"], [role="alertdialog"], .ReactModal__Content, [class*="Attached"], [class*="popover" i], [class*="balloon" i], [data-a-target="tw-balloon"], .tw-balloon'
        );
        if (
          !dialog &&
          !el.matches?.('button, [role="button"]') &&
          el.classList?.contains("reward-center__content")
        ) {
          dialog = el;
        }
        if (dialog && isNativeRewardDialogCandidate(dialog)) {
          return dialog;
        }
      }
    }

    const ariaSelectors = [
      '[role="dialog"][aria-labelledby*="reward" i]',
      '[role="dialog"][aria-label*="reward" i]',
      '[role="dialog"][aria-label*="channel points" i]',
      '[role="dialog"][aria-label*="bits and points" i]',
      '[role="dialog"][aria-label*="power-up" i]',
      '[class*="tw-balloon"]:has([class*="reward" i])',
      '[data-a-target="tw-balloon"]:has([class*="reward" i])',
    ];
    for (const selector of ariaSelectors) {
      const dialog = document.querySelector(selector);
      if (dialog && isNativeRewardDialogCandidate(dialog)) {
        return dialog;
      }
    }

    return null;
  }

  function ensureRewardDialogScrollable(popover) {
    const scrollSelectors = [
      '[class*="scrollable"]',
      '[class*="Scrollable"]',
      ".reward-center__content",
      ".simplebar-content-wrapper",
      ".simplebar-scrollable-node",
      "[data-simplebar-scrollable]",
    ];
    for (const selector of scrollSelectors) {
      popover.querySelectorAll(selector).forEach((el) => {
        el.style.setProperty("pointer-events", "auto", "important");
        el.style.setProperty("visibility", "visible", "important");
        el.style.setProperty("touch-action", "pan-y", "important");
        const overflowY = getComputedStyle(el).overflowY;
        if (overflowY === "hidden" || overflowY === "clip") {
          el.style.setProperty("overflow-y", "auto", "important");
        }
      });
    }
    popover
      .querySelectorAll("textarea, input, button, [contenteditable='true']")
      .forEach((el) => {
        el.style.setProperty("pointer-events", "auto", "important");
        el.style.setProperty("visibility", "visible", "important");
      });
  }

  function positionNativeRewardDialog(anchorEl) {
    const popover = findNativeRewardDialog();
    if (!popover) {
      return false;
    }
    const anchor =
      anchorEl ||
      document.getElementById("chatterino-points-replica") ||
      document.getElementById("chatterino-toolbar-portal");
    if (!anchor) {
      return false;
    }

    popover.classList.remove("chatterino-native-voting-source");
    popover
      .closest(".chatterino-native-voting-source")
      ?.classList.remove("chatterino-native-voting-source");
    popover.classList.add("chatterino-native-reward-dialog");
    popover.style.setProperty("z-index", "2147483647", "important");
    ensureRewardDialogScrollable(popover);

    // The dialog lives inside ancestors with CSS transforms, so
    // `position: fixed` would resolve against them instead of the viewport.
    // Instead, measure where the dialog currently is on screen and shift it
    // to the anchor with a translate() delta. It is never reparented either —
    // it must stay inside the React root so the buttons inside keep working.
    const anchorRect = anchor.getBoundingClientRect();
    const popRect = popover.getBoundingClientRect();
    if (popRect.width === 0 || popRect.height === 0) {
      return false;
    }
    if (
      popRect.width >= (window.innerWidth || 1920) * 0.85 &&
      popRect.height >= (window.innerHeight || 1080) * 0.85
    ) {
      return false;
    }

    const prev = popover.__ccTranslate || { x: 0, y: 0 };
    const baseLeft = popRect.left - prev.x;
    const baseTop = popRect.top - prev.y;

    const desiredLeft = Math.max(
      8,
      Math.min(anchorRect.left, window.innerWidth - popRect.width - 8)
    );
    // Prefer opening below the anchor; flip above it when there is not
    // enough room so the dialog never covers the button itself.
    let desiredTop = anchorRect.bottom + 8;
    if (desiredTop + popRect.height > window.innerHeight - 8) {
      desiredTop = anchorRect.top - popRect.height - 8;
    }
    desiredTop = Math.max(
      8,
      Math.min(desiredTop, window.innerHeight - popRect.height - 8)
    );

    const dx = Math.round(desiredLeft - baseLeft);
    const dy = Math.round(desiredTop - baseTop);
    if (dx !== prev.x || dy !== prev.y) {
      popover.__ccTranslate = { x: dx, y: dy };
      popover.style.setProperty(
        "transform",
        `translate(${dx}px, ${dy}px)`,
        "important"
      );
    }
    return true;
  }

  function stopRewardDialogWatcher() {
    if (rewardDialogWatchId) {
      clearInterval(rewardDialogWatchId);
      rewardDialogWatchId = null;
    }
    if (rewardDialogWatchTimeout) {
      clearTimeout(rewardDialogWatchTimeout);
      rewardDialogWatchTimeout = null;
    }
  }

  function dismissNativeRewardDialog() {
    const dialog =
      document.querySelector(".chatterino-native-reward-dialog") ||
      findNativeRewardDialog();
    if (!dialog) {
      return false;
    }
    stopRewardDialogWatcher();
    unmarkRewardDialog(dialog);
    const closeBtn =
      dialog.querySelector('button[aria-label="Close"]') ||
      dialog.querySelector('button[aria-label="Back"]') ||
      dialog.querySelector('[data-a-target="close-button"]') ||
      dialog.querySelector('[data-a-target="cancel-button"]');
    if (closeBtn) {
      closeBtn.click();
      return true;
    }
    document.dispatchEvent(
      new KeyboardEvent("keydown", {
        key: "Escape",
        code: "Escape",
        bubbles: true,
      })
    );
    return true;
  }

  function stopUnintendedDismissWatcher() {
    if (unintendedDismissTimer) {
      clearInterval(unintendedDismissTimer);
      unintendedDismissTimer = null;
    }
  }

  function scheduleDismissUnintendedRewardDialog() {
    if (intentionalRewardDialogOpen) {
      return;
    }
    stopUnintendedDismissWatcher();

    let attempts = 0;
    const maxAttempts = 40;
    unintendedDismissTimer = setInterval(() => {
      attempts += 1;
      if (intentionalRewardDialogOpen) {
        stopUnintendedDismissWatcher();
        return;
      }
      const dialog = findNativeRewardDialog();
      if (!dialog) {
        if (attempts >= maxAttempts) {
          stopUnintendedDismissWatcher();
        }
        return;
      }
      dismissNativeRewardDialog();
      if (!findNativeRewardDialog() || attempts >= maxAttempts) {
        stopUnintendedDismissWatcher();
      }
    }, 100);
  }

  function resetChannelScopedUi() {
    gqlState = null;
    activityStore.applyGraphql(null);
    intentionalRewardDialogOpen = false;
    stopIntentionalRewardWatcher();
    lastSuccessfulClaimAt = 0;
    stopClaimBootstrap();
    stopRewardDialogWatcher();
    stopUnintendedDismissWatcher();
    stopChatIdentityDialogWatcher();
    stopPredictionDialogWatcher();
    document
      .querySelectorAll(".chatterino-native-prediction-dialog")
      .forEach((el) => {
        unmarkPredictionDialog(el);
      });
    removeBadgeReplica();
    removePointsReplica();
    document.getElementById("chatterino-points-fallback")?.remove();
    document
      .querySelectorAll(".chatterino-native-reward-dialog")
      .forEach((el) => {
        unmarkRewardDialog(el);
      });
    unmarkChatIdentityDialog(findNativeChatIdentityDialog());
    if (rewardPendingActive) {
      forwardRewardClear("channel-reset");
    }
  }

  function startRewardDialogWatcher(anchorEl) {
    stopRewardDialogWatcher();
    rewardDialogWatchId = setInterval(() => {
      const dialog = findNativeRewardDialog();
      if (!dialog) {
        stopRewardDialogWatcher();
        return;
      }
      positionNativeRewardDialog(anchorEl);
    }, 100);

    rewardDialogWatchTimeout = setTimeout(stopRewardDialogWatcher, 60000);
  }

  // Prediction bet prompt — Twitch's "How many Channel Points do you want to
  // use?" modal. Picking an outcome in the toolbar replica forwards the click
  // to the native control, which is parked off-screen at left:-10000px, so
  // Twitch anchors the prompt to that hidden control and it opens outside the
  // viewport. Reveal it and shift it beside the toolbar, exactly like the
  // reward centre, so a viewer can actually spend their points.
  function isNativePredictionDialogCandidate(dialog) {
    if (!dialog || !dialog.isConnected) {
      return false;
    }
    if (dialog.closest("#chatterino-toolbar-portal")) {
      return false;
    }
    if (isOverlayOrBackdrop(dialog)) {
      return false;
    }
    if (window.ChatterinoVotingUi?.isVideoPlayerUi?.(dialog)) {
      return false;
    }
    if (
      dialog.tagName === "BUTTON" ||
      dialog.matches?.('button, [role="button"]')
    ) {
      return false;
    }
    if (
      dialog.matches?.(
        '[data-test-selector*="prediction-banner"], .prediction-banner'
      ) ||
      dialog.classList?.contains("chatterino-moved-banner-inline") ||
      dialog.classList?.contains("chatterino-voting-replica")
    ) {
      return false;
    }
    if (
      dialog.querySelector?.(".reward-center__content") ||
      dialog.querySelector?.('[class*="rewards-popover"]')
    ) {
      return false;
    }
    const rect = dialog.getBoundingClientRect?.();
    if (rect) {
      if (rect.width === 0 && rect.height === 0) {
        return false;
      }
      if (
        rect.width >= (window.innerWidth || 1920) * 0.85 &&
        rect.height >= (window.innerHeight || 1080) * 0.85
      ) {
        return false;
      }
    }
    return Boolean(
      window.ChatterinoVotingUi?.isPredictionBetPrompt(dialog) ||
        window.ChatterinoVotingUi?.isPredictionDialog(dialog) ||
        dialog.querySelector?.(
          '[data-test-selector*="prediction-bet"], [data-a-target*="prediction-bet"], [class*="prediction-bet"]'
        )
    );
  }

  function findNativePredictionDialog() {
    const marked = document.querySelector(
      ".chatterino-native-prediction-dialog"
    );
    if (marked?.isConnected) {
      if (isNativePredictionDialogCandidate(marked)) {
        return marked;
      }
      unmarkPredictionDialog(marked);
    }

    const dedicated = [
      'div[role="dialog"]:has([data-test-selector*="prediction-bet"])',
      'div[role="dialog"]:has([data-a-target*="prediction-bet"])',
      'div[role="dialog"]:has([class*="prediction-bet"])',
      '[role="dialog"]:has([data-test-selector*="prediction" i])',
      '[role="dialog"]:has([data-a-target*="prediction" i])',
      '[role="dialog"]:has([class*="prediction" i])',
      '[role="dialog"][aria-label*="prediction" i]',
      '[role="dialog"][aria-label*="predict" i]',
      '.ReactModal__Content:has([data-test-selector*="prediction" i])',
      '.ReactModal__Content:has([data-a-target*="prediction" i])',
      '.ReactModal__Content:has([class*="prediction" i])',
      '.ReactModal__Content[aria-label*="prediction" i]',
      '[data-test-selector="community-prediction-modal"]',
      '[data-a-target="community-prediction-modal"]',
      '[class*="prediction-modal" i]',
      '[class*="prediction-dialog" i]',
      '[class*="prediction-prompt" i]',
      '[class*="tw-balloon"]:has([class*="prediction" i])',
      '[data-a-target="tw-balloon"]:has([class*="prediction" i])',
    ];
    for (const selector of dedicated) {
      for (let dialog of document.querySelectorAll(selector)) {
        if (isOverlayOrBackdrop(dialog)) {
          dialog =
            dialog.querySelector?.(
              '.ReactModal__Content, [role="dialog"], [role="alertdialog"]'
            ) || null;
        }
        if (dialog && isNativePredictionDialogCandidate(dialog)) {
          return dialog;
        }
      }
    }

    for (let dialog of document.querySelectorAll(
      'div[role="dialog"], [role="dialog"], div.ReactModal__Content, .ReactModal__Content, [class*="balloon" i]'
    )) {
      if (isOverlayOrBackdrop(dialog)) {
        dialog =
          dialog.querySelector?.(
            '.ReactModal__Content, [role="dialog"], [role="alertdialog"]'
          ) || null;
      }
      if (dialog && isNativePredictionDialogCandidate(dialog)) {
        return dialog;
      }
    }
    return null;
  }

  function ensurePredictionDialogScrollable(popover) {
    const scrollSelectors = [
      '[class*="scrollable"]',
      '[class*="Scrollable"]',
      ".simplebar-content-wrapper",
      ".simplebar-scrollable-node",
      "[data-simplebar-scrollable]",
    ];
    for (const selector of scrollSelectors) {
      popover.querySelectorAll(selector).forEach((el) => {
        el.style.setProperty("pointer-events", "auto", "important");
        el.style.setProperty("visibility", "visible", "important");
        el.style.setProperty("touch-action", "pan-y", "important");
        const overflowY = getComputedStyle(el).overflowY;
        if (overflowY === "hidden" || overflowY === "clip") {
          el.style.setProperty("overflow-y", "auto", "important");
        }
      });
    }
    popover
      .querySelectorAll("textarea, input, button, [contenteditable='true']")
      .forEach((el) => {
        el.style.setProperty("pointer-events", "auto", "important");
        el.style.setProperty("visibility", "visible", "important");
      });
  }

  function positionNativePredictionDialog(anchorEl) {
    const popover = findNativePredictionDialog();
    if (!popover) {
      return false;
    }
    const anchor =
      anchorEl ||
      document.getElementById("chatterino-prediction-replica") ||
      document.getElementById("chatterino-points-replica") ||
      document.getElementById("chatterino-toolbar-portal");
    if (!anchor) {
      return false;
    }

    // The prompt is never the banner source. If an earlier sync adopted it,
    // hand it back before measuring so it is not stuck at left:-10000px.
    popover.classList.remove("chatterino-native-voting-source");
    popover
      .closest(".chatterino-native-voting-source")
      ?.classList.remove("chatterino-native-voting-source");
    if (
      activePredictionSource === popover ||
      activePredictionSource ===
        popover.closest(".chatterino-native-voting-source")
    ) {
      releaseVotingSource(activePredictionSource);
      activePredictionSource = null;
    }
    popover.classList.add("chatterino-native-prediction-dialog");
    popover.style.setProperty("z-index", "2147483647", "important");
    ensurePredictionDialogScrollable(popover);

    // Measured-delta placement for the same reason as the reward dialog: the
    // prompt stays inside the React root (never reparented) and its ancestors
    // carry transforms, so `position: fixed` would not resolve to the viewport.
    const anchorRect = anchor.getBoundingClientRect();
    const popRect = popover.getBoundingClientRect();
    if (popRect.width === 0 || popRect.height === 0) {
      return false;
    }
    if (
      popRect.width >= (window.innerWidth || 1920) * 0.85 &&
      popRect.height >= (window.innerHeight || 1080) * 0.85
    ) {
      return false;
    }

    const prev = popover.__ccTranslate || { x: 0, y: 0 };
    const baseLeft = popRect.left - prev.x;
    const baseTop = popRect.top - prev.y;

    const desiredLeft = Math.max(
      8,
      Math.min(anchorRect.left, window.innerWidth - popRect.width - 8)
    );
    let desiredTop = anchorRect.bottom + 8;
    if (desiredTop + popRect.height > window.innerHeight - 8) {
      desiredTop = anchorRect.top - popRect.height - 8;
    }
    desiredTop = Math.max(
      8,
      Math.min(desiredTop, window.innerHeight - popRect.height - 8)
    );

    const dx = Math.round(desiredLeft - baseLeft);
    const dy = Math.round(desiredTop - baseTop);
    if (dx !== prev.x || dy !== prev.y) {
      popover.__ccTranslate = { x: dx, y: dy };
      popover.style.setProperty(
        "transform",
        `translate(${dx}px, ${dy}px)`,
        "important"
      );
    }
    return true;
  }

  function dismissNativePredictionDialog() {
    const dialog =
      document.querySelector(".chatterino-native-prediction-dialog") ||
      findNativePredictionDialog();
    if (!dialog) {
      return false;
    }
    stopPredictionDialogWatcher();
    unmarkPredictionDialog(dialog);
    const closeBtn =
      dialog.querySelector('button[aria-label="Close"]') ||
      dialog.querySelector('button[aria-label="Back"]') ||
      dialog.querySelector('[data-a-target="close-button"]') ||
      dialog.querySelector('[data-a-target="cancel-button"]');
    if (closeBtn) {
      closeBtn.click();
      return true;
    }
    document.dispatchEvent(
      new KeyboardEvent("keydown", {
        key: "Escape",
        code: "Escape",
        bubbles: true,
      })
    );
    return true;
  }

  function stopPredictionDialogWatcher() {
    if (predictionDialogWatchId) {
      clearInterval(predictionDialogWatchId);
      predictionDialogWatchId = null;
    }
    if (predictionDialogWatchTimeout) {
      clearTimeout(predictionDialogWatchTimeout);
      predictionDialogWatchTimeout = null;
    }
  }

  function startPredictionDialogWatcher(anchorEl) {
    stopPredictionDialogWatcher();
    predictionDialogWatchId = setInterval(() => {
      if (!findNativePredictionDialog()) {
        stopPredictionDialogWatcher();
        return;
      }
      positionNativePredictionDialog(anchorEl);
    }, 100);

    predictionDialogWatchTimeout = setTimeout(
      stopPredictionDialogWatcher,
      60000
    );
  }

  function schedulePredictionDialogPositioning(anchorEl) {
    const attempt = (triesLeft) => {
      if (positionNativePredictionDialog(anchorEl)) {
        startPredictionDialogWatcher(anchorEl);
        return;
      }
      if (triesLeft > 0) {
        requestAnimationFrame(() => attempt(triesLeft - 1));
      }
    };
    requestAnimationFrame(() => attempt(120));
  }

  function isChatShellWiped() {
    const shell = document.querySelector(".chat-shell");
    const text = shell?.children[0]?.innerText || "";
    return (
      text.includes("Chatterino should show here") ||
      text.includes("Connection to the Chatterino extension lost") ||
      text.includes("Chatterino also needs to be running")
    );
  }

  function syncGqlFromDomAttributes() {
    const root = document.documentElement;
    const PageData = window.ChatterinoPageData;
    const balance = PageData.sanitizeBalance(
      root.getAttribute("data-cc-gql-balance")
    );
    const claim = root.getAttribute("data-cc-gql-claim");
    const claimId = PageData.sanitizeId(
      root.getAttribute("data-cc-gql-claim-id")
    );
    const predRaw = root.getAttribute("data-cc-gql-prediction");
    const pollRaw = root.getAttribute("data-cc-gql-poll");
    const rewardsRaw = root.getAttribute("data-cc-gql-rewards");
    const hasPrediction = root.hasAttribute("data-cc-gql-prediction");
    const hasPoll = root.hasAttribute("data-cc-gql-poll");

    if (
      !balance &&
      claim == null &&
      !claimId &&
      !predRaw &&
      !pollRaw &&
      !rewardsRaw
    ) {
      return;
    }

    gqlState = gqlState || {
      channelPoints: {},
      prediction: null,
      poll: null,
      rewards: [],
    };

    gqlState.channelPoints = {
      ...gqlState.channelPoints,
      ...(balance ? { balance } : {}),
      ...(claim != null ? { claimAvailable: claim === "1" } : {}),
      ...(claimId ? { claimId } : {}),
    };

    if (hasPrediction) {
      gqlState.prediction = PageData.parseActivityAttribute(predRaw);
    }

    if (hasPoll) {
      gqlState.poll = PageData.parseActivityAttribute(pollRaw);
    }

    if (rewardsRaw) {
      gqlState.rewards = PageData.parseRewardsAttribute(rewardsRaw);
    }

    activityStore.applyGraphql(gqlState);
  }

  function ensureToolbarPortal() {
    let portal = document.getElementById("chatterino-toolbar-portal");
    if (portal) {
      return portal;
    }

    portal = document.createElement("div");
    portal.id = "chatterino-toolbar-portal";

    const slot = document.createElement("div");
    slot.id = "chatterino-toolbar-slot";
    portal.appendChild(slot);

    document.body.appendChild(portal);

    // Listeners are installed once per script, not once per portal: if the
    // portal is ever removed and rebuilt they would otherwise stack.
    if (!portalListenersInstalled) {
      portalListenersInstalled = true;
      window.addEventListener("scroll", schedulePortalReposition, true);
      window.addEventListener("resize", schedulePortalReposition);
    }

    return portal;
  }

  let portalListenersInstalled = false;
  let portalRepositionFrame = null;
  let portalRepositionFollowUp = false;

  // Capture-phase scroll fires for every scrolling element on the page (chat
  // list, sidebars); a full reposition per event forced a layout each time.
  // Coalesce scroll/resize/mount requests into at most one reposition per
  // frame, plus a single follow-up frame for layout that settles late.
  function schedulePortalReposition(withFollowUp = false) {
    if (contextTornDown) {
      return;
    }
    if (withFollowUp === true) {
      portalRepositionFollowUp = true;
    }
    if (portalRepositionFrame !== null) {
      return;
    }
    portalRepositionFrame = requestAnimationFrame(() => {
      portalRepositionFrame = null;
      positionToolbarPortal();
      if (portalRepositionFollowUp) {
        portalRepositionFollowUp = false;
        schedulePortalReposition();
      }
    });
  }

  function positionToolbarPortal() {
    if (contextTornDown) {
      return null;
    }
    ensureToolbarPortal();
    const portal = document.getElementById("chatterino-toolbar-portal");
    const slot = document.getElementById("chatterino-toolbar-slot");

    if (!portal || !slot) {
      return null;
    }

    const fullscreen =
      window.ChatterinoVotingUi?.isFullscreenActive(document, window) || false;
    document.documentElement.classList.toggle(
      "chatterino-video-fullscreen",
      fullscreen
    );
    if (fullscreen) {
      portal.style.setProperty("display", "none", "important");
      return slot;
    }

    const targetBtn = findTargetButton();
    if (!targetBtn) {
      portal.style.setProperty("display", "none", "important");
      return null;
    }

    const rect = targetBtn.getBoundingClientRect();
    if (rect.width === 0 && rect.height === 0) {
      portal.style.setProperty("display", "none", "important");
      return slot;
    }

    // Do not show the portal if the target button is scrolled out of the viewport
    if (
      rect.bottom < 0 ||
      rect.top > (window.innerHeight || 0) ||
      rect.right < 0 ||
      rect.left > (window.innerWidth || 0)
    ) {
      portal.style.setProperty("display", "none", "important");
      return slot;
    }

    // Do not show the portal if it would overlap active video player controls
    const playerControls = document.querySelector(
      '.player-controls, [data-a-target="player-controls"], .video-player__default-player'
    );
    if (playerControls) {
      const controlsRect = playerControls.getBoundingClientRect();
      if (
        controlsRect.width > 0 &&
        controlsRect.height > 0 &&
        rect.top >= controlsRect.top &&
        rect.top <= controlsRect.bottom &&
        rect.left >= controlsRect.left &&
        rect.left <= controlsRect.right
      ) {
        portal.style.setProperty("display", "none", "important");
        return slot;
      }
    }

    portal.style.setProperty("display", "inline-flex", "important");
    slot.style.setProperty("display", "inline-flex", "important");

    const slotWidth = slot.offsetWidth || 1;
    const slotHeight = slot.offsetHeight || 32;
    const top = rect.top + Math.max(0, (rect.height - slotHeight) / 2);
    const left = Math.max(8, rect.left - slotWidth - 8);
    portal.style.top = `${top}px`;
    portal.style.left = `${left}px`;

    requestAnimationFrame(() => {
      const width = slot.offsetWidth;
      if (width > 0) {
        portal.style.left = `${Math.max(8, rect.left - width - 8)}px`;
      }
    });

    return slot;
  }

  function getToolbarSlot() {
    ensureToolbarPortal();
    return document.getElementById("chatterino-toolbar-slot");
  }

  // Every replica is mounted on every sync. Each mount used to run two
  // synchronous repositions plus two nested animation frames, i.e. ~20 layout
  // passes per sync. Reposition synchronously only when the slot content
  // actually changed; otherwise the coalesced frame (and runSync's final
  // reposition) keep the portal aligned.
  function mountInSlot(element, orderIndex) {
    const slot = getToolbarSlot();
    if (!slot || !element) {
      return;
    }
    const order = String(orderIndex);
    let changed = false;
    if (element.style.order !== order) {
      element.style.order = order;
      changed = true;
    }
    if (element.parentElement !== slot) {
      slot.appendChild(element);
      changed = true;
    }
    if (changed) {
      positionToolbarPortal();
    }
    schedulePortalReposition(true);
  }

  function getTwitchChannelName() {
    const path = window.location.pathname.toLowerCase();
    const parts = path.split("/").filter(Boolean);
    if (parts.length === 0) {
      return "";
    }
    if (parts[0] === "popout" && parts[1]) {
      return parts[1];
    }
    if (parts[0] === "moderator" && parts[1]) {
      return parts[1];
    }
    const staticPages = [
      "directory",
      "videos",
      "settings",
      "subscriptions",
      "wallet",
      "drops",
      "search",
    ];
    if (staticPages.includes(parts[0])) {
      return "";
    }
    return parts[0];
  }

  function isContextInvalidated() {
    if (contextTornDown) {
      return true;
    }
    let alive = false;
    try {
      alive = Boolean(chrome?.runtime?.id);
    } catch (_err) {
      alive = false;
    }
    if (!alive) {
      teardownOrphanedScript();
      return true;
    }
    return false;
  }

  // Previously only the two intervals were cleared, so the document-wide
  // MutationObservers, watcher intervals and claim timers of an orphaned
  // script kept running (and throwing on chrome.* calls) until the tab closed.
  function teardownOrphanedScript() {
    if (contextTornDown) {
      return;
    }
    contextTornDown = true;
    clearInterval(pollIntervalId);
    clearInterval(syncIntervalId);
    pollIntervalId = null;
    syncIntervalId = null;
    clearTimeout(syncTimer);
    syncTimer = null;
    syncPending = false;
    clearTimeout(claimAttemptTimer);
    claimAttemptTimer = null;
    clearTimeout(claimBonusDebounceTimer);
    claimBonusDebounceTimer = null;
    clearTimeout(bootstrapObserveTimer);
    bootstrapObserveTimer = null;
    channelResetTimers.forEach(clearTimeout);
    channelResetTimers = [];
    stopClaimBootstrap();
    stopRewardDialogWatcher();
    stopIntentionalRewardWatcher();
    stopUnintendedDismissWatcher();
    stopChatIdentityDialogWatcher();
    stopPredictionDialogWatcher();
    observer.disconnect();
    bootstrapObserver.disconnect();
    claimBonusObserver.disconnect();
    observerAttached = false;
    removeToolbarReplicas();
  }

  // The orphaned script can no longer keep its toolbar replicas current (or
  // forward their clicks), so they are removed and the native controls they
  // stood in for are revealed again. Only DOM owned by this script is touched.
  function removeToolbarReplicas() {
    try {
      cancelAnimationFrame(portalRepositionFrame);
    } catch (_err) {
      // requestAnimationFrame may be unavailable; nothing to cancel then.
    }
    portalRepositionFrame = null;
    portalRepositionFollowUp = false;
    if (portalListenersInstalled) {
      try {
        window.removeEventListener("scroll", schedulePortalReposition, true);
        window.removeEventListener("resize", schedulePortalReposition);
      } catch (_err) {
        // The listeners are inert after teardown anyway.
      }
      portalListenersInstalled = false;
    }
    for (const cleanup of [
      removeBadgeReplica,
      removePointsReplica,
      cleanupPredictionUi,
      cleanupPollUi,
    ]) {
      try {
        cleanup();
      } catch (_err) {
        // Keep removing the rest even if Twitch markup changed underneath.
      }
    }
    for (const id of [
      "chatterino-points-fallback",
      "chatterino-toolbar-portal",
    ]) {
      document.getElementById(id)?.remove();
    }
    document.documentElement.classList.remove("chatterino-video-fullscreen");
  }

  function resetFingerprints() {
    if (isContextInvalidated()) {
      return;
    }
    activityStore.resetPublications();
  }

  function findVotingSurface(selectors, kind) {
    const candidates = new Map();
    for (const selector of selectors) {
      for (const surface of document.querySelectorAll(selector)) {
        if (
          window.ChatterinoVotingUi?.isPredictionBetPrompt(surface) ||
          window.ChatterinoVotingUi?.isPredictionDialog(surface) ||
          surface.classList?.contains("chatterino-native-prediction-dialog") ||
          surface.classList?.contains("chatterino-native-reward-dialog") ||
          surface.closest?.(".chatterino-native-prediction-dialog") ||
          surface.closest?.(".chatterino-native-reward-dialog")
        ) {
          continue;
        }
        candidates.set(surface, 20);
      }
    }
    for (const surface of document.querySelectorAll(
      '[role="dialog"], [role="menu"]'
    )) {
      if (window.ChatterinoVotingUi.isGenericVotingSurface(surface, kind)) {
        candidates.set(surface, 40);
      }
    }

    let best = null;
    let bestScore = -1;
    for (const [surface, baseScore] of candidates) {
      if (
        !surface.isConnected ||
        surface.closest("#chatterino-toolbar-portal")
      ) {
        continue;
      }
      const controls = surface.querySelectorAll(VOTING_CONTROL_SELECTOR).length;
      const score = baseScore + Math.min(controls, 10);
      if (score > bestScore) {
        best = surface;
        bestScore = score;
      }
    }
    return best;
  }

  function findBanner() {
    return findVotingSurface(BANNER_SELECTORS, "prediction");
  }

  function findPollBanner() {
    return findVotingSurface(POLL_BANNER_SELECTORS, "poll");
  }

  function updateDisplayState(banner) {
    const minIcon = document.getElementById("chatterino-prediction-min-icon");
    if (isMinimized) {
      banner.style.setProperty("display", "none", "important");
      if (minIcon) {
        minIcon.style.setProperty("display", "inline-flex", "important");
      }
    } else {
      banner.style.removeProperty("display");
      if (minIcon) {
        minIcon.style.setProperty("display", "none", "important");
      }
    }
  }

  function isClaimBonusButtonClaimable(btn) {
    if (!btn || !btn.isConnected) {
      return false;
    }
    if (btn.closest("#chatterino-toolbar-portal, #chatterino-points-replica")) {
      return false;
    }
    if (btn.closest('[aria-hidden="true"]')) {
      return false;
    }
    return true;
  }

  function findClaimBonusButton() {
    const candidates = [];

    for (const selector of CLAIM_BONUS_SELECTORS) {
      for (const btn of document.querySelectorAll(selector)) {
        if (isClaimBonusButtonClaimable(btn)) {
          candidates.push(btn);
        }
      }
    }

    for (const icon of document.querySelectorAll(".claimable-bonus__icon")) {
      const btn = icon.closest("button");
      if (isClaimBonusButtonClaimable(btn)) {
        candidates.push(btn);
      }
    }

    if (candidates.length === 0) {
      return null;
    }

    const rewardDialog = findNativeRewardDialog();
    const outsideDialog = candidates.find(
      (btn) => !rewardDialog?.contains(btn)
    );
    return outsideDialog || candidates[0];
  }

  function dispatchClaimClick(btn, includeSynthetic = false) {
    btn.click();
    if (includeSynthetic) {
      btn.dispatchEvent(
        new MouseEvent("click", {
          bubbles: true,
          cancelable: true,
          view: window,
        })
      );
    }
  }

  function markClaimSucceeded() {
    lastSuccessfulClaimAt = Date.now();
  }

  function verifyClaimClick(clickedButton, attempt) {
    const stillClaimable =
      clickedButton.isConnected && isClaimBonusButtonClaimable(clickedButton);
    if (!stillClaimable) {
      markClaimSucceeded();
      return;
    }
    if (attempt >= 10) {
      return;
    }
    const delayMs = attempt < 4 ? 250 : attempt < 7 ? 750 : 1500;
    setTimeout(() => {
      if (
        !clickedButton.isConnected ||
        !isClaimBonusButtonClaimable(clickedButton)
      ) {
        markClaimSucceeded();
        return;
      }
      dispatchClaimClick(clickedButton, true);
      verifyClaimClick(clickedButton, attempt + 1);
    }, delayMs);
  }

  function stopClaimBootstrap() {
    for (const timerId of claimBootstrapTimers) {
      clearTimeout(timerId);
    }
    claimBootstrapTimers = [];
  }

  function scheduleClaimBootstrap() {
    stopClaimBootstrap();
    for (const delay of CLAIM_BOOTSTRAP_DELAYS_MS) {
      claimBootstrapTimers.push(
        setTimeout(() => scheduleAutoClaimAttempt(), delay)
      );
    }
  }

  function scheduleAutoClaimAttempt() {
    if (contextTornDown || claimAttemptTimer !== null) {
      return;
    }
    claimAttemptTimer = setTimeout(() => {
      claimAttemptTimer = null;
      tryAutoClaim();
    }, 100);
  }

  function tryAutoClaim() {
    if (!autoClaimEnabled) {
      return;
    }

    const claimButton = findClaimBonusButton();
    const now = Date.now();
    const recentlyClaimed =
      now - lastSuccessfulClaimAt < AUTO_CLAIM_MIN_INTERVAL_MS;

    if (claimButton) {
      if (recentlyClaimed) {
        return;
      }
      dispatchClaimClick(claimButton);
      verifyClaimClick(claimButton, 0);
      return;
    }

    // Fallback: GQL claim when bonus is confirmed but the button is not mounted yet.
    if (recentlyClaimed || gqlState?.channelPoints?.claimAvailable !== true) {
      return;
    }
    const claimId =
      gqlState?.channelPoints?.claimId ||
      window.ChatterinoPageData.sanitizeId(
        document.documentElement.getAttribute("data-cc-gql-claim-id")
      );
    if (!claimId) {
      return;
    }

    window.dispatchEvent(new CustomEvent("chatterino-companion-claim-request"));
  }

  function scrapePointsText(pointsSummary) {
    if (!pointsSummary) {
      return "";
    }
    // textContent (not innerText) so this still works when the native element
    // is visually hidden by companion mode or the Chatterino integration.
    const text = pointsSummary.textContent.replace(/\s+/g, " ").trim();
    const match = text.match(/([\d,.]+[KMB]?)/i);
    return match ? match[1] : "";
  }

  const PINNED_MESSAGE_BODY_SELECTORS = [
    '[data-a-target="chat-line-message-body"]',
    '[data-test-selector="message-text"]',
    '[data-test-selector="chat-message-text"]',
    '[class*="chat-line__message"]',
    '[class*="message-body"]',
  ];

  const PINNED_BLOCK_TAGS = new Set([
    "p",
    "div",
    "li",
    "h1",
    "h2",
    "h3",
    "h4",
    "blockquote",
  ]);

  function findPinnedMessageBody(root) {
    if (!root) {
      return null;
    }
    for (const selector of PINNED_MESSAGE_BODY_SELECTORS) {
      const el = root.querySelector(selector);
      if (el) {
        return el;
      }
    }
    return (
      root.querySelector('[class*="message"]') ||
      root.querySelector('[class*="text"]') ||
      root.querySelector('[class*="body"]') ||
      root
    );
  }

  function scrapeTextWithNewlines(node) {
    if (!node) {
      return "";
    }
    const parts = [];
    const walk = (el) => {
      if (el.nodeType === Node.TEXT_NODE) {
        parts.push(el.textContent);
        return;
      }
      if (el.nodeType !== Node.ELEMENT_NODE) {
        return;
      }
      const tag = el.tagName?.toLowerCase();
      if (tag === "a") {
        const href = el.getAttribute("href")?.trim() || "";
        const label = el.textContent.replace(/\s+/g, " ").trim();
        parts.push(href || label);
        return;
      }
      if (tag === "br") {
        parts.push("\n");
        return;
      }
      if (
        PINNED_BLOCK_TAGS.has(tag) &&
        parts.length > 0 &&
        !parts[parts.length - 1].endsWith("\n")
      ) {
        parts.push("\n");
      }
      for (const child of el.childNodes) {
        walk(child);
      }
      if (PINNED_BLOCK_TAGS.has(tag)) {
        parts.push("\n");
      }
    };
    walk(node);
    return parts.join("");
  }

  function joinWrappedUrls(text) {
    return String(text || "")
      .replace(/(https?:\/\/)\s*\n\s*/gi, "$1")
      .replace(/(www\.)\s*\n\s*/gi, "$1")
      .replace(/(https?:\/\/[^\s\n]*)\n([^\s\n]+)/gi, "$1$2");
  }

  function applySentenceSpacingOutsideUrls(text) {
    const urlRx = /(https?:\/\/[^\s]+|www\.[^\s]+)/gi;
    let result = "";
    let last = 0;
    let match;
    while ((match = urlRx.exec(text)) !== null) {
      const before = text.slice(last, match.index);
      result += before.replace(/([.!?])([A-Za-zÀ-ÖØ-öø-ÿ])/g, "$1 $2");
      result += match[0];
      last = match.index + match[0].length;
    }
    result += text.slice(last).replace(/([.!?])([A-Za-zÀ-ÖØ-öø-ÿ])/g, "$1 $2");
    return result;
  }

  function formatPinnedAnnouncementText(raw) {
    const lines = joinWrappedUrls(raw)
      .replace(/\r\n?/g, "\n")
      .replace(/✕/g, "")
      .replace(/\b(dismiss|unpin|pinned message)\b/gi, "")
      .split("\n")
      .map((line) => line.replace(/[^\S\n]+/g, " ").trim());

    const compacted = [];
    let lastWasEmpty = false;
    for (const line of lines) {
      const empty = line.length === 0;
      if (empty && lastWasEmpty) {
        continue;
      }
      compacted.push(line);
      lastWasEmpty = empty;
    }
    while (compacted.length > 0 && compacted[0] === "") {
      compacted.shift();
    }
    while (compacted.length > 0 && compacted[compacted.length - 1] === "") {
      compacted.pop();
    }

    return applySentenceSpacingOutsideUrls(
      compacted.join(" ").replace(/\s+/g, " ")
    ).trim();
  }

  function isSubscriptionHighlight(element, text) {
    if (!element) {
      return false;
    }

    const sample = String(text || element.textContent || "")
      .replace(/\s+/g, " ")
      .trim();
    if (
      sample &&
      SUBSCRIPTION_HIGHLIGHT_PATTERNS.some((re) => re.test(sample))
    ) {
      return true;
    }

    const classHint = element.className?.toString?.() || "";
    if (/sub-?gift|subscription/i.test(classHint)) {
      return true;
    }

    if (
      element.querySelector(
        '[data-a-target="sub-gift-chat-message"], [class*="sub-gift"], [class*="SubGift"]'
      )
    ) {
      return true;
    }

    const aria = element.getAttribute("aria-label") || "";
    if (
      aria &&
      /\b(subscription|gifted|resub)\b/i.test(aria) &&
      !/\bannouncement\b/i.test(aria)
    ) {
      return true;
    }

    return false;
  }

  function containsVotingBanner(element) {
    return Boolean(
      element?.matches?.(VOTING_BANNER_SELECTOR) ||
      element?.querySelector?.(VOTING_BANNER_SELECTOR)
    );
  }

  function findPinnedAnnouncementElement() {
    for (const selector of PINNED_SELECTORS) {
      for (const el of document.querySelectorAll(selector)) {
        if (el.closest("#chatterino-toolbar-portal")) {
          continue;
        }
        // Polls and predictions have their own structured native-messaging
        // path. Scraping their full Twitch card as a pin duplicates labels,
        // countdown text, and truncated option names in Chatterino.
        if (containsVotingBanner(el)) {
          continue;
        }
        if (isSubscriptionHighlight(el)) {
          continue;
        }
        return el;
      }
    }
    return null;
  }

  function scrapePinnedMessageText(root) {
    const messageBody = findPinnedMessageBody(root);
    if (!messageBody) {
      return "";
    }

    // textContent avoids layout-wrapped line breaks inside URLs (innerText
    // splits "https://starforgepc.com/foo" across lines when the banner is narrow).
    let raw = messageBody.textContent.replace(/\s+/g, " ").trim();
    if (!raw) {
      raw = joinWrappedUrls(scrapeTextWithNewlines(messageBody));
    }
    const formatted = formatPinnedAnnouncementText(raw);
    if (isSubscriptionHighlight(root, formatted)) {
      return "";
    }
    return formatted;
  }

  function handlePinnedMessages(channelName) {
    const pinnedBanner = findPinnedAnnouncementElement();
    const pinnedText = pinnedBanner
      ? scrapePinnedMessageText(pinnedBanner)
      : "";

    if (!channelName) {
      return;
    }

    const pinFingerprint = JSON.stringify({ text: pinnedText });
    if (pinFingerprint !== lastPinFingerprint) {
      lastPinFingerprint = pinFingerprint;
      safeSendMessage({
        action: "pin",
        channel: channelName,
        message: pinnedText,
      });
    }
  }

  function parseBannerDetails(banner) {
    let title = "";
    const titleEl =
      banner.querySelector('[class*="title"]') ||
      banner.querySelector("h4") ||
      banner.querySelector('[class*="header"]');
    if (titleEl) {
      title = titleEl.textContent.trim();
    } else {
      const lines = banner.innerText
        .split("\n")
        .map((s) => s.trim())
        .filter(Boolean);
      if (lines.length > 0) {
        title = lines[0];
      }
    }

    const options = [];
    banner
      .querySelectorAll('button, [class*="option"], [class*="outcome"]')
      .forEach((el) => {
        const text = el.textContent.trim();
        if (
          text &&
          text.length < 50 &&
          !text.includes("✕") &&
          !text.includes("Dismiss") &&
          !text.includes("Delete") &&
          !text.includes("—")
        ) {
          if (!options.includes(text)) {
            options.push(text);
          }
        }
      });

    let status = "started";
    const bannerText = banner.textContent.toLowerCase();
    if (
      bannerText.includes("submissions closed") ||
      bannerText.includes("locked")
    ) {
      status = "locked";
    } else if (
      bannerText.includes("ended") ||
      bannerText.includes("won") ||
      bannerText.includes("refunded")
    ) {
      status = "ended";
    }

    let durationSeconds = 0;
    const timerMatch =
      banner.textContent.match(/(\d+):(\d+)\s*(?:remaining|left)/i) ||
      banner.textContent.match(/(\d+)\s*m\s*(\d+)\s*s/i) ||
      banner.textContent.match(/in\s*(\d+)\s*m/i);

    if (timerMatch) {
      if (timerMatch[2] !== undefined) {
        durationSeconds =
          parseInt(timerMatch[1], 10) * 60 + parseInt(timerMatch[2], 10);
      } else {
        durationSeconds = parseInt(timerMatch[1], 10) * 60;
      }
    } else {
      const secMatch = banner.textContent.match(
        /(\d+)\s*s\s*(?:remaining|left)?/i
      );
      if (secMatch) {
        durationSeconds = parseInt(secMatch[1], 10);
      }
    }

    let winner = "";
    if (status === "ended") {
      const winnerEl =
        banner.querySelector('[class*="winner"]') ||
        banner.querySelector('[class*="won"]');
      if (winnerEl) {
        winner = winnerEl.textContent.trim();
      } else {
        const lines = banner.innerText
          .split("\n")
          .map((s) => s.trim())
          .filter(Boolean);
        const winLine = lines.find(
          (l) =>
            l.toLowerCase().includes("won") ||
            l.toLowerCase().includes("winner") ||
            l.toLowerCase().includes("ended")
        );
        if (winLine) {
          winner = winLine;
        }
      }
    }

    return { title, options, status, durationSeconds, winner };
  }

  // Ended DOM activities expire like GraphQL ones do in twitch-api.js. The
  // native card itself (and its toolbar replica) stays as Twitch shows it;
  // only what is published to the desktop is affected.
  const domEndedExpiry = window.ChatterinoPageData.createEndedActivityExpiry();

  function mergeVotingDetails(kind, domDetails) {
    activityStore.applyGraphql(gqlState);
    return activityStore.observeDom(
      kind,
      domEndedExpiry.filter(kind, domDetails)
    );
  }

  function sendVotingMessage(kind, channelName, options) {
    const publication = activityStore.nextPublication(kind, options);
    if (!publication || !channelName) {
      return;
    }
    const details = publication.activity;
    safeSendMessage(
      window.ChatterinoProtocol.create("engagement", {
        kind,
        channel: channelName,
        lifecycle: publication.lifecycle,
        ...(details
          ? {
              title: details.title,
              options: details.options,
              status: details.status,
              duration: details.durationSeconds,
              closesAt: details.closesAt,
              winner: details.winner,
            }
          : {}),
      })
    );
  }

  function removeVotingMessages(channelName) {
    if (!channelName) {
      return;
    }
    for (const kind of window.ChatterinoActivity.ACTIVITY_KINDS) {
      safeSendMessage(
        window.ChatterinoProtocol.create("engagement", {
          lifecycle: "remove",
          kind,
          channel: channelName,
        })
      );
    }
  }

  // Predictions are voted on in two places: the replica beside the player and
  // the sticky banner at the top of the Chatterino chat, whose click opens the
  // desktop prediction dialog. Both follow the same activity store, so the
  // banner appears when a prediction starts and is removed when it ends.
  function sendPredictionMessage(channelName) {
    sendVotingMessage("prediction", channelName, { hideEnded: true });
  }

  function sendPollMessage(channelName) {
    sendVotingMessage("poll", channelName);
  }

  function sanitizeVotingClone(clone, kind) {
    clone.removeAttribute("id");
    clone.classList.remove(
      "chatterino-native-voting-source",
      "chatterino-moved-banner-floating",
      "chatterino-moved-banner-inline",
      "chatterino-moved-poll-inline"
    );
    clone.classList.add(
      kind === "poll"
        ? "chatterino-moved-poll-inline"
        : "chatterino-moved-banner-inline"
    );
    clone.removeAttribute("aria-hidden");
    clone.querySelectorAll("[id]").forEach((el) => el.removeAttribute("id"));
    clone
      .querySelectorAll('[aria-hidden="true"]')
      .forEach((el) => el.removeAttribute("aria-hidden"));
  }

  function ensureVotingReplica(source, kind, orderIndex) {
    const replicaId = `chatterino-${kind}-replica`;
    let replica = document.getElementById(replicaId);
    if (!replica) {
      replica = document.createElement("div");
      replica.id = replicaId;
      replica.className = `chatterino-voting-replica chatterino-${kind}-replica`;
      replica.addEventListener(
        "click",
        (event) => {
          if (event.target.closest(".chatterino-prediction-minimize-btn")) {
            return;
          }
          const cloneRoot = replica.firstElementChild;
          const sourceRoot = replica.__ccSource;
          const activated = window.ChatterinoVotingUi?.forwardActivation(
            event,
            cloneRoot,
            sourceRoot
          );
          if (activated) {
            if (kind === "prediction") {
              schedulePredictionDialogPositioning(replica);
            }
            requestAnimationFrame(() => scheduleSync());
            setTimeout(scheduleSync, 100);
          } else if (kind === "prediction") {
            // A source detached by a Twitch re-render (or SPA navigation)
            // swallows the click; fall back to the live native trigger.
            if (
              sourceRoot?.isConnected &&
              typeof sourceRoot.click === "function"
            ) {
              sourceRoot.click();
            } else {
              window.ChatterinoVotingUi?.activateVotingTrigger(
                document,
                "prediction",
                replica
              );
            }
            schedulePredictionDialogPositioning(replica);
            requestAnimationFrame(() => scheduleSync());
            setTimeout(scheduleSync, 100);
          }
        },
        true
      );
    }

    source.classList.remove(
      "chatterino-moved-banner-inline",
      "chatterino-moved-poll-inline"
    );
    source.classList.add("chatterino-native-voting-source");
    replica.__ccSource = source;

    const sourceSignature = source.outerHTML;
    if (replica.__ccSourceSignature !== sourceSignature) {
      replica.__ccSourceSignature = sourceSignature;
      const clone = source.cloneNode(true);
      sanitizeVotingClone(clone, kind);
      replica.replaceChildren(clone);
    }

    mountInSlot(replica, orderIndex);
    return replica;
  }

  function releaseVotingSource(source) {
    if (source?.isConnected) {
      source.classList.remove("chatterino-native-voting-source");
    }
  }

  function ensureMinimizedIcon(banner) {
    let minIcon = document.getElementById("chatterino-prediction-min-icon");
    if (!minIcon) {
      minIcon = document.createElement("button");
      minIcon.id = "chatterino-prediction-min-icon";
      minIcon.className = "chatterino-prediction-minimized-icon";
      minIcon.title = "Expand Prediction";
      minIcon.type = "button";
      minIcon.innerHTML = `
        <svg width="18" height="18" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round">
          <circle cx="12" cy="9" r="7"/>
          <path d="M17 16l-2 4H9l-2-4"/>
          <path d="M8 20h8"/>
          <path d="M12 6a2 2 0 0 1 2 2"/>
        </svg>
      `;
      minIcon.addEventListener("click", () => {
        isMinimized = false;
        updateDisplayState(banner);
      });
    }
    mountInSlot(minIcon, 1);
    minIcon.style.setProperty(
      "display",
      isMinimized ? "inline-flex" : "none",
      "important"
    );
    return minIcon;
  }

  function handlePredictionBanner(source, channelName, pointsText) {
    domPredictionLastSeen = Date.now();
    document.getElementById("chatterino-prediction-fallback")?.remove();

    const details = mergeVotingDetails(
      "prediction",
      parseBannerDetails(source)
    );
    if (details.title && details.title !== lastPredictionTitle) {
      lastPredictionTitle = details.title;
      isMinimized = false;
    }

    sendPredictionMessage(channelName);
    // Voting is over once Twitch reports the prediction locked or ended. Only
    // trust that when GraphQL confirmed it: the DOM text heuristic can misread
    // a title containing "won't".
    if (
      details?.source === "dom+graphql" &&
      (details.status === "locked" || details.status === "ended")
    ) {
      document.getElementById("chatterino-prediction-replica")?.remove();
      document.getElementById("chatterino-prediction-min-icon")?.remove();
      return;
    }
    if (activePredictionSource && activePredictionSource !== source) {
      releaseVotingSource(activePredictionSource);
    }
    activePredictionSource = source;
    const banner = ensureVotingReplica(source, "prediction", 1);
    activeBanner = banner;

    let minBtn = banner.querySelector(".chatterino-prediction-minimize-btn");
    if (!minBtn) {
      minBtn = document.createElement("button");
      minBtn.className = "chatterino-prediction-minimize-btn";
      minBtn.textContent = "—";
      minBtn.title = "Minimize";
      minBtn.addEventListener("click", (e) => {
        e.stopPropagation();
        e.preventDefault();
        isMinimized = true;
        updateDisplayState(banner);
      });
      banner.appendChild(minBtn);
    }

    ensureMinimizedIcon(banner);
    updateDisplayState(banner);

    if (pointsText) {
      let badge = banner.querySelector(".chatterino-points-badge");
      if (!badge) {
        badge = document.createElement("div");
        badge.className = "chatterino-points-badge";
        badge.style.position = "absolute";
        badge.style.top = "8px";
        badge.style.right = "40px";
        badge.style.background = "#9146ff";
        badge.style.color = "#ffffff";
        badge.style.padding = "3px 8px";
        badge.style.borderRadius = "4px";
        badge.style.fontSize = "11px";
        badge.style.fontWeight = "bold";
        badge.style.zIndex = "100000";
        badge.style.pointerEvents = "none";
        badge.style.boxShadow = "0 2px 6px rgba(0,0,0,0.3)";
        banner.appendChild(badge);
        if (getComputedStyle(banner).position === "static") {
          banner.style.position = "relative";
        }
      }
      badge.textContent = `${pointsText} pts`;
    }
  }

  function handlePredictionFallback(channelName) {
    activityStore.removeDom("prediction");
    activityStore.applyGraphql(gqlState);
    const prediction = activityStore.current("prediction");
    if (
      !prediction?.title ||
      prediction.status === "ended" ||
      prediction.status === "locked"
    ) {
      document.getElementById("chatterino-prediction-fallback")?.remove();
      sendPredictionMessage(channelName);
      return;
    }

    let pill = document.getElementById("chatterino-prediction-fallback");
    if (!pill) {
      pill = document.createElement("button");
      pill.id = "chatterino-prediction-fallback";
      pill.type = "button";
      pill.className = "chatterino-prediction-vote-pill";
      pill.innerHTML =
        '<span class="dot"></span><span class="label"></span>';
      pill.addEventListener("click", (e) => {
        e.preventDefault();
        e.stopPropagation();
        schedulePredictionDialogPositioning(pill);
        if (
          window.ChatterinoVotingUi?.activateVotingTrigger(
            document,
            "prediction",
            pill
          )
        ) {
          scheduleSync();
          return;
        }
        // No native outcome control is reachable (companion mode hid the
        // banner). Twitch answers the channel-points button with the bet
        // prompt while a prediction is accepting entries, so the points
        // replica opens the voting menu.
        const pointsReplica = document.getElementById(
          "chatterino-points-replica"
        );
        if (pointsReplica) {
          pointsReplica.click();
        }
        schedulePredictionDialogPositioning(pill);
      });
    }

    pill.querySelector(".label").textContent = prediction.title;
    mountInSlot(pill, 1);
    sendPredictionMessage(channelName);
  }

  function cleanupPredictionUi() {
    stopPredictionDialogWatcher();
    document.getElementById("chatterino-prediction-min-icon")?.remove();
    document.getElementById("chatterino-prediction-fallback")?.remove();
    document.getElementById("chatterino-prediction-replica")?.remove();
    releaseVotingSource(activePredictionSource);
    activePredictionSource = null;
    lastPredictionTitle = "";
    isMinimized = false;
    activeBanner = null;
  }

  function handlePollBanner(source, channelName) {
    document.getElementById("chatterino-poll-fallback")?.remove();
    const details = mergeVotingDetails("poll", parseBannerDetails(source));
    sendPollMessage(channelName);
    if (activePollSource && activePollSource !== source) {
      releaseVotingSource(activePollSource);
    }
    activePollSource = source;
    ensureVotingReplica(source, "poll", 0);
  }

  function handlePollFallback(channelName) {
    activityStore.removeDom("poll");
    activityStore.applyGraphql(gqlState);
    const poll = activityStore.current("poll");
    if (!poll?.title) {
      document.getElementById("chatterino-poll-fallback")?.remove();
      return;
    }

    let pill = document.getElementById("chatterino-poll-fallback");
    if (!pill) {
      pill = document.createElement("div");
      pill.id = "chatterino-poll-fallback";
      pill.className = "chatterino-poll-fallback-pill";
      pill.innerHTML = '<span class="dot"></span><span class="label"></span>';
    }

    pill.querySelector(".label").textContent = poll.title;
    mountInSlot(pill, 0);
    sendPollMessage(channelName);
  }

  function cleanupPollUi() {
    document.getElementById("chatterino-poll-fallback")?.remove();
    document.getElementById("chatterino-poll-replica")?.remove();
    releaseVotingSource(activePollSource);
    activePollSource = null;
  }

  function syncCompanionState() {
    if (
      document.documentElement.classList.contains("chatterino-companion-active")
    ) {
      companionActive = true;
      return;
    }
    if (isChatShellWiped()) {
      companionActive = true;
      document.documentElement.classList.add("chatterino-companion-active");
      return;
    }
    companionActive = document.documentElement.hasAttribute(
      "data-chatterino-companion-active"
    );
  }

  let lastSyncStageError = "";
  let channelResetTimers = [];

  function runSyncStage(name, stage) {
    try {
      stage();
    } catch (err) {
      // Log each distinct failure once instead of on every sync.
      const signature = `${name}: ${err?.message || err}`;
      if (signature !== lastSyncStageError) {
        lastSyncStageError = signature;
        console.warn("[Chatterino Companion] sync stage failed", signature);
      }
    }
  }

  function runSync() {
    if (isContextInvalidated()) {
      return;
    }
    if (syncInProgress) {
      scheduleSync();
      return;
    }
    syncInProgress = true;
    const syncStartedAt = window.performance?.now?.() ?? Date.now();
    observer.disconnect();
    observerAttached = false;
    try {
      syncGqlFromDomAttributes();
      syncCompanionState();

      const channelName = getTwitchChannelName();
      if (channelName !== currentChannel) {
        removeVotingMessages(currentChannel);
        domEndedExpiry.reset();
        currentChannel = channelName;
        activityStore.setChannel(channelName);
        domPointsLastSeen = 0;
        domBadgesLastSeen = 0;
        lastPinFingerprint = "";
        resetFingerprints();
        cleanupPredictionUi();
        cleanupPollUi();
        resetChannelScopedUi();
        window.dispatchEvent(
          new CustomEvent("chatterino-companion-channel-change", {
            detail: { channel: channelName },
          })
        );
        scheduleClaimBootstrap();
        // Republish shortly after a channel change. Timers of a previous,
        // quickly abandoned channel are cancelled first.
        channelResetTimers.forEach(clearTimeout);
        channelResetTimers = [1000, 2000, 5000].map((delay) =>
          setTimeout(resetFingerprints, delay)
        );
      }

      scheduleAutoClaimAttempt();
      // Each stage is isolated: a Twitch markup change that makes one of
      // them throw must not skip the remaining stages (voting replicas,
      // dialog positioning, portal placement) on every single sync.
      runSyncStage("pinned", () => handlePinnedMessages(channelName));

      runSyncStage("badges", () => {
        if (findChatBadgeCarousel() || findNativeChatBadgeButton()) {
          domBadgesLastSeen = Date.now();
          ensureBadgeReplica();
        } else if (channelName === "") {
          removeBadgeReplica();
        } else if (document.getElementById("chatterino-badge-replica")) {
          ensureBadgeReplica();
        }
      });

      let pointsText = gqlState?.channelPoints?.balance || "";
      runSyncStage("points", () => {
        const pointsSummary = findPointsSummary();
        if (pointsSummary) {
          domPointsLastSeen = Date.now();
        }

        pointsText =
          scrapePointsText(pointsSummary) ||
          gqlState?.channelPoints?.balance ||
          "";
        if (pointsSummary) {
          ensurePointsReplica();
        } else if (channelName === "") {
          removePointsReplica();
        } else {
          // Native summary was destroyed (e.g. Chatterino chat wipe) — keep the
          // existing clone alive and refresh its balance from GQL data.
          updateReplicaBalanceFromGql();
        }
      });

      runSyncStage("poll", () => {
        const pollBanner = findPollBanner();
        if (pollBanner) {
          handlePollBanner(pollBanner, channelName);
        } else {
          activityStore.removeDom("poll");
          cleanupPollUi();
          if (
            gqlState?.poll?.title &&
            (companionActive || isChatShellWiped() || channelName)
          ) {
            handlePollFallback(channelName);
          } else {
            sendPollMessage(channelName);
          }
        }
      });

      runSyncStage("prediction", () => {
        const banner = findBanner();
        if (banner) {
          handlePredictionBanner(banner, channelName, pointsText);
        } else {
          activityStore.removeDom("prediction");
          cleanupPredictionUi();
          if (
            gqlState?.prediction?.title &&
            (companionActive || isChatShellWiped() || channelName)
          ) {
            handlePredictionFallback(channelName);
          } else {
            document.getElementById("chatterino-prediction-fallback")?.remove();
            sendPredictionMessage(channelName);
          }
        }
      });

      runSyncStage("dialogs", () => {
        const openRewardDialog = findNativeRewardDialog();
        if (openRewardDialog && openRewardDialog.isConnected) {
          positionNativeRewardDialog();
        } else {
          document
            .querySelectorAll(".chatterino-native-reward-dialog")
            .forEach((el) => {
              unmarkRewardDialog(el);
            });
        }
        const openPredictionDialog = findNativePredictionDialog();
        if (openPredictionDialog && openPredictionDialog.isConnected) {
          positionNativePredictionDialog();
        } else {
          document
            .querySelectorAll(".chatterino-native-prediction-dialog")
            .forEach((el) => {
              unmarkPredictionDialog(el);
            });
        }
        const openIdentityDialog = findNativeChatIdentityDialog();
        if (!openIdentityDialog || !openIdentityDialog.isConnected) {
          document.querySelectorAll(".chatterino-native-chat-identity-dialog").forEach((el) => {
            el.classList.remove("chatterino-native-chat-identity-dialog");
          });
        }
      });
      runSyncStage("reward-pending", checkNativeRewardPending);

      runSyncStage("portal", positionToolbarPortal);
    } finally {
      recordObserverCallback(syncStartedAt);
      syncInProgress = false;
      observeTarget();
    }
  }

  chrome.storage.local.get({ autoClaimEnabled: true }, (items) => {
    autoClaimEnabled = items.autoClaimEnabled;
  });

  chrome.storage.onChanged.addListener((changes) => {
    if (changes.autoClaimEnabled) {
      autoClaimEnabled = changes.autoClaimEnabled.newValue;
    }
  });

  document.addEventListener("visibilitychange", () => {
    if (document.visibilityState === "visible") {
      resetFingerprints();
      scheduleSync();
    }
  });

  window.addEventListener("focus", () => {
    resetFingerprints();
    scheduleSync();
  });

  document.addEventListener("fullscreenchange", scheduleSync);
  document.addEventListener("webkitfullscreenchange", scheduleSync);

  window.addEventListener("chatterino-companion-active", () => {
    companionActive = true;
    // Re-send prediction/pin state when Chatterino attaches so mid-flight
    // predictions show up even if we already sent them earlier.
    resetFingerprints();
    scheduleSync();
  });

  window.addEventListener("chatterino-companion-gql", (event) => {
    const prevClaimAvailable = gqlState?.channelPoints?.claimAvailable;
    const snapshot = window.ChatterinoPageData.sanitizeSnapshot(event.detail);
    if (!snapshot) {
      return;
    }
    gqlState = snapshot;
    activityStore.applyGraphql(gqlState);
    if (gqlState?.channelPoints?.claimAvailable) {
      scheduleAutoClaimAttempt();
    } else if (prevClaimAvailable) {
      markClaimSucceeded();
    }
    scheduleSync();
  });

  window.addEventListener("chatterino-companion-dismiss-reward-dialog", () => {
    scheduleDismissUnintendedRewardDialog();
  });

  window.addEventListener("chatterino-companion-reward-pending", (event) => {
    forwardRewardPending(event.detail);
  });

  window.addEventListener("chatterino-companion-reward-cancelled", (event) => {
    forwardRewardClear(event.detail?.reason || "cancelled");
  });

  window.addEventListener(
    "chatterino-companion-minute-watched-result",
    (event) => {
      // The page can dispatch this event itself, so only known fields of the
      // expected types are relayed and `action` cannot be overridden.
      const detail = event.detail || {};
      const text = (value, max) =>
        typeof value === "string" ? value.slice(0, max) : "";
      safeSendMessage({
        ok: detail.ok === true,
        statusCode: Number.isInteger(detail.statusCode) ? detail.statusCode : 0,
        error: text(detail.error, 200),
        requestId: text(detail.requestId, 128),
        channel: text(detail.channel, 64),
        streamId: text(detail.streamId, 64),
        sessionId: text(detail.sessionId, 128),
        action: "minuteWatchedResult",
      });
    }
  );

  chrome.runtime.onMessage.addListener((message, _sender, sendResponse) => {
    if (message?.action === "nativeAttachState") {
      sendResponse({ ok: true });
      return true;
    }
    if (message?.action === "getIntegrationHealth") {
      sendResponse({
        channel: currentChannel,
        companionActive,
        fullscreen: window.ChatterinoVotingUi.isFullscreenActive(
          document,
          window
        ),
        graphqlUpdatedAt: Number(
          document.documentElement.getAttribute("data-cc-gql-updated") || 0
        ),
        activities: activityStore.snapshot(),
        // When Twitch changes its markup, these stop updating first.
        detected: {
          points: domPointsLastSeen,
          badges: domBadgesLastSeen,
        },
      });
      return true;
    }
    if (message?.action === "sendNativeChat") {
      const result = sendNativeChatMessage(message.message);
      if (result.ok) {
        forwardRewardClear("sent");
      }
      sendResponse(result);
      return true;
    }
    if (message?.action === "sendMinuteWatched") {
      window.dispatchEvent(
        new CustomEvent("chatterino-companion-minute-watched", {
          detail: message,
        })
      );
      sendResponse({ ok: true });
      return true;
    }
    if (message?.action === "nativeChatResult") {
      // Results are session-exact and observable by optional UI/plugin layers;
      // do not clear the user's draft unless the native side reports a
      // definitive success.
      window.dispatchEvent(
        new CustomEvent("chatterino-native-chat-result", {
          detail: {
            sessionId: message.sessionId,
            requestId: message.requestId,
            generation: message.generation,
            status: message.status,
            reason: message.reason,
          },
        })
      );
      sendResponse({ ok: true });
      return true;
    }
    return false;
  });

  syncIntervalId = setInterval(resetFingerprints, 10000);
  pollIntervalId = setInterval(scheduleSync, SAFETY_POLL_MS);

  // Chat lines are added and trimmed constantly (also while the restored
  // native chat is hidden in companion mode). None of the synced controls live
  // inside the message list, so those records must not drive a full sync.
  const CHAT_MESSAGE_LIST_SELECTOR = [
    '[data-test-selector="chat-scrollable-area__message-container"]',
    ".chat-scrollable-area__message-container",
    ".chat-list__lines",
    '[role="log"]',
    // 7TV / FFZ replace Twitch's list with their own containers, and typing in
    // the chat input mutates the editor on every keystroke.
    ".seventv-chat-list",
    ".ffz--chat-list",
    '[data-a-target="chat-input"]',
    ".chat-input__textarea",
  ].join(",");

  function isRelevantSyncMutation(record) {
    const target = record?.target;
    if (!target || target.nodeType !== 1 || typeof target.closest !== "function") {
      return true;
    }
    return !target.closest(CHAT_MESSAGE_LIST_SELECTOR);
  }

  const observer = new MutationObserver((records) => {
    measureObserverWork(() => {
      if (records.some(isRelevantSyncMutation)) {
        scheduleSync();
      }
    });
  });

  let shellObserved = false;
  let streamChatObserved = false;
  let observedShell = null;
  let observedStreamChat = null;

  // runSync disconnects the observer while it works. The old version only
  // observed when the "already observed" flags were false, and the flags were
  // never cleared, so after the first sync the observer stayed disconnected
  // for good (only the 2 s safety poll was left) and a .chat-shell replaced by
  // SPA navigation was never observed. Re-attach to the current nodes instead.
  const observeTarget = () => {
    if (contextTornDown) {
      return;
    }
    const shell = document.querySelector(".chat-shell");
    const streamChat = document.querySelector(".stream-chat");
    if (
      observerAttached &&
      shell === observedShell &&
      streamChat === observedStreamChat
    ) {
      return;
    }
    observer.disconnect();
    observedShell = shell;
    observedStreamChat = streamChat;
    if (shell) {
      observer.observe(shell, { childList: true, subtree: true });
    }
    if (streamChat && !shell?.contains(streamChat)) {
      observer.observe(streamChat, { childList: true, subtree: true });
    }
    shellObserved = Boolean(shell);
    streamChatObserved = Boolean(streamChat);
    observerAttached = Boolean(shell || streamChat);
  };

  observeTarget();
  let bootstrapObserveTimer = null;
  const bootstrapObserver = new MutationObserver(() => {
    const startedAt = window.performance?.now?.() ?? Date.now();
    if (shellObserved && streamChatObserved) {
      return;
    }
    if (bootstrapObserveTimer) {
      return;
    }
    bootstrapObserveTimer = setTimeout(() => {
      bootstrapObserveTimer = null;
      observeTarget();
      if (shellObserved && streamChatObserved) {
        bootstrapObserver.disconnect();
      }
    }, 1000);
    recordObserverCallback(startedAt);
  });
  bootstrapObserver.observe(document.documentElement, {
    childList: true,
    subtree: true,
  });

  let claimBonusDebounceTimer = null;
  const claimBonusObserver = new MutationObserver(() => {
    const startedAt = window.performance?.now?.() ?? Date.now();
    if (claimBonusDebounceTimer !== null) {
      return;
    }
    claimBonusDebounceTimer = setTimeout(() => {
      claimBonusDebounceTimer = null;
      if (findClaimBonusButton()) {
        scheduleAutoClaimAttempt();
      }
    }, 250);
    recordObserverCallback(startedAt);
  });
  claimBonusObserver.observe(document.documentElement, {
    childList: true,
    subtree: true,
  });

  scheduleClaimBootstrap();
  scheduleSync();
  console.log("[Chatterino Companion] Extension script active.");
})();
