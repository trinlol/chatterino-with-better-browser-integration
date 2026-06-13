(function () {
  'use strict';

  const CLAIM_COMMUNITY_POINTS = {
    operationName: 'ClaimCommunityPoints',
    extensions: {
      persistedQuery: {
        version: 1,
        sha256Hash: '46aaeebe02c99afdf4fc97c7c0cba964124bf6b0af229395f1f6d1feed05b3d0'
      }
    }
  };

  let gqlClientId = 'kimne78zx3cx6dzkoethbq4z55auq1';
  let claimInFlight = false;

  const state = {
    channelPoints: {
      balance: null,
      claimAvailable: false,
      claimId: null,
      channelId: null,
      channelLogin: null
    },
    prediction: null,
    rewards: [],
    lastUpdate: 0
  };

  function getCurrentChannelLogin() {
    const path = window.location.pathname.toLowerCase();
    const parts = path.split('/').filter(Boolean);
    if (parts.length === 0) {
      return '';
    }
    if (parts[0] === 'popout' && parts[1]) {
      return parts[1];
    }
    if (parts[0] === 'moderator' && parts[1]) {
      return parts[1];
    }
    const staticPages = ['directory', 'videos', 'settings', 'subscriptions', 'wallet', 'drops', 'search'];
    if (staticPages.includes(parts[0])) {
      return '';
    }
    return parts[0];
  }

  function resetStateForChannel(channelLogin) {
    state.channelPoints = {
      balance: null,
      claimAvailable: false,
      claimId: null,
      channelId: null,
      channelLogin: channelLogin || null
    };
    state.prediction = null;
    state.rewards = [];
  }

  function shouldApplyPointsForChannel(channelLogin) {
    const current = getCurrentChannelLogin();
    if (!current) {
      return true;
    }
    if (!channelLogin) {
      return !state.channelPoints.channelLogin || state.channelPoints.channelLogin === current;
    }
    return channelLogin.toLowerCase() === current;
  }

  function applyAvailableClaim(availableClaim, channelLogin) {
    if (!shouldApplyPointsForChannel(channelLogin)) {
      return;
    }
    if (availableClaim == null) {
      state.channelPoints.claimAvailable = false;
      state.channelPoints.claimId = null;
      return;
    }
    if (typeof availableClaim === 'object') {
      state.channelPoints.claimAvailable = true;
      if (availableClaim.id) {
        state.channelPoints.claimId = availableClaim.id;
      }
      return;
    }
    state.channelPoints.claimAvailable = Boolean(availableClaim);
  }

  function formatBalance(value) {
    if (value == null || Number.isNaN(Number(value))) {
      return null;
    }
    const num = Number(value);
    if (num >= 1_000_000) {
      return (num / 1_000_000).toFixed(1).replace(/\.0$/, '') + 'M';
    }
    if (num >= 1_000) {
      return (num / 1_000).toFixed(1).replace(/\.0$/, '') + 'K';
    }
    return String(num);
  }

  function emitUpdate() {
    state.lastUpdate = Date.now();

    const root = document.documentElement;
    root.setAttribute('data-cc-gql-balance', state.channelPoints.balance || '');
    root.setAttribute('data-cc-gql-claim', state.channelPoints.claimAvailable ? '1' : '0');
    root.setAttribute(
      'data-cc-gql-prediction',
      state.prediction?.title ? JSON.stringify(state.prediction) : ''
    );
    root.setAttribute('data-cc-gql-rewards', JSON.stringify(state.rewards.slice(0, 80)));
    root.setAttribute('data-cc-gql-updated', String(state.lastUpdate));

    window.dispatchEvent(
      new CustomEvent('chatterino-companion-gql', { detail: structuredClone(state) })
    );
  }

  function parsePredictionEvent(event) {
    if (!event || typeof event !== 'object') {
      return null;
    }
    const statusRaw = (event.status || event.predictionStatus || '').toLowerCase();
    let status = 'started';
    if (statusRaw.includes('lock') || statusRaw.includes('close')) {
      status = 'locked';
    } else if (statusRaw.includes('end') || statusRaw.includes('resolve') || statusRaw.includes('cancel')) {
      status = 'ended';
    }

    const outcomes = event.outcomes || event.predictionOutcomes || [];
    const options = outcomes
      .map((o) => o.title || o.name || o.label)
      .filter(Boolean);

    return {
      title: event.title || event.predictionTitle || 'Prediction',
      options,
      status,
      duration: event.durationSeconds || event.predictionWindowSeconds || 0,
      winner: event.winningOutcome?.title || event.winner || ''
    };
  }

  function walkJson(node, visitor) {
    if (!node || typeof node !== 'object') {
      return;
    }
    visitor(node);
    if (Array.isArray(node)) {
      node.forEach((item) => walkJson(item, visitor));
      return;
    }
    Object.values(node).forEach((value) => walkJson(value, visitor));
  }

  function extractFromBody(body) {
    if (!body || typeof body !== 'object') {
      return;
    }

    walkJson(body, (obj) => {
      const responseChannelLogin = obj.channel?.login?.toLowerCase?.() || null;

      if (obj.communityPoints != null && typeof obj.communityPoints === 'object') {
        const cp = obj.communityPoints;
        if (shouldApplyPointsForChannel(responseChannelLogin || state.channelPoints.channelLogin)) {
          if (cp.balance != null) {
            state.channelPoints.balance = formatBalance(cp.balance);
          }
          if (cp.availableClaim != null) {
            applyAvailableClaim(cp.availableClaim, responseChannelLogin || state.channelPoints.channelLogin);
          } else if (cp.claimAvailable != null) {
            applyAvailableClaim(cp.claimAvailable, responseChannelLogin || state.channelPoints.channelLogin);
          }
        }
      }

      if (
        obj.balance != null &&
        (obj.claimAvailable != null || obj.availableClaim != null) &&
        shouldApplyPointsForChannel(responseChannelLogin || state.channelPoints.channelLogin)
      ) {
        state.channelPoints.balance = formatBalance(obj.balance);
        applyAvailableClaim(obj.availableClaim ?? obj.claimAvailable, responseChannelLogin || state.channelPoints.channelLogin);
      }

      if (obj.channel != null && typeof obj.channel === 'object') {
        const channelLogin = obj.channel.login?.toLowerCase?.() || null;
        if (channelLogin && !shouldApplyPointsForChannel(channelLogin)) {
          return;
        }
        if (obj.channel.id) {
          state.channelPoints.channelId = obj.channel.id;
        }
        if (obj.channel.login) {
          state.channelPoints.channelLogin = obj.channel.login.toLowerCase();
        }
        if (obj.channel.communityPoints?.balance != null) {
          state.channelPoints.balance = formatBalance(obj.channel.communityPoints.balance);
        }
        if (obj.channel.communityPoints?.availableClaim != null) {
          applyAvailableClaim(
            obj.channel.communityPoints.availableClaim,
            obj.channel.login?.toLowerCase?.() || channelLogin
          );
        }
      }

      if (obj.communityPredictionEvent != null) {
        const parsed = parsePredictionEvent(obj.communityPredictionEvent);
        if (parsed) {
          state.prediction = parsed;
        }
      }

      if (obj.event != null && (obj.event.outcomes || obj.event.predictionOutcomes)) {
        const parsed = parsePredictionEvent(obj.event);
        if (parsed) {
          state.prediction = parsed;
        }
      }

      if (obj.prediction != null && typeof obj.prediction === 'object') {
        const parsed = parsePredictionEvent(obj.prediction);
        if (parsed) {
          state.prediction = parsed;
        }
      }

      if (obj.claimCommunityPoints != null) {
        const claim = obj.claimCommunityPoints;
        if (claim.balance != null) {
          state.channelPoints.balance = formatBalance(claim.balance);
        }
        state.channelPoints.claimAvailable = false;
        state.channelPoints.claimId = null;
      }

      if (Array.isArray(obj.customRewards)) {
        state.rewards = obj.customRewards
          .filter((r) => r && (r.title || r.name))
          .map((r) => ({
            id: r.id || r.rewardID || '',
            title: r.title || r.name || 'Reward',
            cost: r.cost ?? r.defaultCost ?? 0,
            prompt: r.prompt || r.defaultPrompt || ''
          }));
      }

      if (Array.isArray(obj.communityPointsSettings?.customRewards)) {
        state.rewards = obj.communityPointsSettings.customRewards
          .filter((r) => r && (r.title || r.name))
          .map((r) => ({
            id: r.id || '',
            title: r.title || r.name || 'Reward',
            cost: r.cost ?? 0,
            prompt: r.prompt || ''
          }));
      }
    });

    if (Array.isArray(body)) {
      body.forEach((entry) => {
        if (entry?.errors?.length) {
          return;
        }
        extractFromBody(entry?.data ?? entry);
      });
      return;
    }

    if (body.data) {
      extractFromBody(body.data);
    }
  }

  function handleGqlPayload(payload) {
    try {
      const body = typeof payload === 'string' ? JSON.parse(payload) : payload;
      extractFromBody(body);
      emitUpdate();
    } catch (_) {
      // ignore malformed payloads
    }
  }

  function captureGqlClientId(headers) {
    if (!headers) {
      return;
    }
    if (headers instanceof Headers) {
      const clientId = headers.get('Client-Id') || headers.get('client-id');
      if (clientId) {
        gqlClientId = clientId;
      }
      return;
    }
    const clientId = headers['Client-Id'] || headers['client-id'];
    if (clientId) {
      gqlClientId = clientId;
    }
  }

  async function claimViaGql(channelId, claimId) {
    if (!channelId || !claimId || claimInFlight) {
      return false;
    }
    claimInFlight = true;
    try {
      const response = await fetch('https://gql.twitch.tv/gql', {
        method: 'POST',
        credentials: 'include',
        headers: {
          'Content-Type': 'text/plain;charset=UTF-8',
          'Client-Id': gqlClientId
        },
        body: JSON.stringify({
          ...CLAIM_COMMUNITY_POINTS,
          variables: {
            input: {
              channelID: String(channelId),
              claimID: claimId
            }
          }
        })
      });
      if (!response.ok) {
        return false;
      }
      const body = await response.json();
      handleGqlPayload(body);
      const entries = Array.isArray(body) ? body : [body];
      const claimFailed = entries.some(
        (entry) => entry?.errors?.length || entry?.data?.claimCommunityPoints?.error
      );
      if (claimFailed) {
        return false;
      }
      state.channelPoints.claimAvailable = false;
      state.channelPoints.claimId = null;
      emitUpdate();
      return true;
    } catch (_) {
      return false;
    } finally {
      claimInFlight = false;
    }
  }

  function claimViaDomClick() {
    const claimBtn =
      document.querySelector('button[aria-label="Claim Bonus"]') ||
      document.querySelector('.claimable-bonus__icon')?.closest('button');
    if (!claimBtn) {
      return false;
    }
    // DOM click toggles the reward menu open — only use when GQL claim is unavailable.
    claimBtn.click();
    window.dispatchEvent(new CustomEvent('chatterino-companion-dismiss-reward-dialog'));
    return true;
  }

  const originalFetch = window.fetch;
  window.fetch = async function (...args) {
    try {
      captureGqlClientId(args[1]?.headers);
    } catch (_) {
      // ignore header capture errors
    }
    const response = await originalFetch.apply(this, args);
    try {
      const url = typeof args[0] === 'string' ? args[0] : args[0]?.url;
      if (url && url.includes('gql.twitch.tv')) {
        const clone = response.clone();
        clone.text().then(handleGqlPayload).catch(() => {});
      }
    } catch (_) {
      // ignore hook errors
    }
    return response;
  };

  const originalOpen = XMLHttpRequest.prototype.open;
  const originalSend = XMLHttpRequest.prototype.send;

  XMLHttpRequest.prototype.open = function (method, url, ...rest) {
    this.__ccGqlUrl = url;
    return originalOpen.call(this, method, url, ...rest);
  };

  XMLHttpRequest.prototype.send = function (...args) {
    this.addEventListener('load', function () {
      try {
        if (this.__ccGqlUrl && String(this.__ccGqlUrl).includes('gql.twitch.tv') && this.responseText) {
          handleGqlPayload(this.responseText);
        }
      } catch (_) {
        // ignore
      }
    });
    return originalSend.apply(this, args);
  };

  window.__chatterinoCompanionGql = {
    getState() {
      return structuredClone(state);
    },
    async claimChannelPoints() {
      const { channelId, claimId, claimAvailable } = state.channelPoints;
      if (claimAvailable && channelId && claimId) {
        const claimed = await claimViaGql(channelId, claimId);
        if (claimed) {
          return true;
        }
      }
      if (claimAvailable) {
        return claimViaDomClick();
      }
      return false;
    },
    redeemReward(rewardId, channelId) {
      window.dispatchEvent(
        new CustomEvent('chatterino-companion-redeem', { detail: { rewardId, channelId } })
      );
    }
  };

  window.addEventListener('chatterino-companion-claim-request', () => {
    void window.__chatterinoCompanionGql.claimChannelPoints();
  });

  window.addEventListener('chatterino-companion-channel-change', (event) => {
    resetStateForChannel(event.detail?.channel || getCurrentChannelLogin());
    emitUpdate();
  });

  emitUpdate();
})();
