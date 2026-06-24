(function () {
  'use strict';

  const PLACEHOLDER_MARKERS = [
    'Chatterino should show here',
    'Connection to the Chatterino extension lost',
    'Chatterino also needs to be running'
  ];

  function isChatterinoWipe(html) {
    if (typeof html !== 'string') {
      return false;
    }
    return PLACEHOLDER_MARKERS.some((marker) => html.includes(marker));
  }

  function isChatShellWiped() {
    const shell = document.querySelector('.chat-shell');
    const text = shell?.children[0]?.innerText || '';
    return PLACEHOLDER_MARKERS.some((marker) => text.includes(marker));
  }

  function activateCompanionMode(reason) {
    document.documentElement.classList.add('chatterino-companion-active');
    document.documentElement.setAttribute('data-chatterino-companion-active', reason || '1');
    window.dispatchEvent(new CustomEvent('chatterino-companion-active', { detail: { reason } }));
  }

  // The Chatterino Native Host extension wipes the chat with
  // `chatShell.children[0].innerHTML = '<placeholder>'` from its own isolated
  // world, which we cannot intercept. But the wiped React-managed nodes are
  // still alive inside the MutationRecord — restore them (hidden) into their
  // original parent so Twitch React stays mounted and the channel points /
  // chat badge carousel buttons keep working.
  function restoreWipedChat(target, removedNodes) {
    let restored = false;
    for (const node of removedNodes) {
      if (node.nodeType !== 1 || node.isConnected) {
        continue;
      }
      node.classList.add('chatterino-cc-restored');
      target.appendChild(node);
      restored = true;
    }
    if (!restored) {
      return false;
    }
    for (const child of target.children) {
      if (!child.classList.contains('chatterino-cc-restored')) {
        // Chatterino's placeholder — overlay it so it doesn't affect layout.
        child.classList.add('chatterino-cc-placeholder');
      }
    }
    console.log('[Chatterino] Restored wiped chat nodes (hidden) — companion mode active');
    return true;
  }

  function handleWipeMutations(mutations) {
    for (const mutation of mutations) {
      if (
        mutation.type !== 'childList' ||
        mutation.removedNodes.length === 0 ||
        !(mutation.target instanceof Element) ||
        !mutation.target.closest('.chat-shell')
      ) {
        continue;
      }
      // Only react to the wipe mutation itself (placeholder added in the same
      // record) — never to React's own routine node removals.
      const addedWipe = [...mutation.addedNodes].some(
        (n) => n.nodeType === 1 && isChatterinoWipe(n.outerHTML || '')
      );
      if (!addedWipe) {
        continue;
      }
      if (restoreWipedChat(mutation.target, mutation.removedNodes)) {
        activateCompanionMode('wipe-restored');
      }
    }
  }

  function tryProtectChatShell() {
    if (isChatShellWiped()) {
      activateCompanionMode('wipe-detected');
    }
  }

  function injectPageScript(file) {
    try {
      const script = document.createElement('script');
      script.src = chrome.runtime.getURL(file);
      script.onload = () => script.remove();
      (document.head || document.documentElement).appendChild(script);
    } catch (e) {
      console.error('[Chatterino] Failed to inject page script:', file, e);
    }
  }

  injectPageScript('page-inject.js');
  injectPageScript('twitch-api.js');

  const observer = new MutationObserver((mutations) => {
    handleWipeMutations(mutations);
    tryProtectChatShell();
  });

  observer.observe(document.documentElement, { childList: true, subtree: true });
  tryProtectChatShell();

  // NOTE: Native points and chat-badge elements stay in place. Moving them out
  // of the React root detaches Twitch's delegated handlers. content.js renders
  // replicas in the toolbar instead.

  window.addEventListener('chatterino-companion-active', () => {
    if (chrome?.runtime?.id) {
      chrome.storage.local.set({ companionActive: true, lastWipeBlocked: Date.now() });
    }
  });
})();
