(function (global) {
  "use strict";

  const NOTIFICATIONS_ROOT_SELECTOR = [
    ".onsite-notifications",
    '[data-test-selector="onsite-notifications"]',
    '[data-test-selector="onsite-notifications-toast-manager"]',
  ].join(",");

  function findRoot(document) {
    const matched = document?.querySelector?.(NOTIFICATIONS_ROOT_SELECTOR);
    return matched?.closest?.(".onsite-notifications") || matched || null;
  }

  function findToggle(node) {
    const button = node?.closest?.("button");
    if (!button) {
      return null;
    }
    const label = String(button.getAttribute?.("aria-label") || "");
    if (!/^\s*(?:open|close)\s+notifications\s*$/i.test(label)) {
      return null;
    }
    return button.closest?.(NOTIFICATIONS_ROOT_SELECTOR) ? button : null;
  }

  function findPanel(document) {
    const root = findRoot(document);
    if (!root) {
      return null;
    }

    const semanticPanel = root.querySelector?.('[role="dialog"], [role="menu"]');
    if (semanticPanel) {
      return semanticPanel;
    }

    // Twitch mounts the notification balloon into the detector's sibling.
    // Prefer its first child because the sibling itself can be a layout host.
    const detector = root.querySelector?.(
      '[data-test-selector="toggle-balloon-wrapper__mouse-enter-detector"]'
    );
    const balloonHost = Array.from(
      detector?.parentElement?.children || []
    ).find((child) => child !== detector);
    if (
      !balloonHost ||
      (!balloonHost.childElementCount &&
        !String(balloonHost.textContent || "").trim())
    ) {
      return null;
    }
    return balloonHost.firstElementChild || balloonHost;
  }

  function clearPanelShift(panel) {
    if (!panel) {
      return;
    }
    panel.style?.removeProperty?.("translate");
    if (panel.dataset) {
      delete panel.dataset.ccNotificationShift;
    }
  }

  function positionPanelLeftOf(panel, obstructionRect, gap = 12) {
    if (!panel || !Number.isFinite(obstructionRect?.left)) {
      return 0;
    }

    const renderedRight = panel.getBoundingClientRect?.().right;
    if (!Number.isFinite(renderedRight)) {
      return 0;
    }

    // The bounding rect already contains our previous translation. Restore
    // its unshifted right edge before recalculating so repeated DOM mutations
    // cannot compound the offset.
    const previousShift = Number(panel.dataset?.ccNotificationShift) || 0;
    const unshiftedRight = renderedRight + previousShift;
    const shift = Math.max(
      0,
      Math.ceil(unshiftedRight - (obstructionRect.left - gap))
    );

    if (!shift) {
      clearPanelShift(panel);
      return 0;
    }

    panel.style?.setProperty?.("translate", `${-shift}px 0px`, "important");
    if (panel.dataset) {
      panel.dataset.ccNotificationShift = String(shift);
    }
    return shift;
  }

  function toggleWillOpen(button) {
    const label = String(button?.getAttribute?.("aria-label") || "");
    return /^\s*open\s+notifications\s*$/i.test(label);
  }

  function isNotificationsSurface(element) {
    return Boolean(element?.closest?.(NOTIFICATIONS_ROOT_SELECTOR));
  }

  function isOpen(document) {
    const root = findRoot(document);
    if (!root) {
      return false;
    }

    if (
      root.querySelector?.('button[aria-label="Close Notifications" i]') ||
      root.querySelector?.(
        'button[aria-label*="Notifications" i][aria-expanded="true"]'
      ) ||
      root.querySelector?.('[role="dialog"], [role="menu"]')
    ) {
      return true;
    }

    // Twitch's toggle-balloon wrapper keeps an empty sibling host while the
    // inbox is closed and mounts the balloon into that host when it opens.
    const detector = root.querySelector?.(
      '[data-test-selector="toggle-balloon-wrapper__mouse-enter-detector"]'
    );
    const balloonHost = Array.from(
      detector?.parentElement?.children || []
    ).find((child) => child !== detector);
    return Boolean(
      balloonHost &&
      (balloonHost.childElementCount > 0 ||
        String(balloonHost.textContent || "").trim())
    );
  }

  global.ChatterinoNotificationUi = {
    clearPanelShift,
    findPanel,
    findRoot,
    findToggle,
    isNotificationsSurface,
    isOpen,
    positionPanelLeftOf,
    toggleWillOpen,
  };
})(window);
