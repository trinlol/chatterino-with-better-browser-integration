(function () {
  'use strict';

  let autoClaimEnabled = true;
  let savedTop = '120px';
  let savedLeft = '20px';
  let lastPredictionFingerprint = '';
  let lastPredictionTitle = '';
  let isMinimized = false;
  let lastPinFingerprint = '';

  let pollIntervalId = null;
  let syncIntervalId = null;

  function isContextInvalidated() {
    if (!chrome?.runtime?.id) {
      if (pollIntervalId) clearInterval(pollIntervalId);
      if (syncIntervalId) clearInterval(syncIntervalId);
      return true;
    }
    return false;
  }

  function resetFingerprints() {
    if (isContextInvalidated()) return;
    lastPinFingerprint = '';
    lastPredictionFingerprint = '';
  }

  document.addEventListener('visibilitychange', () => {
    if (document.visibilityState === 'visible') {
      resetFingerprints();
    }
  });

  window.addEventListener('focus', () => {
    resetFingerprints();
  });

  // Periodically reset fingerprints to keep Chatterino in sync in case of restarts or new splits
  syncIntervalId = setInterval(resetFingerprints, 10000);

  // Sync fingerprints on startup to handle race conditions during page load/F5 reload
  setTimeout(resetFingerprints, 1000);
  setTimeout(resetFingerprints, 2000);
  setTimeout(resetFingerprints, 5000);

  const selectors = [
    '[data-test-selector="community-prediction-banner"]',
    '[data-test-selector="community-poll-banner"]',
    '.prediction-banner',
    '.gamba-prediction-status-banner'
  ];

  const pinnedSelectors = [
    '[data-a-target="chat-pinned-message"]',
    '.pinned-chat__highlight-card',
    '.pinned-chat__container',
    '.pinned-chat-list-item'
  ];

  // Load saved settings
  chrome.storage.local.get({
    autoClaimEnabled: true,
    floatingTop: '120px',
    floatingLeft: '20px'
  }, (items) => {
    autoClaimEnabled = items.autoClaimEnabled;
    savedTop = items.floatingTop;
    savedLeft = items.floatingLeft;
  });

  // Listen for storage updates
  chrome.storage.onChanged.addListener((changes) => {
    if (changes.autoClaimEnabled) {
      autoClaimEnabled = changes.autoClaimEnabled.newValue;
    }
    if (changes.floatingTop) {
      savedTop = changes.floatingTop.newValue;
    }
    if (changes.floatingLeft) {
      savedLeft = changes.floatingLeft.newValue;
    }
    updateBannerStyles();
  });

  // Apply positions to the banner (minimized icon inherits inline/toolbar layout)
  function updateBannerStyles() {
    const banner = document.querySelector('.chatterino-moved-banner-floating');
    if (banner) {
      if (savedTop) banner.style.top = savedTop;
      if (savedLeft) banner.style.left = savedLeft;
    }
  }

  // Update visibility based on minimized state
  function updateDisplayState(banner) {
    const minIcon = document.getElementById('chatterino-prediction-min-icon');
    if (isMinimized) {
      banner.style.setProperty('display', 'none', 'important');
      if (minIcon) minIcon.style.setProperty('display', 'inline-flex', 'important');
    } else {
      banner.style.removeProperty('display');
      if (minIcon) minIcon.style.setProperty('display', 'none', 'important');
    }
    updateBannerStyles();
  }

  // Check for prediction or poll banners and move them
  function checkAndMoveBanners() {
    if (isContextInvalidated()) return;
    // Auto-claim channel points
    if (autoClaimEnabled) {
      const claimButton = document.querySelector('button[aria-label="Claim Bonus"]') || 
                          document.querySelector('.claimable-bonus__icon')?.closest('button');
      if (claimButton) {
        claimButton.click();
        console.log('[Chatterino Companion] Claimed channel points!');
      }
    }

    // Scrape channel points balance
    let pointsText = '';
    const pointsSummary = document.querySelector('[data-test-selector="community-points-summary"]');
    if (pointsSummary) {
      const text = pointsSummary.innerText.replace(/[\r\n]+/g, ' ').trim();
      const match = text.match(/([\d,.]+[KMB]?)/i);
      if (match) {
        pointsText = match[1];
      }
    }

    // Pinned messages check
    let pinnedBanner = null;
    for (const selector of pinnedSelectors) {
      pinnedBanner = document.querySelector(selector);
      if (pinnedBanner) break;
    }

    let pinnedText = '';
    if (pinnedBanner) {
      const textEl = pinnedBanner.querySelector('[class*="message"]') || 
                     pinnedBanner.querySelector('[class*="text"]') || 
                     pinnedBanner.querySelector('[class*="body"]');
      if (textEl) {
        pinnedText = textEl.textContent.trim();
      } else {
        pinnedText = pinnedBanner.innerText
          .split('\n')
          .map(s => s.trim())
          .filter(s => s && !s.includes('✕') && !s.toLowerCase().includes('dismiss') && !s.toLowerCase().includes('unpin'))
          .join(' ');
      }
    }

    const pinFingerprint = JSON.stringify({ text: pinnedText });
    if (pinFingerprint !== lastPinFingerprint) {
      lastPinFingerprint = pinFingerprint;
      chrome.runtime.sendMessage({
        action: "pin",
        message: pinnedText
      });
      console.log('[Chatterino Companion] Sent pin message to background:', pinnedText);
    }

    // Find any matching banner in the DOM
    let banner = null;
    for (const selector of selectors) {
      banner = document.querySelector(selector);
      if (banner) break;
    }

    if (banner) {
      // Parse prediction details and send to background
      let title = '';
      const titleEl = banner.querySelector('[class*="title"]') || banner.querySelector('h4') || banner.querySelector('[class*="header"]');
      if (titleEl) {
        title = titleEl.textContent.trim();
      } else {
        const lines = banner.innerText.split('\n').map(s => s.trim()).filter(Boolean);
        if (lines.length > 0) {
          title = lines[0];
        }
      }

      // Reset minimized state on a new prediction
      if (title && title !== lastPredictionTitle) {
        lastPredictionTitle = title;
        isMinimized = false;
      }

      const options = [];
      const optionEls = banner.querySelectorAll('button, [class*="option"], [class*="outcome"]');
      optionEls.forEach(el => {
        const text = el.textContent.trim();
        if (text && text.length < 50 && !text.includes('✕') && !text.includes('Dismiss') && !text.includes('Delete') && !text.includes('—')) {
          if (!options.includes(text)) {
            options.push(text);
          }
        }
      });

      let status = 'started';
      const bannerText = banner.textContent.toLowerCase();
      if (bannerText.includes('submissions closed') || bannerText.includes('locked')) {
        status = 'locked';
      } else if (bannerText.includes('ended') || bannerText.includes('won') || bannerText.includes('refunded')) {
        status = 'ended';
      }

      // Parse remaining duration in seconds
      let durationSeconds = 0;
      const timerMatch = banner.textContent.match(/(\d+):(\d+)\s*(?:remaining|left)/i) || 
                         banner.textContent.match(/(\d+)\s*m\s*(\d+)\s*s/i) ||
                         banner.textContent.match(/in\s*(\d+)\s*m/i);
      
      if (timerMatch) {
        if (timerMatch[2] !== undefined) {
          durationSeconds = parseInt(timerMatch[1]) * 60 + parseInt(timerMatch[2]);
        } else {
          durationSeconds = parseInt(timerMatch[1]) * 60;
        }
      } else {
        const secMatch = banner.textContent.match(/(\d+)\s*s\s*(?:remaining|left)?/i);
        if (secMatch) {
          durationSeconds = parseInt(secMatch[1]);
        }
      }

      // Parse winner details if prediction ended
      let winner = '';
      if (status === 'ended') {
        const winnerEl = banner.querySelector('[class*="winner"]') || banner.querySelector('[class*="won"]');
        if (winnerEl) {
          winner = winnerEl.textContent.trim();
        } else {
          const lines = banner.innerText.split('\n').map(s => s.trim()).filter(Boolean);
          const winLine = lines.find(l => l.toLowerCase().includes('won') || l.toLowerCase().includes('winner') || l.toLowerCase().includes('ended'));
          if (winLine) {
            winner = winLine;
          }
        }
      }

      const fingerprint = JSON.stringify({ title, options, status, durationSeconds, winner });
      if (fingerprint !== lastPredictionFingerprint) {
        lastPredictionFingerprint = fingerprint;
        chrome.runtime.sendMessage({
          action: "prediction",
          title: title,
          options: options,
          status: status,
          duration: durationSeconds,
          winner: winner
        });
        console.log('[Chatterino Companion] Sent prediction message to background:', title, options, status, durationSeconds, winner);
      }

      // Position it as floating banner
      if (!banner.classList.contains('chatterino-moved-banner-floating')) {
        banner.classList.add('chatterino-moved-banner-floating');
        console.log('[Chatterino Companion] Positioned prediction banner as floating card.');
      }

      // Create/Update minimize button
      let minBtn = banner.querySelector('.chatterino-prediction-minimize-btn');
      if (!minBtn) {
        minBtn = document.createElement('button');
        minBtn.className = 'chatterino-prediction-minimize-btn';
        minBtn.textContent = '—';
        minBtn.title = 'Minimize';
        minBtn.addEventListener('click', (e) => {
          e.stopPropagation();
          e.preventDefault();
          isMinimized = true;
          updateDisplayState(banner);
        });
        banner.appendChild(minBtn);
      }

      // Create/Update minimized button next to the follow/subscribe button
      let minIcon = document.getElementById('chatterino-prediction-min-icon');
      
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
          'button[data-test-selector="follow-button"]',
          // Fallbacks
          '[data-a-target="subscribe-button"]',
          'button[aria-label*="Subscribe" i]',
          "button[aria-label*=\"S'abonner\" i]",
          'button[data-test-selector="subscribe-button"]'
        ];
        for (const selector of selectors) {
          const btn = document.querySelector(selector);
          if (btn) return btn;
        }
        return null;
      };

      const targetBtn = findTargetButton();

      if (targetBtn && targetBtn.parentElement) {
        if (!minIcon) {
          minIcon = document.createElement('button');
          minIcon.id = 'chatterino-prediction-min-icon';
          minIcon.className = 'chatterino-prediction-minimized-icon';
          minIcon.title = 'Expand Prediction';
          minIcon.type = 'button';
          minIcon.innerHTML = `
            <svg width="18" height="18" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round">
              <circle cx="12" cy="9" r="7"/>
              <path d="M17 16l-2 4H9l-2-4"/>
              <path d="M8 20h8"/>
              <path d="M12 6a2 2 0 0 1 2 2"/>
            </svg>
          `;
          minIcon.addEventListener('click', () => {
            isMinimized = false;
            updateDisplayState(banner);
          });
        }
        
        // Insert directly before the target button to align nicely inside the row
        if (minIcon.nextSibling !== targetBtn) {
          targetBtn.parentElement.insertBefore(minIcon, targetBtn);
        }
      }

      updateDisplayState(banner);

      // Render or update channel points badge (shifted left for minimize button)
      if (pointsText) {
        let badge = banner.querySelector('.chatterino-points-badge');
        if (!badge) {
          badge = document.createElement('div');
          badge.className = 'chatterino-points-badge';
          badge.style.position = 'absolute';
          badge.style.top = '8px';
          badge.style.right = '40px'; // Shifted left to make room for minimize button
          badge.style.background = '#9146ff';
          badge.style.color = '#ffffff';
          badge.style.padding = '3px 8px';
          badge.style.borderRadius = '4px';
          badge.style.fontSize = '11px';
          badge.style.fontWeight = 'bold';
          badge.style.zIndex = '100000';
          badge.style.pointerEvents = 'none';
          badge.style.boxShadow = '0 2px 6px rgba(0,0,0,0.3)';
          banner.appendChild(badge);
          
          if (getComputedStyle(banner).position === 'static') {
            banner.style.position = 'relative';
          }
        }
        badge.textContent = pointsText + ' pts';
      }
    } else {
      // Clean up minimized icon if prediction is over
      const minIcon = document.getElementById('chatterino-prediction-min-icon');
      if (minIcon) {
        minIcon.remove();
      }
      lastPredictionTitle = '';
      isMinimized = false;
    }
  }

  // Poll every 500ms
  pollIntervalId = setInterval(checkAndMoveBanners, 500);
  console.log('[Chatterino Companion] Extension script active.');
})();
