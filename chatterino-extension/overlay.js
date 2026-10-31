(() => {
  let lastRect = {};
  let port = null;

  let settings = {};
  let installedObjects = {};
  let popupChatLink = null;
  let errorDiv = null;

  let showingChat = false;
  // Attachment is a prepare -> matching acknowledgement -> reversible hide
  // transaction. Twitch's React subtree is never replaced by this script.
  let attachment = {
    phase: "idle",
    sessionId: null,
    generation: null,
    leaseTimer: null,
    preparedAt: 0,
  };
  let notificationsSuppressed = false;
  let notificationsTogglePendingUntil = 0;
  let shiftedNotificationsPanel = null;

  const ignoredPages = {
    settings: true,
    payments: true,
    inventory: true,
    messages: true,
    subscriptions: true,
    friends: true,
    directory: true,
    videos: true,
    prime: true,
  };

  let errors = {};
  let isCollapsed = false;
  let installRetryTimer = null;
  let pathPollTimer = null;
  let notificationsObserver = null;
  let chatShellObserver = null;
  let observedChatShell = null;
  let chatShellRetry = null;
  let contextTornDown = false;

  function isContextInvalidated() {
    try {
      return !chrome?.runtime?.id;
    } catch (_err) {
      return true;
    }
  }

  // After the extension is reloaded this script is orphaned: every
  // chrome.runtime call throws "Extension context invalidated". Stop the
  // path poll and the document-wide observers instead of throwing from them
  // until the tab is closed.
  function teardownOrphanedScript() {
    if (contextTornDown) return;
    contextTornDown = true;
    clearInterval(pathPollTimer);
    clearTimeout(installRetryTimer);
    clearInterval(chatShellRetry);
    notificationsObserver?.disconnect?.();
    chatShellObserver?.disconnect?.();
  }

  function sendRuntimeMessage(message) {
    if (contextTornDown) return false;
    try {
      const result = chrome.runtime.sendMessage(message);
      result?.catch?.(() => {});
      return true;
    } catch (_err) {
      if (isContextInvalidated()) {
        teardownOrphanedScript();
      }
      return false;
    }
  }

  // return channel name if it should contain a chat or undefined
  function matchChannelName(url) {
    if (!url) return undefined;

    const match = url.match(
      /^https?:\/\/(?:www\.)?twitch\.tv\/(\w+)\/?(?:\?.*)?$/
    );

    let channelName;
    if (match && ((channelName = match[1]), !ignoredPages[channelName])) {
      return channelName;
    }

    return undefined;
  }

  let findChatDiv = () => document.getElementsByClassName("chat-shell")[0];
  let findRightColumn = () =>
    document.getElementsByClassName("channel-page__right-column")[0];
  let findInfoBar = () =>
    document.getElementsByClassName("channel-info-bar__content-right")[0];

  // logging function
  function log(obj) {
    console.log("Chatterino: ", obj);
  }

  // install events
  function installChatterino() {
    if (matchChannelName(window.location.href)) {
      showingChat = true;
      if (settings.replaceTwitchChat) {
        replaceChat();
      }
    } else {
      showingChat = false;
      sendRuntimeMessage({ type: "detach" });
    }
  }

  function replaceChat() {
    log("attempting to replace chat");

    let retry = false;

    // right column
    if (!installedObjects.rightColumn) {
      let x = findChatDiv();

      window.chatDiv = x;

      if (x != undefined && x.children.length >= 1) {
        // Do not touch the React-owned child tree here. The background will
        // send nativeAttachState(attached) only after a matching native ack.
        // Until then Twitch remains fully usable (fail-open).
        errorDiv = null;
        installedObjects.rightColumn = true;
      } else {
        retry = true;
      }
    }

    // open popup link
    /*
    if (!installedObjects.infoBar) {
      let x = findInfoBar();

      if (x != undefined &&
        !document.querySelector('#chatterino-popup-chat-link')) {
        let link = document.createElement('a');
        link.id = 'chatterino-popup-chat-link';
        link.target = '_blank';
        link.style.margin = '0 16px';
        link.style.color = '#ff9999';
        link.appendChild(
          document.createTextNode('Open popup chat (for resubs)'));

        x.appendChild(link);

        popupChatLink = link;
        updatePopupChatLink();
        installedObjects.infoBar = true;
      } else {
        retry = true;
      }
    }
    */

    // retry if needed
    if (retry) {
      // One pending retry at a time: path changes while the shell is missing
      // used to start parallel retry chains.
      clearTimeout(installRetryTimer);
      installRetryTimer = setTimeout(() => {
        installRetryTimer = null;
        installChatterino();
      }, 1000);
    } else {
      log("installed all events");
    }

    queryChatRect();
  }

  // X is relative to the window in display pixels
  function calcViewportX() {
    // Sidebars on the left can offset the viewport relative to the window
    // There is no good way to calculate that, but on Firefox we could query
    // mozInnerScreenX to calculate that.
    //
    // See https://github.com/w3c/csswg-drafts/issues/809
    if ("mozInnerScreenX" in window) {
      let x =
        window.devicePixelRatio * (window.mozInnerScreenX - window.screenX);
      // (Windows only)
      // Firefox on Windows has -8px(!) insets even when not maximized
      // (I don't know why). On Windows, only maximized windows have these
      // insets. To get to the "visible" x-offset, we thus need to subtract
      // 8px. We account for this in Chatterino already so we need to
      // counter this. So we will just subtract 8px here.
      //
      // This is not needed in fullscreen. If you go fullscreen, then the
      // `mozInnerScreenX` and `screenX` seem to differ exactly in the sidebar
      // size, no inset.
      //
      // As for the non-fullscreen case... With no scaling, zoom or sidebars
      // calculated `x` seems to be exactly 8, so it matches the fullscreen
      // case in the end.
      //
      // But when you add display scaling, everything starts drifting... With
      // 150% scaling `x` will be one of: 10.5, 11, 11.5 - depending on the
      // position of the window. Nonetheless, with -8px it still looks fine.
      if (!window.fullScreen) {
        x -= 8;
      }
      return Math.round(x);
    }
    return 0;
  }

  // query the rect of the chat
  function queryChatRect() {
    if (!showingChat || contextTornDown) {
      return;
    }

    if (
      document.fullscreenElement != null ||
      notificationsSuppressed ||
      window.ChatterinoNotificationUi?.isOpen(document)
    ) {
      sendRuntimeMessage({ type: "detach" });
      return;
    }

    let element = findChatDiv();

    if (element === undefined) {
      return;
    }
    if (element !== observedChatShell) {
      observeChatShell();
    }

    let rect = element.getBoundingClientRect();

    if (
      document.documentElement?.classList?.contains?.(
        "chatterino-reward-pending"
      )
    ) {
      const inputArea =
        document.querySelector?.('[data-a-target="chat-input"]') ||
        document.querySelector?.('[class*="chat-input"]');
      if (inputArea?.getBoundingClientRect) {
        const inputRect = inputArea.getBoundingClientRect();
        if (
          inputRect.height > 0 &&
          inputRect.top > rect.top &&
          inputRect.top < rect.bottom
        ) {
          rect = {
            x: rect.x,
            y: rect.y,
            width: rect.width,
            height: Math.max(100, Math.round(inputRect.top - rect.top)),
          };
        }
      }
    }

    /* if (
      lastRect.left == rect.left &&
      lastRect.right == rect.right &&
      lastRect.top == rect.top &&
      lastRect.bottom == rect.bottom
    ) {
      // log("skipped sending message");
      return;
    } */
    lastRect = rect;

    let data = {
      type: "chat-resized",
      rect: { x: rect.x, y: rect.y, width: rect.width, height: rect.height },
      dpr: window.devicePixelRatio,
      viewportX: calcViewportX(),
    };

    isCollapsed = rect.width == 0;

    try {
      chrome.runtime.sendMessage(data);
    } catch (err) {
      errors.sendMessage = true;
      updateErrors();
      if (isContextInvalidated()) {
        teardownOrphanedScript();
      }
    }
  }

  function sameAttachment(message) {
    if (attachment.phase === "idle" && !message.sessionId) return false;
    if (message.sessionId && attachment.sessionId) {
      if (message.sessionId !== attachment.sessionId) return false;
    }
    if (
      message.generation !== undefined &&
      attachment.generation !== null &&
      Number(message.generation) < Number(attachment.generation)
    ) {
      return false;
    }
    return true;
  }

  function revealChat(reason = "native-unavailable") {
    if (attachment.leaseTimer !== null) {
      clearTimeout(attachment.leaseTimer);
      attachment.leaseTimer = null;
    }
    attachment.phase = "revealed";
    document.documentElement.classList.remove("chatterino-companion-active");
    document.documentElement.removeAttribute(
      "data-chatterino-companion-active"
    );
    window.dispatchEvent(
      new CustomEvent("chatterino-companion-revealed", { detail: { reason } })
    );
  }

  function commitHide(message) {
    console.log('[OVERLAY-DEBUG] commitHide called with message:', message);
    if (!sameAttachment(message)) {
      console.warn(
        '[OVERLAY-DEBUG] commitHide ABORTED - sameAttachment returned false',
        JSON.stringify({
          phase: attachment.phase,
          sessionId: attachment.sessionId,
          generation: attachment.generation,
          msgSessionId: message.sessionId,
          msgGeneration: message.generation
        })
      );
      return;
    }
    console.log('[OVERLAY-DEBUG] sameAttachment check passed, proceeding with commit');
    if (attachment.leaseTimer !== null) {
      clearTimeout(attachment.leaseTimer);
      attachment.leaseTimer = null;
    }
    attachment.phase = "attached";
    if (message.sessionId) {
      attachment.sessionId = message.sessionId;
    }
    if (message.generation !== undefined) {
      attachment.generation = message.generation;
    }
    console.log('[OVERLAY-DEBUG] Phase set to "attached", dispatching chatterino-companion-active event');
    document.documentElement.classList.add("chatterino-companion-active");
    document.documentElement.setAttribute(
      "data-chatterino-companion-active",
      message.sessionId || "native-ack"
    );
    window.dispatchEvent(
      new CustomEvent("chatterino-companion-active", {
        detail: { reason: "native-ack", sessionId: message.sessionId },
      })
    );
    const leaseMs = globalThis.ChatterinoLifecycle.leaseDelay(
      message.leaseExpiresAt
    );
    if (leaseMs !== null) {
      attachment.leaseTimer = setTimeout(() => {
        if (attachment.phase === "attached") {
          queryChatRect();
          attachment.leaseTimer = setTimeout(() => {
            if (attachment.phase === "attached") {
              revealChat("lease-expired");
              sendRuntimeMessage({ type: "detach" });
            }
          }, 15000);
        } else {
          revealChat("lease-expired");
          sendRuntimeMessage({ type: "detach" });
        }
      }, Math.max(1000, leaseMs - 15000));
    }
  }
  function prepareAttachment(message = {}) {
    console.log('[OVERLAY-DEBUG] prepareAttachment called with:', {
      sessionId: message.sessionId,
      generation: message.generation
    });
    attachment.phase = "prepared";
    attachment.sessionId = message.sessionId || null;
    attachment.generation = message.generation ?? null;
    attachment.preparedAt = Date.now();
    console.log('[OVERLAY-DEBUG] Attachment state after prepare:', attachment);
  }

  function updateErrors() {
    if (!errorDiv) return;

    if (errors.osUnsupported) {
      errorDiv.innerHTML =
        "The Chatterino Native Host browser extension currently only works on Windows.";

      return;
    }

    let closeButton =
      '<div onclick="document.getElementsByClassName(`right-column__toggle-visibility`)[0].children[0].children[0].click()" style="padding:5px; left: -30px; width: 30px; height: 30px; background: #222;z-index: 100;cursor: pointer;top: 10px;position: absolute;transform: rotateZ(180deg);color: white;"><svg class="tw-icon__svg" width="100%" height="100%" version="1.1" viewBox="0 0 20 20" x="0px" y="0px"><g><path fill="#bbbbbb" d="M16 16V4h2v12h-2zM6 9l2.501-2.5-1.5-1.5-5 5 5 5 1.5-1.5-2.5-2.5h8V9H6z"></path></g></svg></div>';

    if (errors.sendMessage) {
      errorDiv.innerHTML =
        closeButton +
        "Connection to the Chatterino extension lost!<br><br>" +
        "Please reload the page.";
    } else {
      errorDiv.innerHTML =
        closeButton +
        "Chatterino should show here:<br><br>" +
        "Try deselecting and selecting the page.<br>" +
        "Chatterino also needs to be running.<br><br>" +
        "You can temporarily disable the extension in the extension.";
    }
  }

  function updatePopupChatLink() {
    if (popupChatLink !== null) {
      popupChatLink.href =
        "/popout/" + matchChannelName(window.location.href) + "/chat";
    }
  }

  log("hello there in the dev tools 👋");

  try {
    chrome.runtime.sendMessage(
      { type: "get-setting", key: "replaceTwitchChat" },
      (replaceTwitchChat) => {
        log({ replaceTwitchChat });

        settings = { replaceTwitchChat };
        installChatterino();
      }
    );
  } catch {
    errors.sendMessage = true;
    updateErrors();
  }

  // The setting is read once at load, so a later toggle from the popup would
  // otherwise leave this page in its previous mode until a manual reload. React
  // to the change directly: reveal Twitch's own chat when the integration is
  // turned off, and re-measure so it can attach again when turned back on.
  chrome.storage.onChanged.addListener((changes, area) => {
    if (area !== "local" || !changes.replaceTwitchChat) {
      return;
    }
    const enabled = Boolean(changes.replaceTwitchChat.newValue);
    settings.replaceTwitchChat = enabled;
    if (!enabled) {
      revealChat("setting-disabled");
      attachment.phase = "idle";
      attachment.sessionId = null;
      attachment.generation = null;
      sendRuntimeMessage({ type: "detach" });
      return;
    }
    installedObjects = {};
    installChatterino();
  });

  try {
    chrome.runtime.sendMessage({ type: "get-os" }, (os) => {
      // Available OS string are documented here:
      // https://developer.chrome.com/docs/extensions/reference/runtime/#type-PlatformOs
      if (os !== "win") {
        errors.osUnsupported = true;
      }

      updateErrors();
    });
  } catch {
    errors.sendMessage = true;
    updateErrors();
  }

  // event listeners
  window.addEventListener("load", () => setTimeout(queryChatRect, 1000));
  window.addEventListener("resize", () => {
    queryChatRect();
    syncNotificationsOverlay();
    setTimeout(queryChatRect, 475);
    setTimeout(syncNotificationsOverlay, 475);
  });
  window.addEventListener("focus", queryChatRect);
  window.addEventListener("mouseup", () => setTimeout(queryChatRect, 10));

  // Twitch relayouts the chat column without firing window resize (theater
  // mode, sidebar toggles, player size changes). Track the shell itself so
  // the overlay always matches its current rect.
  // Twitch replaces .chat-shell on SPA channel navigation, so the observed
  // node is re-checked on path changes and measurements instead of only once
  // at load.
  function observeChatShell() {
    if (!chatShellObserver || contextTornDown) {
      return false;
    }
    const shell = findChatDiv();
    if (!shell) {
      return false;
    }
    if (shell !== observedChatShell) {
      if (observedChatShell) {
        chatShellObserver.unobserve(observedChatShell);
      }
      chatShellObserver.observe(shell);
      observedChatShell = shell;
    }
    return true;
  }
  function ensureChatShellObserved() {
    if (!chatShellObserver || observeChatShell() || chatShellRetry !== null) {
      return;
    }
    // Shell not mounted yet - retry until it appears
    chatShellRetry = setInterval(() => {
      if (observeChatShell() || contextTornDown) {
        clearInterval(chatShellRetry);
        chatShellRetry = null;
      }
    }, 1000);
  }
  if (typeof ResizeObserver !== "undefined") {
    chatShellObserver = new ResizeObserver(() => queryChatRect());
    ensureChatShellObserved();
  }

  function syncNotificationsOverlay() {
    const notificationUi = window.ChatterinoNotificationUi;
    const isOpen = notificationUi?.isOpen(document) || false;
    if (isOpen) {
      notificationsSuppressed = true;
      sendRuntimeMessage({ type: "detach" });

      const panel = notificationUi.findPanel(document);
      const chatRect = findChatDiv()?.getBoundingClientRect?.();
      if (panel && chatRect?.width > 0) {
        if (shiftedNotificationsPanel !== panel) {
          notificationUi.clearPanelShift(shiftedNotificationsPanel);
          shiftedNotificationsPanel = panel;
        }
        notificationUi.positionPanelLeftOf(panel, chatRect, 12);
      }
      return;
    }

    notificationUi?.clearPanelShift(shiftedNotificationsPanel);
    shiftedNotificationsPanel = null;
    if (
      notificationsSuppressed &&
      Date.now() >= notificationsTogglePendingUntil
    ) {
      notificationsSuppressed = false;
      queryChatRect();
    }
  }

  document.addEventListener(
    "click",
    (event) => {
      const toggle = window.ChatterinoNotificationUi?.findToggle(event.target);
      if (!toggle) {
        return;
      }
      if (window.ChatterinoNotificationUi.toggleWillOpen(toggle)) {
        notificationsSuppressed = true;
        notificationsTogglePendingUntil = Date.now() + 1500;
        sendRuntimeMessage({ type: "detach" });
      } else {
        notificationsTogglePendingUntil = 0;
      }
      setTimeout(syncNotificationsOverlay, 0);
      setTimeout(syncNotificationsOverlay, 100);
      setTimeout(syncNotificationsOverlay, 500);
      setTimeout(syncNotificationsOverlay, 1600);
    },
    true
  );

  let notificationsSyncTimer = null;
  notificationsObserver = new MutationObserver(() => {
    if (notificationsSyncTimer !== null) return;
    notificationsSyncTimer = setTimeout(() => {
      notificationsSyncTimer = null;
      syncNotificationsOverlay();
    }, 100);
  });
  notificationsObserver.observe(document.documentElement, {
    attributes: true,
    childList: true,
    subtree: true,
    attributeFilter: ["aria-expanded", "aria-label"],
  });

  window.addEventListener("chatterino-companion-reward-pending", () => {
    queryChatRect();
    setTimeout(queryChatRect, 100);
  });
  window.addEventListener("chatterino-companion-reward-cancelled", () => {
    queryChatRect();
    setTimeout(queryChatRect, 100);
  });

  // The background script asks for a fresh measurement whenever this Twitch
  // tab becomes active. This covers tabs that finished loading while they
  // were in the background, when their initial chat-resized message is
  // correctly ignored to avoid attaching over an inactive window.
  chrome.runtime.onMessage.addListener((message) => {
    console.log('[OVERLAY-DEBUG] Message received:', message);

    if (message?.action === "requestChatRect") {
      console.log('[OVERLAY-DEBUG] Preparing attachment (requestChatRect)');
      prepareAttachment(message);
      queryChatRect();
      return;
    }
    if (message?.action === "nativeAttachState") {
      console.log('[OVERLAY-DEBUG] Native attach state:', message.state);
      console.log('[OVERLAY-DEBUG] Current attachment phase:', attachment.phase);
      console.log('[OVERLAY-DEBUG] Message sessionId:', message.sessionId, 'Attachment sessionId:', attachment.sessionId);
      console.log('[OVERLAY-DEBUG] Message generation:', message.generation, 'Attachment generation:', attachment.generation);

      if (message.state === "prepare") {
        console.log('[OVERLAY-DEBUG] Calling prepareAttachment');
        prepareAttachment(message);
      } else if (message.state === "attached") {
        console.log('[OVERLAY-DEBUG] Calling commitHide');
        const sameResult = sameAttachment(message);
        console.log('[OVERLAY-DEBUG] sameAttachment returned:', sameResult);
        if (!sameResult) {
          console.warn('[OVERLAY-DEBUG] commitHide will be SKIPPED - sameAttachment check failed!');
          console.warn(
            '[OVERLAY-DEBUG] Failure reason: ' +
              JSON.stringify({
                phase: attachment.phase,
                sessionIdMatch: message.sessionId === attachment.sessionId,
                generationMatch: message.generation === attachment.generation,
                msgSessionId: message.sessionId,
                attSessionId: attachment.sessionId,
                msgGeneration: message.generation,
                attGeneration: attachment.generation,
              })
          );
        }
        commitHide(message);
      } else if (
        ["revealed", "rejected", "lost", "detached"].includes(message.state)
      ) {
        if (sameAttachment(message) || !message.sessionId) {
          revealChat(message.reason || message.state);
        }
      }
    }
  });

  let path = location.pathname;
  pathPollTimer = setInterval(() => {
    if (contextTornDown) return;
    if (location.pathname != path) {
      path = location.pathname;
      ensureChatShellObserved();

      log("path changed");

      installedObjects = {};
      installChatterino();
      if (settings.replaceTwitchChat) {
        updatePopupChatLink();
      }
      if (matchChannelName(window.location.href)) {
        sendRuntimeMessage({ type: "location-updated" });
      }
    }
  }, 1000);
})();
