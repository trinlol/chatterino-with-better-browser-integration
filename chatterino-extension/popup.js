document.addEventListener('DOMContentLoaded', () => {
  const replaceTwitchCheckbox = document.getElementById('replaceTwitchCheckbox');
  const autoClaimCheckbox = document.getElementById('autoClaimCheckbox');

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
});
