document.addEventListener('DOMContentLoaded', () => {
  const replaceTwitchCheckbox = document.getElementById('replaceTwitchCheckbox');
  const autoClaimCheckbox = document.getElementById('autoClaimCheckbox');
  const debugPanel = document.getElementById('debugPanel');
  const healthDot = document.getElementById('healthDot');
  const healthHeadline = document.getElementById('healthHeadline');

  function refreshIntegrationHealth() {
    chrome.runtime.sendMessage({ type: 'get-integration-health' }, (health) => {
      if (chrome.runtime.lastError || !health) {
        health = {
          native: {
            connected: false,
            lastError: chrome.runtime.lastError?.message || 'Background service unavailable'
          }
        };
      }
      const summary = globalThis.ChatterinoIntegrationHealth.summarize(health);
      healthHeadline.textContent = summary.headline;
      healthDot.className = `health-dot ${summary.level}`;
      debugPanel.textContent = summary.lines.join('\n');
    });
  }

  chrome.storage.local.get({ autoClaimEnabled: true }, (items) => {
    autoClaimCheckbox.checked = items.autoClaimEnabled;
  });

  chrome.runtime.getPlatformInfo((platform) => {
    if (platform.os === 'win') {
      document.body.classList.add('chatterino-windows');
      chrome.runtime.sendMessage({
        type: 'get-setting',
        key: 'replaceTwitchChat'
      }, (replaceTwitchChat) => {
        if (replaceTwitchCheckbox) {
          replaceTwitchCheckbox.checked = Boolean(replaceTwitchChat);
        }
      });
    }
  });

  if (replaceTwitchCheckbox) {
    replaceTwitchCheckbox.addEventListener('change', () => {
      chrome.runtime.sendMessage({
        type: 'set-setting',
        key: 'replaceTwitchChat',
        value: replaceTwitchCheckbox.checked
      });
    });
  }

  autoClaimCheckbox.addEventListener('change', () => {
    chrome.storage.local.set({
      autoClaimEnabled: autoClaimCheckbox.checked
    });
  });

  chrome.storage.local.onChanged.addListener((changes) => {
    if (changes.replaceTwitchChat && replaceTwitchCheckbox) {
      replaceTwitchCheckbox.checked = changes.replaceTwitchChat.newValue;
    }
  });

  refreshIntegrationHealth();
  const healthTimer = setInterval(refreshIntegrationHealth, 2000);
  window.addEventListener('unload', () => clearInterval(healthTimer), { once: true });
});
