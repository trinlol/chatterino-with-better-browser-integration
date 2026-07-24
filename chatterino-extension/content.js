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
  let domPredictionLastSeen = 0;
  let activeBanner = null;
  let activePredictionSource = null;
  let activePollSource = null;
  let syncScheduled = false;
  let syncPending = false;
  let syncTimer = null;
  let lastSyncAt = 0;
  let syncInProgress = false;
  const MIN_SYNC_INTERVAL_MS = 250;

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
    chrome.runtime.sendMessage({
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
    chrome.runtime.sendMessage({
      action: "rewardClear",
      channel: channelName,
      reason: reason || "",
    });
  }

  function scheduleSync() {
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
    for (const btn of document.querySelectorAll(
      'button[aria-label*="Channel Points" i]'
    )) {
      if (!btn.closest("#chatterino-toolbar-portal")) {
        return btn;
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
    const clearIntentional = setInterval(() => {
      if (!findNativeRewardDialog()) {
        intentionalRewardDialogOpen = false;
        clearInterval(clearIntentional);
      }
    }, 250);
    setTimeout(() => clearInterval(clearIntentional), 120000);
    // Programmatic click bubbles through the React root, so Twitch's own
    // handler opens/closes the reward center even if the button is hidden.
    nativeBtn.click();
    const attemptPosition = (triesLeft) => {
      if (positionNativeRewardDialog(replica)) {
        startRewardDialogWatcher(replica);
        return;
      }
      if (triesLeft > 0) {
        requestAnimationFrame(() => attemptPosition(triesLeft - 1));
      }
    };
    requestAnimationFrame(() => attemptPosition(20));
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
      replica.innerHTML = sourceHtml;
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

  function stopChatIdentityDialogWatcher() {
    if (identityDialogWatchId) {
      clearInterval(identityDialogWatchId);
      identityDialogWatchId = null;
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

    setTimeout(() => {
      stopChatIdentityDialogWatcher();
    }, 60000);
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
  let intentionalRewardDialogOpen = false;
  let unintendedDismissTimer = null;
  let lastSuccessfulClaimAt = 0;
  let claimAttemptTimer = null;
  let claimBootstrapTimers = [];
  const AUTO_CLAIM_MIN_INTERVAL_MS = 30000;
  const CLAIM_BOOTSTRAP_DELAYS_MS = [0, 300, 750, 1500, 3000, 6000, 12000];

  function findNativeRewardDialog() {
    return (
      document.querySelector(
        'div[role="dialog"]:has(.reward-center__content)'
      ) ||
      document.querySelector(
        'div[role="dialog"][aria-labelledby="channel-points-reward-center-header"]'
      ) ||
      document.querySelector(
        'div[role="dialog"]:has([data-test-selector="reward-center"])'
      ) ||
      document.querySelector(
        '.ReactModal__Content:has([data-test-selector="reward-center"])'
      ) ||
      document.querySelector(
        "div.ReactModal__Content:has(.reward-center__content)"
      )
    );
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
  }

  function positionNativeRewardDialog(anchorEl) {
    const popover = findNativeRewardDialog();
    if (!popover || !anchorEl) {
      return false;
    }

    popover.classList.add("chatterino-native-reward-dialog");
    popover.style.setProperty("z-index", "2147483647", "important");
    ensureRewardDialogScrollable(popover);

    // The dialog lives inside ancestors with CSS transforms, so
    // `position: fixed` would resolve against them instead of the viewport.
    // Instead, measure where the dialog currently is on screen and shift it
    // to the anchor with a translate() delta. It is never reparented either —
    // it must stay inside the React root so the buttons inside keep working.
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
  }

  function dismissNativeRewardDialog() {
    const dialog = findNativeRewardDialog();
    if (!dialog) {
      return false;
    }
    stopRewardDialogWatcher();
    dialog.classList.remove("chatterino-native-reward-dialog");
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
    lastSuccessfulClaimAt = 0;
    stopClaimBootstrap();
    stopRewardDialogWatcher();
    stopUnintendedDismissWatcher();
    stopChatIdentityDialogWatcher();
    removeBadgeReplica();
    removePointsReplica();
    document.getElementById("chatterino-points-fallback")?.remove();
    findNativeRewardDialog()?.classList.remove(
      "chatterino-native-reward-dialog"
    );
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

    setTimeout(() => {
      stopRewardDialogWatcher();
    }, 60000);
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
    const balance = root.getAttribute("data-cc-gql-balance");
    const claim = root.getAttribute("data-cc-gql-claim");
    const claimId = root.getAttribute("data-cc-gql-claim-id");
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
      if (predRaw) {
        try {
          gqlState.prediction = JSON.parse(predRaw);
        } catch (_) {
          gqlState.prediction = {
            title: predRaw,
            options: [],
            status: "started",
          };
        }
      } else {
        gqlState.prediction = null;
      }
    }

    if (hasPoll) {
      if (pollRaw) {
        try {
          gqlState.poll = JSON.parse(pollRaw);
        } catch (_) {
          gqlState.poll = { title: pollRaw, options: [], status: "started" };
        }
      } else {
        gqlState.poll = null;
      }
    }

    if (rewardsRaw) {
      try {
        gqlState.rewards = JSON.parse(rewardsRaw);
      } catch (_) {
        gqlState.rewards = [];
      }
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

    window.addEventListener("scroll", () => positionToolbarPortal(), true);
    window.addEventListener("resize", () => positionToolbarPortal());

    return portal;
  }

  function positionToolbarPortal() {
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
    positionToolbarPortal();
    return document.getElementById("chatterino-toolbar-slot");
  }

  function mountInSlot(element, orderIndex) {
    const slot = getToolbarSlot();
    if (!slot || !element) {
      return;
    }
    element.style.order = String(orderIndex);
    if (element.parentElement !== slot) {
      slot.appendChild(element);
    }
    positionToolbarPortal();
    requestAnimationFrame(() => {
      positionToolbarPortal();
      requestAnimationFrame(() => positionToolbarPortal());
    });
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
    if (!chrome?.runtime?.id) {
      if (pollIntervalId) {
        clearInterval(pollIntervalId);
      }
      if (syncIntervalId) {
        clearInterval(syncIntervalId);
      }
      return true;
    }
    return false;
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
        candidates.set(surface, 20);
      }
    }
    for (const surface of document.querySelectorAll(
      '[role="dialog"], [role="menu"]'
    )) {
      const text = String(surface.textContent || "").toLowerCase();
      const controls = surface.querySelectorAll(VOTING_CONTROL_SELECTOR);
      if (
        controls.length >= 2 &&
        (text.includes(kind) ||
          (kind === "prediction" && text.includes("predict")))
      ) {
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
    if (claimAttemptTimer !== null) {
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
      document.documentElement.getAttribute("data-cc-gql-claim-id");
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
      chrome.runtime.sendMessage({
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

  function mergeVotingDetails(kind, domDetails) {
    activityStore.applyGraphql(gqlState);
    return activityStore.observeDom(kind, domDetails);
  }

  function sendVotingMessage(kind, channelName) {
    const publication = activityStore.nextPublication(kind);
    if (!publication || !channelName) {
      return;
    }
    const details = publication.activity;
    chrome.runtime.sendMessage(
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
      chrome.runtime.sendMessage(
        window.ChatterinoProtocol.create("engagement", {
          lifecycle: "remove",
          kind,
          channel: channelName,
        })
      );
    }
  }

  function sendPredictionMessage(channelName) {
    sendVotingMessage("prediction", channelName);
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
          if (
            window.ChatterinoVotingUi?.forwardActivation(
              event,
              cloneRoot,
              sourceRoot
            )
          ) {
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
    if (!prediction?.title) {
      document.getElementById("chatterino-prediction-fallback")?.remove();
      return;
    }

    let pill = document.getElementById("chatterino-prediction-fallback");
    if (!pill) {
      pill = document.createElement("button");
      pill.id = "chatterino-prediction-fallback";
      pill.className = "chatterino-prediction-fallback-pill";
      pill.type = "button";
      pill.title = "Open Twitch prediction voting";
      pill.innerHTML = '<span class="dot"></span><span class="label"></span>';
      pill.addEventListener("click", () => {
        if (
          window.ChatterinoVotingUi?.activateVotingTrigger(
            document,
            "prediction",
            pill
          )
        ) {
          requestAnimationFrame(scheduleSync);
          setTimeout(scheduleSync, 100);
          setTimeout(scheduleSync, 300);
        }
      });
    }

    pill.setAttribute("aria-label", `Vote on prediction: ${prediction.title}`);
    pill.querySelector(".label").textContent = `Vote: ${prediction.title}`;
    mountInSlot(pill, 1);
    sendPredictionMessage(channelName);
  }

  function cleanupPredictionUi() {
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

  function runSync() {
    if (isContextInvalidated()) {
      return;
    }
    if (syncInProgress) {
      scheduleSync();
      return;
    }
    syncInProgress = true;
    observer.disconnect();
    try {
      syncGqlFromDomAttributes();
      syncCompanionState();

      const channelName = getTwitchChannelName();
      if (channelName !== currentChannel) {
        removeVotingMessages(currentChannel);
        currentChannel = channelName;
        activityStore.setChannel(channelName);
        domPointsLastSeen = 0;
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
        setTimeout(resetFingerprints, 1000);
        setTimeout(resetFingerprints, 2000);
        setTimeout(resetFingerprints, 5000);
      }

      scheduleAutoClaimAttempt();
      handlePinnedMessages(channelName);

      if (findChatBadgeCarousel() || findNativeChatBadgeButton()) {
        ensureBadgeReplica();
      } else if (channelName === "") {
        removeBadgeReplica();
      } else if (document.getElementById("chatterino-badge-replica")) {
        ensureBadgeReplica();
      }

      const pointsSummary = findPointsSummary();
      if (pointsSummary) {
        domPointsLastSeen = Date.now();
      }

      const pointsText =
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
          sendPredictionMessage(channelName);
        }
      }

      positionToolbarPortal();
    } finally {
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
    gqlState = event.detail;
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

  chrome.runtime.onMessage.addListener((message, _sender, sendResponse) => {
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
    return false;
  });

  syncIntervalId = setInterval(resetFingerprints, 10000);
  pollIntervalId = setInterval(scheduleSync, SAFETY_POLL_MS);

  const observer = new MutationObserver(() => {
    scheduleSync();
  });

  let shellObserved = false;
  let streamChatObserved = false;

  const observeTarget = () => {
    const shell = document.querySelector(".chat-shell");
    const streamChat = document.querySelector(".stream-chat");
    if (shell && !shellObserved) {
      observer.observe(shell, { childList: true, subtree: true });
      shellObserved = true;
    }
    if (streamChat && !streamChatObserved) {
      observer.observe(streamChat, { childList: true, subtree: true });
      streamChatObserved = true;
    }
  };

  observeTarget();
  let bootstrapObserveTimer = null;
  const bootstrapObserver = new MutationObserver(() => {
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
  });
  bootstrapObserver.observe(document.documentElement, {
    childList: true,
    subtree: true,
  });

  const claimBonusObserver = new MutationObserver(() => {
    if (findClaimBonusButton()) {
      scheduleAutoClaimAttempt();
    }
  });
  claimBonusObserver.observe(document.documentElement, {
    childList: true,
    subtree: true,
  });

  scheduleClaimBootstrap();
  scheduleSync();
  console.log("[Chatterino Companion] Extension script active.");
})();
