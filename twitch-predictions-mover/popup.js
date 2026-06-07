document.addEventListener('DOMContentLoaded', () => {
  const autoClaimCheckbox = document.getElementById('autoClaimCheckbox');
  const topRange = document.getElementById('topRange');
  const leftRange = document.getElementById('leftRange');
  const topVal = document.getElementById('topVal');
  const leftVal = document.getElementById('leftVal');
  const resetBtn = document.getElementById('resetBtn');

  // Load saved settings
  chrome.storage.local.get({
    autoClaimEnabled: true,
    floatingTop: '120px',
    floatingLeft: '20px'
  }, (items) => {
    autoClaimCheckbox.checked = items.autoClaimEnabled;

    // Convert e.g. "120px" -> 120
    const topInt = parseInt(items.floatingTop) || 120;
    const leftInt = parseInt(items.floatingLeft) || 20;

    topRange.value = topInt;
    leftRange.value = leftInt;

    topVal.textContent = topInt + 'px';
    leftVal.textContent = leftInt + 'px';
  });

  // Save checkbox change
  autoClaimCheckbox.addEventListener('change', () => {
    chrome.storage.local.set({
      autoClaimEnabled: autoClaimCheckbox.checked
    });
  });

  // Save range changes
  topRange.addEventListener('input', () => {
    const val = topRange.value + 'px';
    topVal.textContent = val;
    chrome.storage.local.set({ floatingTop: val });
  });

  leftRange.addEventListener('input', () => {
    const val = leftRange.value + 'px';
    leftVal.textContent = val;
    chrome.storage.local.set({ floatingLeft: val });
  });

  // Reset button action
  resetBtn.addEventListener('click', () => {
    const defaultTop = '120px';
    const defaultLeft = '20px';

    topRange.value = 120;
    leftRange.value = 20;
    topVal.textContent = defaultTop;
    leftVal.textContent = defaultLeft;

    chrome.storage.local.set({
      floatingTop: defaultTop,
      floatingLeft: defaultLeft
    });
  });
});
