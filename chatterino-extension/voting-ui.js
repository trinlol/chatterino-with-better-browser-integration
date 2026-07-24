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
    const path = getElementPath(replicaControl, replicaRoot);
    const sourceControl = path ? resolveElementPath(sourceRoot, path) : null;
    if (!sourceControl || typeof sourceControl.click !== "function") {
      return false;
    }

    event.preventDefault?.();
    event.stopPropagation?.();
    sourceControl.click();
    return true;
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
    forwardActivation,
    getElementPath,
    isFullscreenActive,
    resolveElementPath,
  };
})(window);
