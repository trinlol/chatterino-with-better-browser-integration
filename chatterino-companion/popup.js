document.addEventListener('DOMContentLoaded', () => {
  const autoClaimCheckbox = document.getElementById('autoClaimCheckbox');
  const debugPanel = document.getElementById('debugPanel');

  function renderDebug(status) {
    if (!status) {
      debugPanel.textContent = 'No status yet. Open a Twitch channel tab.';
      return;
    }
    debugPanel.textContent = [
      `Companion active: ${status.companionActive ? 'yes' : 'no'}`,
      `Chat wiped: ${status.chatWiped ? 'yes' : 'no'}`,
      `Toolbar mounted: ${status.toolbarMounted ? 'yes' : 'no'}`,
      `Follow button found: ${status.followButtonFound ? 'yes' : 'no'}`,
      `Points replica mounted: ${status.pointsReplicaMounted ? 'yes' : 'no'}`,
      `Native points button found: ${status.nativePointsButtonFound ? 'yes' : 'no'}`,
      `DOM points found: ${status.domPointsFound ? 'yes' : 'no'}`,
      `DOM prediction found: ${status.domPredictionFound ? 'yes' : 'no'}`,
      `GQL balance: ${status.gqlBalance ?? 'n/a'}`,
      `GQL prediction: ${status.gqlPrediction ?? 'n/a'}`,
      `Last native msg: ${status.lastNativeMessage ?? 'none'}`,
      `Updated: ${status.updatedAt ? new Date(status.updatedAt).toLocaleTimeString() : 'n/a'}`
    ].join('\n');
  }

  chrome.storage.local.get({
    autoClaimEnabled: true,
    debugStatus: null
  }, (items) => {
    autoClaimCheckbox.checked = items.autoClaimEnabled;
    renderDebug(items.debugStatus);
  });

  autoClaimCheckbox.addEventListener('change', () => {
    chrome.storage.local.set({
      autoClaimEnabled: autoClaimCheckbox.checked
    });
  });

  chrome.storage.onChanged.addListener((changes) => {
    if (changes.debugStatus) {
      renderDebug(changes.debugStatus.newValue);
    }
  });
});
