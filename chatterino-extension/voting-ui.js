(function (global) {
  "use strict";

  const INTERACTIVE_SELECTOR =
    'button, a, input, label, select, textarea, [role="button"], [role="radio"], [role="option"]';

  function closestInteractive(node, root) {
    let current = node;
    while (current && current !== root.parentElement) {
      if (
        typeof current.matches === "function" &&
        current.matches(INTERACTIVE_SELECTOR)
      ) {
        return current;
      }
      if (current === root) {
        break;
      }
      current = current.parentElement;
    }
    return null;
  }

  function getElementPath(node, root) {
    const path = [];
    let current = node;
    while (current && current !== root) {
      const parent = current.parentElement;
      if (!parent) {
        return null;
      }
      const index = Array.prototype.indexOf.call(parent.children, current);
      if (index < 0) {
        return null;
      }
      path.unshift(index);
      current = parent;
    }
    return current === root ? path : null;
  }

  function resolveElementPath(root, path) {
    let current = root;
    for (const index of path || []) {
      current = current?.children?.[index];
      if (!current) {
        return null;
      }
    }
    return current;
  }

  function isInteractive(node) {
    return Boolean(
      node &&
      typeof node.matches === "function" &&
      node.matches(INTERACTIVE_SELECTOR)
    );
  }

  function collectInteractiveControls(root) {
    const controls = [];
    const visit = (node) => {
      if (!node) {
        return;
      }
      if (isInteractive(node)) {
        controls.push(node);
      }
      for (const child of node.children || []) {
        visit(child);
      }
    };
    visit(root);
    return controls;
  }

  function controlLabel(control) {
    return String(
      control?.getAttribute?.("aria-label") ||
        control?.getAttribute?.("data-a-target") ||
        control?.getAttribute?.("data-test-selector") ||
        control?.textContent ||
        ""
    )
      .replace(/\s+/g, " ")
      .trim()
      .toLowerCase();
  }

  function resolveActivationTarget(replicaControl, replicaRoot, sourceRoot) {
    const path = getElementPath(replicaControl, replicaRoot);
    const pathControl = path ? resolveElementPath(sourceRoot, path) : null;
    if (isInteractive(pathControl)) {
      return pathControl;
    }

    const replicaControls = collectInteractiveControls(replicaRoot);
    const sourceControls = collectInteractiveControls(sourceRoot);
    const label = controlLabel(replicaControl);
    if (label) {
      const semanticMatch = sourceControls.find(
        (control) => controlLabel(control) === label
      );
      if (semanticMatch) {
        return semanticMatch;
      }
    }

    const controlIndex = replicaControls.indexOf(replicaControl);
    return controlIndex >= 0 ? sourceControls[controlIndex] || null : null;
  }

  function forwardActivation(event, replicaRoot, sourceRoot) {
    if (
      !event?.target ||
      !replicaRoot ||
      !sourceRoot ||
      (sourceRoot.isConnected !== undefined && !sourceRoot.isConnected)
    ) {
      return false;
    }

    const replicaControl = closestInteractive(event.target, replicaRoot);
    if (!replicaControl) {
      return false;
    }
    const sourceControl = resolveActivationTarget(
      replicaControl,
      replicaRoot,
      sourceRoot
    );
    if (!sourceControl || typeof sourceControl.click !== "function") {
      return false;
    }

    event.preventDefault?.();
    event.stopPropagation?.();
    sourceControl.click();
    return true;
  }

  function activateVotingTrigger(document, kind, excludedRoot = null) {
    const label = kind === "poll" ? "poll" : "prediction";
    const selectors = [
      `[data-a-target*="${label}" i]`,
      `[data-test-selector*="${label}" i]`,
      `button[aria-label*="${label}" i]`,
      '[role="button"]',
      "button",
    ];
    const candidates = [];
    const seen = new Set();
    for (const selector of selectors) {
      for (const candidate of document?.querySelectorAll?.(selector) || []) {
        if (seen.has(candidate)) {
          continue;
        }
        seen.add(candidate);
        candidates.push(candidate);
      }
    }

    for (const candidate of candidates) {
      if (
        candidate === excludedRoot ||
        candidate?.isConnected === false ||
        candidate?.disabled ||
        candidate?.closest?.("#chatterino-toolbar-portal")
      ) {
        continue;
      }
      const text = controlLabel(candidate);
      const matchesKind =
        text.includes(label) ||
        (label === "prediction" &&
          (text.includes("predict") || text.includes("vote"))) ||
        (label === "poll" && text.includes("vote"));
      if (!matchesKind || typeof candidate.click !== "function") {
        continue;
      }
      candidate.click();
      return true;
    }
    return false;
  }

  function isGenericVotingSurface(surface, kind) {
    if (
      !surface ||
      surface.closest?.(
        '.onsite-notifications, [data-test-selector="onsite-notifications"], [data-test-selector="onsite-notifications-toast-manager"]'
      ) ||
      global.ChatterinoNotificationUi?.isNotificationsSurface(surface)
    ) {
      return false;
    }

    const label = kind === "poll" ? "poll" : "prediction";
    const text = String(surface.textContent || "").toLowerCase();
    if (
      !text.includes(label) &&
      !(label === "prediction" && text.includes("predict"))
    ) {
      return false;
    }

    // Generic dialogs are accepted only when they expose voting semantics.
    // A button-heavy dialog which merely mentions a prediction (notably the
    // Twitch notifications inbox) must never be cloned into the toolbar.
    const votingControls = surface.querySelectorAll?.(
      '[role="radio"], [role="option"], input[type="radio"], input[type="checkbox"], [data-a-target*="poll" i], [data-a-target*="prediction" i], [data-test-selector*="poll" i], [data-test-selector*="prediction" i]'
    );
    return (votingControls?.length || 0) >= 2;
  }

  function isFullscreenActive(document, window) {
    if (document?.fullscreenElement || document?.webkitFullscreenElement) {
      return true;
    }

    const width = Number(window?.innerWidth) || 0;
    const height = Number(window?.innerHeight) || 0;
    if (width <= 0 || height <= 0) {
      return false;
    }

    const video = document?.querySelector?.("video");
    const rect = video?.getBoundingClientRect?.();
    if (!rect) {
      return false;
    }

    const horizontalCoverage = rect.width / width;
    const verticalCoverage = rect.height / height;
    return (
      horizontalCoverage >= 0.95 &&
      verticalCoverage >= 0.9 &&
      rect.left <= width * 0.025 &&
      rect.top <= height * 0.05 &&
      rect.right >= width * 0.975 &&
      rect.bottom >= height * 0.95
    );
  }

  global.ChatterinoVotingUi = {
    activateVotingTrigger,
    collectInteractiveControls,
    forwardActivation,
    getElementPath,
    isGenericVotingSurface,
    isFullscreenActive,
    resolveActivationTarget,
    resolveElementPath,
  };
})(window);
