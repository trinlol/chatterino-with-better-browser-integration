(function () {
  "use strict";

  // anti-wipe can be evaluated more than once during Twitch SPA navigation.
  // Do not stack fetch/XHR hooks or polling loops when that happens.
  if (window.__chatterinoCompanionGql?.__version === 2) {
    return;
  }

  const DEFAULT_CLAIM_COMMUNITY_POINTS_HASH =
    "46aaeebe02c99afdf4fc97c7c0cba964124bf6b0af229395f1f6d1feed05b3d0";

  let gqlClientId = "kimne78zx3cx6dzkoethbq4z55auq1";
  let claimInFlight = false;
  let pendingRewardTimeoutId = null;
  let contextPollTimer = null;
  let lastClaimAttemptAt = 0;
  let channelPointsContextHash = "";
  let claimCommunityPointsHash = DEFAULT_CLAIM_COMMUNITY_POINTS_HASH;
  const PENDING_REWARD_TIMEOUT_MS = 90000;
  const CONTEXT_POLL_MS = 45000;
  const CONTEXT_POLL_INITIAL_MS = 1000;
  const CLAIM_RETRY_MIN_MS = 30000;
  const GQL_REQUEST_TIMEOUT_MS = 10000;
  const ADAPTER_FAILURE_THRESHOLD = 3;
  const ADAPTER_COOLDOWN_MS = 120000;
  const ADAPTER_FRESHNESS_MS = 90000;

  const CHANNEL_POINTS_CONTEXT_HASHES = [
    "7fe050e3761eb2cf258d70ee1a21cbd76fa8cf3d7e7b12fc437e7029d446b5e3",
    "374314de591e69925fce3ddc2bcf085796f56ebb8cad67a0daa3165c03adc345",
    "9988086babc615a918a1e9a722ff41d98847acac822645209ac7379eecb27152",
    "1530a003a7d374b0380b79db0be0534f30ff46e61cffa2bc0e2468a909fbc024",
  ];

  const gqlHeaders = {};

  const state = {
    channelPoints: {
      balance: null,
      claimAvailable: false,
      claimId: null,
      channelId: null,
      channelLogin: null,
    },
    prediction: null,
    poll: null,
    rewards: [],
    lastUpdate: 0,
    adapter: {
      source: "private-graphql",
      updatedAt: 0,
      freshUntil: 0,
      supported: false,
      failure: "uninitialized",
      consecutiveFailures: 0,
      cooldownUntil: 0,
    },
  };

  function isPollingAllowed() {
    return (
      Boolean(getCurrentChannelLogin()) && document.visibilityState !== "hidden"
    );
  }

  function adapterStatus() {
    return {
      source: state.adapter.source,
      updatedAt: state.adapter.updatedAt,
      freshUntil: state.adapter.freshUntil,
      supported: state.adapter.supported,
      failure: state.adapter.failure,
      consecutiveFailures: state.adapter.consecutiveFailures,
      cooldownUntil: state.adapter.cooldownUntil,
    };
  }

  function recordAdapterSuccess() {
    const now = Date.now();
    state.adapter = {
      ...state.adapter,
      updatedAt: now,
      freshUntil: now + ADAPTER_FRESHNESS_MS,
      supported: true,
      failure: "",
      consecutiveFailures: 0,
      cooldownUntil: 0,
    };
  }

  // Failures of Twitch's own requests (observed through the hooks) are
  // reported but do not count towards the cooldown of our own polling: an
  // unrelated failing operation on the page must not suspend the adapter.
  function recordAdapterFailure(reason, { observed = false } = {}) {
    const now = Date.now();
    if (observed) {
      // Record the reason only; one failing page request (integrity errors
      // are common) must not mark otherwise fresh data as unsupported.
      state.adapter = {
        ...state.adapter,
        failure: String(reason || "network"),
      };
      return;
    }
    const failures = state.adapter.consecutiveFailures + 1;
    const cooldownUntil =
      failures >= ADAPTER_FAILURE_THRESHOLD ? now + ADAPTER_COOLDOWN_MS : 0;
    state.adapter = {
      ...state.adapter,
      updatedAt: now,
      freshUntil: 0,
      supported: false,
      failure: String(reason || "network"),
      consecutiveFailures: failures,
      cooldownUntil,
    };
  }

  function adapterCircuitOpen() {
    return state.adapter.cooldownUntil > Date.now();
  }

  async function fetchWithTimeout(url, init = {}) {
    const controller = new AbortController();
    const timeoutId = setTimeout(
      () => controller.abort(),
      GQL_REQUEST_TIMEOUT_MS
    );
    try {
      // Use the unhooked fetch: our own requests are handled by their callers
      // and must not be parsed a second time by the observer.
      return await originalFetch.call(window, url, {
        ...init,
        signal: controller.signal,
      });
    } finally {
      clearTimeout(timeoutId);
    }
  }

  function getCurrentChannelLogin() {
    const path = window.location.pathname.toLowerCase();
    const parts = path.split("/").filter(Boolean);
    if (parts.length === 0) {
      return "";
    }
    if (parts[0] === "popout" && parts[1]) {
      return parts[1];
    }
    if (parts[0] === "moderator" && parts[1]) {
      return parts[1];
    }
    const staticPages = [
      "directory",
      "videos",
      "settings",
      "subscriptions",
      "wallet",
      "drops",
      "search",
    ];
    if (staticPages.includes(parts[0])) {
      return "";
    }
    return parts[0];
  }

  // Bumped on every channel reset. Observed requests capture it when they are
  // issued, so a late response to a request made for the previous channel
  // cannot repopulate the new channel's state (see isEntryForCurrentChannel).
  let channelGeneration = 0;

  function resetStateForChannel(channelLogin) {
    channelGeneration += 1;
    resetEndedActivityTracking();
    lastClaimAttemptAt = 0;
    state.channelPoints = {
      balance: null,
      claimAvailable: false,
      claimId: null,
      channelId: null,
      channelLogin: loginOf(channelLogin),
    };
    state.prediction = null;
    state.poll = null;
    state.rewards = [];
    state.adapter = {
      ...state.adapter,
      freshUntil: 0,
      supported: false,
      failure: "channel-change",
    };
  }

  function shouldApplyPointsForChannel(channelLogin) {
    const current = getCurrentChannelLogin();
    if (!current) {
      return true;
    }
    if (!channelLogin) {
      return (
        !state.channelPoints.channelLogin ||
        state.channelPoints.channelLogin === current
      );
    }
    return String(channelLogin).toLowerCase() === current;
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
    if (typeof availableClaim === "object") {
      const claimId =
        normalizeId(availableClaim.id) ||
        normalizeId(availableClaim.claimID) ||
        normalizeId(availableClaim.claimId);
      if (!claimId) {
        state.channelPoints.claimAvailable = false;
        state.channelPoints.claimId = null;
        return;
      }
      state.channelPoints.claimAvailable = true;
      state.channelPoints.claimId = claimId;
      return;
    }
    state.channelPoints.claimAvailable = Boolean(availableClaim);
  }

  function applyCommunityPointsSnapshot(cp, channelLogin, channelId) {
    if (
      !cp ||
      typeof cp !== "object" ||
      !shouldApplyPointsForChannel(channelLogin)
    ) {
      return;
    }
    if (normalizeId(channelId)) {
      state.channelPoints.channelId = normalizeId(channelId);
    }
    if (loginOf(channelLogin)) {
      state.channelPoints.channelLogin = loginOf(channelLogin);
    }
    if (cp.balance != null) {
      state.channelPoints.balance = formatBalance(cp.balance);
    }
    if (cp.availableClaim != null) {
      applyAvailableClaim(cp.availableClaim, channelLogin);
    } else if (cp.claimAvailable != null) {
      applyAvailableClaim(cp.claimAvailable, channelLogin);
    }
  }

  function formatBalance(value) {
    if (typeof value !== "number" && typeof value !== "string") {
      return null;
    }
    const num = Math.trunc(Number(value));
    // page-data.js sanitizeBalance only accepts non-negative digit strings
    // with an optional K/M/B suffix.
    if (!Number.isFinite(num) || num < 0 || (typeof value === "string" && !value.trim())) {
      return null;
    }
    const units = [
      [1_000_000_000, "B"],
      [1_000_000, "M"],
      [1_000, "K"],
    ];
    for (let i = 0; i < units.length; i += 1) {
      const [size, suffix] = units[i];
      if (num >= size) {
        const scaled = (num / size).toFixed(1);
        // 999,999 rounds to "1000.0K": promote it to "1M" instead.
        if (Number(scaled) >= 1000 && i > 0) {
          const [biggerSize, biggerSuffix] = units[i - 1];
          return (num / biggerSize).toFixed(1).replace(/\.0$/, "") + biggerSuffix;
        }
        return scaled.replace(/\.0$/, "") + suffix;
      }
    }
    return String(num);
  }

  const MAX_REWARDS = 100;

  function mapReward(r) {
    const cost = Number(r.cost ?? r.defaultCost ?? 0);
    return {
      id: normalizeId(r.id) || normalizeId(r.rewardID),
      title: boundedText(r.title || r.name) || "Reward",
      cost: Number.isFinite(cost) && cost >= 0 ? cost : 0,
      prompt: boundedText(r.prompt || r.defaultPrompt),
      isUserInputRequired: Boolean(r.isUserInputRequired),
      channelId: normalizeId(r.channelID) || normalizeId(r.channel_id),
    };
  }

  function clearPendingRewardTimeout() {
    if (pendingRewardTimeoutId) {
      clearTimeout(pendingRewardTimeoutId);
      pendingRewardTimeoutId = null;
    }
  }

  function cancelPendingReward(reason) {
    clearPendingRewardTimeout();
    window.dispatchEvent(
      new CustomEvent("chatterino-companion-reward-cancelled", {
        detail: { reason: reason || "cancelled" },
      })
    );
  }

  function notifyPendingReward(detail) {
    clearPendingRewardTimeout();
    pendingRewardTimeoutId = setTimeout(() => {
      cancelPendingReward("timeout");
    }, PENDING_REWARD_TIMEOUT_MS);

    window.dispatchEvent(
      new CustomEvent("chatterino-companion-reward-pending", {
        detail: {
          rewardId: detail.rewardId || "",
          title: detail.title || "",
          prompt: detail.prompt || "",
          channelId: detail.channelId || state.channelPoints.channelId || "",
        },
      })
    );
  }

  function handleRedemptionPayload(redemption, channelId) {
    if (!redemption || typeof redemption !== "object") {
      return;
    }

    const reward =
      redemption.reward || redemption.communityPointsCustomReward || {};
    if (!reward.isUserInputRequired) {
      return;
    }

    const userInput =
      redemption.userInput ||
      redemption.user_input ||
      redemption.input ||
      redemption.message;
    if (userInput != null && String(userInput).trim()) {
      return;
    }

    const status = String(
      redemption.status || redemption.redemptionStatus || ""
    ).toUpperCase();
    if (
      status === "FULFILLED" ||
      status === "CANCELED" ||
      status === "CANCELLED"
    ) {
      return;
    }

    notifyPendingReward({
      rewardId: reward.id || reward.rewardID || "",
      title: reward.title || reward.name || "",
      prompt: reward.prompt || "",
      channelId:
        channelId ||
        reward.channelID ||
        reward.channel_id ||
        state.channelPoints.channelId ||
        "",
    });
  }

  function extractRedemptionFromPayload(obj) {
    if (!obj || typeof obj !== "object") {
      return;
    }

    const redeemKeys = [
      "redeemCommunityPointsCustomReward",
      "redeemCommunityPointsCommunityPointsAutomaticReward",
      "redeemCommunityPointsAutomaticReward",
    ];

    for (const key of redeemKeys) {
      if (obj[key] == null) {
        continue;
      }
      const payload = obj[key];
      if (payload?.error) {
        continue;
      }
      const redemption = payload.redemption || payload;
      handleRedemptionPayload(redemption, state.channelPoints.channelId);
    }
  }


  // emitUpdate runs for every observed GraphQL response. Attributes are only
  // rewritten when their value changed, and the full-state event is only
  // dispatched when something the content script uses changed. A periodic
  // heartbeat republishes everything anyway, so the adapter freshness the
  // content side checks (freshUntil, 90 s) never lapses while responses keep
  // arriving, and an attribute changed by someone else is repaired.
  const PUBLISH_HEARTBEAT_MS = 15000;
  const publishedAttributes = new Map();
  // A separate "freshUntil advanced by > 30 s" trigger would be redundant:
  // freshUntil is now + 90 s, so it cannot advance 30 s without the 15 s
  // heartbeat having fired first. Published freshness trails by < 15 s.
  let lastPublishedFingerprint = null;
  let lastPublishedAt = 0;

  function setRootAttribute(root, name, value, force) {
    if (!force && publishedAttributes.get(name) === value) {
      return;
    }
    root.setAttribute(name, value);
    publishedAttributes.set(name, value);
  }

  function emitUpdate() {
    const now = Date.now();
    expireEndedActivities(now);
    state.lastUpdate = now;

    const root = document.documentElement;
    const heartbeat = now - lastPublishedAt >= PUBLISH_HEARTBEAT_MS;
    const rewardsJson = JSON.stringify(state.rewards);
    const attributes = [
      ["data-cc-gql-balance", state.channelPoints.balance || ""],
      ["data-cc-gql-claim", state.channelPoints.claimAvailable ? "1" : "0"],
      ["data-cc-gql-claim-id", state.channelPoints.claimId || ""],
      ["data-cc-gql-channel-id", state.channelPoints.channelId || ""],
      [
        "data-cc-gql-prediction",
        state.prediction?.title ? JSON.stringify(state.prediction) : "",
      ],
      ["data-cc-gql-poll", state.poll?.title ? JSON.stringify(state.poll) : ""],
      [
        "data-cc-gql-rewards",
        state.rewards.length > 80
          ? JSON.stringify(state.rewards.slice(0, 80))
          : rewardsJson,
      ],
    ];
    for (const [name, value] of attributes) {
      setRootAttribute(root, name, value, heartbeat);
    }

    // Timestamps (lastUpdate, adapter.updatedAt/freshUntil) are left out:
    // they change on every response and are covered by the heartbeat.
    const fingerprint = JSON.stringify([
      attributes.map(([, value]) => value),
      rewardsJson,
      state.channelPoints.channelLogin,
      state.adapter.source,
      state.adapter.supported,
      state.adapter.failure,
      state.adapter.consecutiveFailures,
      state.adapter.cooldownUntil,
    ]);
    if (heartbeat || fingerprint !== lastPublishedFingerprint) {
      setRootAttribute(
        root,
        "data-cc-gql-adapter",
        JSON.stringify(adapterStatus()),
        true
      );
      lastPublishedFingerprint = fingerprint;
      lastPublishedAt = now;
      window.dispatchEvent(
        new CustomEvent("chatterino-companion-gql", {
          detail: structuredClone(state),
        })
      );
    }
    // Freshness for integration health ("GraphQL: 3s ago" in the popup):
    // written on every update, changed or not.
    root.setAttribute("data-cc-gql-updated", String(now));

    // Claiming is decided by content.js (tryAutoClaim), which honours the
    // user's auto-claim setting. Requesting a claim from here bypassed it.
  }

  function normalizeEventTimestamp(value) {
    if (typeof value === "number" && Number.isFinite(value) && value > 0) {
      return Math.round(value < 1_000_000_000_000 ? value * 1000 : value);
    }
    if (typeof value === "string" && value.trim()) {
      const numeric = Number(value);
      if (Number.isFinite(numeric) && numeric > 0) {
        return Math.round(
          numeric < 1_000_000_000_000 ? numeric * 1000 : numeric
        );
      }
      const parsed = Date.parse(value);
      return Number.isFinite(parsed) ? parsed : 0;
    }
    return 0;
  }

  function eventDeadline(event, durationSeconds) {
    const explicitDeadline = normalizeEventTimestamp(
      event.locksAt ??
        event.lockAt ??
        event.closesAt ??
        event.closeAt ??
        event.endsAt ??
        event.endAt
    );
    if (explicitDeadline > 0) {
      return explicitDeadline;
    }
    const startedAt = normalizeEventTimestamp(
      event.startedAt ?? event.createdAt ?? event.startTime
    );
    return startedAt > 0 && durationSeconds > 0
      ? startedAt + durationSeconds * 1000
      : 0;
  }

  // Internal marker: the event was cancelled/removed and must clear state.
  const CLEARED_STATUS = "cleared";
  const MAX_TEXT_LENGTH = 500;
  const MAX_OPTIONS = 20;

  function statusText(value) {
    return typeof value === "string" ? value.toLowerCase() : "";
  }

  function boundedText(value) {
    return typeof value === "string"
      ? value.trim().slice(0, MAX_TEXT_LENGTH)
      : "";
  }

  function optionTitles(list, extraKey) {
    if (!Array.isArray(list)) {
      return [];
    }
    const options = [];
    for (const item of list) {
      if (!item || typeof item !== "object") {
        continue;
      }
      const text = boundedText(
        item.title || item.name || item.label || (extraKey && item[extraKey])
      );
      if (text) {
        options.push(text);
      }
      if (options.length >= MAX_OPTIONS) {
        break;
      }
    }
    return options;
  }

  // Twitch keeps answering with a finished poll/prediction for a while and
  // nothing replaces it until the next one starts, so an ended activity is
  // dropped from state after a short grace period. The same ended activity
  // arriving again later is not resurrected. (Predictions are already sent to
  // the desktop as removals once ended, see hideEnded in content.js; this only
  // makes the page state agree.)
  const ENDED_ACTIVITY_TTL_MS = 30000;
  const endedSince = { poll: 0, prediction: 0 };
  const expiredEndedIdentity = { poll: "", prediction: "" };
  let endedExpiryTimer = null;

  function activityIdentity(activity) {
    return JSON.stringify([activity?.title || "", activity?.options || []]);
  }

  function resetEndedActivityTracking() {
    endedSince.poll = 0;
    endedSince.prediction = 0;
    expiredEndedIdentity.poll = "";
    expiredEndedIdentity.prediction = "";
    clearTimeout(endedExpiryTimer);
    endedExpiryTimer = null;
  }

  function expireEndedActivities(now) {
    let changed = false;
    for (const key of ["poll", "prediction"]) {
      if (
        state[key]?.status === "ended" &&
        endedSince[key] > 0 &&
        now - endedSince[key] >= ENDED_ACTIVITY_TTL_MS
      ) {
        expiredEndedIdentity[key] = activityIdentity(state[key]);
        state[key] = null;
        endedSince[key] = 0;
        changed = true;
      }
    }
    return changed;
  }

  function scheduleEndedExpiry() {
    clearTimeout(endedExpiryTimer);
    endedExpiryTimer = null;
    const pending = ["poll", "prediction"]
      .filter((key) => state[key]?.status === "ended" && endedSince[key] > 0)
      .map((key) => endedSince[key] + ENDED_ACTIVITY_TTL_MS);
    if (pending.length === 0) {
      return;
    }
    const delay = Math.max(0, Math.min(...pending) - Date.now());
    endedExpiryTimer = setTimeout(() => {
      endedExpiryTimer = null;
      try {
        emitUpdate();
      } finally {
        scheduleEndedExpiry();
      }
    }, delay + 50);
  }

  function applyActivity(key, parsed) {
    if (!parsed) {
      return;
    }
    if (parsed.status === CLEARED_STATUS) {
      state[key] = null;
      endedSince[key] = 0;
      return;
    }
    const identity = activityIdentity(parsed);
    if (parsed.status === "ended") {
      if (expiredEndedIdentity[key] === identity) {
        return;
      }
      const previous = state[key];
      const alreadyEnded =
        previous?.status === "ended" &&
        endedSince[key] > 0 &&
        activityIdentity(previous) === identity;
      if (!alreadyEnded) {
        endedSince[key] = Date.now();
      }
      state[key] = parsed;
      if (!alreadyEnded) {
        scheduleEndedExpiry();
      }
      return;
    }
    endedSince[key] = 0;
    expiredEndedIdentity[key] = "";
    state[key] = parsed;
  }

  function parsePredictionEvent(event) {
    if (!event || typeof event !== "object") {
      return null;
    }
    const statusRaw = statusText(event.status || event.predictionStatus);
    let status = "started";
    // Cancelled predictions are refunded: nothing is left to show or vote on.
    if (statusRaw.includes("cancel") || statusRaw.includes("refund")) {
      status = CLEARED_STATUS;
    } else if (statusRaw.includes("lock") || statusRaw.includes("close")) {
      status = "locked";
    } else if (
      statusRaw.includes("end") ||
      statusRaw.includes("resolve")
    ) {
      status = "ended";
    }

    const options = optionTitles(event.outcomes || event.predictionOutcomes);
    const title = boundedText(event.title || event.predictionTitle);
    if (!title && options.length < 2) {
      return null;
    }
    const duration = Number(
      event.durationSeconds || event.predictionWindowSeconds || 0
    );

    return {
      title: title || "Prediction",
      options,
      status,
      duration: Number.isFinite(duration) && duration > 0 ? duration : 0,
      closesAt: eventDeadline(event, duration),
      winner: boundedText(event.winningOutcome?.title || event.winner),
    };
  }

  function parsePollEvent(event) {
    if (!event || typeof event !== "object") {
      return null;
    }
    const statusRaw = statusText(event.status || event.pollStatus);
    let status = "started";
    // Twitch poll states: ACTIVE, COMPLETED, TERMINATED (ended early, results
    // stay visible), ARCHIVED / MODERATED / INVALID (removed).
    if (
      statusRaw.includes("cancel") ||
      statusRaw.includes("archive") ||
      statusRaw.includes("moderat") ||
      statusRaw.includes("invalid") ||
      statusRaw.includes("delete")
    ) {
      status = CLEARED_STATUS;
    } else if (statusRaw.includes("lock") || statusRaw.includes("close")) {
      status = "locked";
    } else if (
      statusRaw.includes("end") ||
      statusRaw.includes("complete") ||
      statusRaw.includes("terminat")
    ) {
      status = "ended";
    }

    const options = optionTitles(
      event.choices || event.pollChoices || event.outcomes,
      "choiceText"
    );
    const title = boundedText(event.title || event.pollTitle);
    if (!title && options.length < 2) {
      return null;
    }
    const duration = Number(event.durationSeconds || 0);
    let closesAt = eventDeadline(event, duration);
    if (!closesAt && status === "started") {
      // "remaining" durations are relative to when the response arrived, not
      // to when the poll started.
      const remainingMs =
        Number(event.remainingDurationMilliseconds) ||
        Number(event.remainingDurationSeconds) * 1000;
      if (Number.isFinite(remainingMs) && remainingMs > 0) {
        closesAt = Date.now() + Math.round(remainingMs);
      }
    }

    return {
      title: title || "Poll",
      options,
      status,
      duration: Number.isFinite(duration) && duration > 0 ? duration : 0,
      closesAt,
    };
  }

  function getEventKind(container, event) {
    const typeHint = [
      container?.__typename,
      container?.type,
      container?.eventType,
      container?.operationName,
      event?.__typename,
      event?.type,
      event?.eventType,
    ]
      .filter(Boolean)
      .join(" ")
      .toLowerCase();

    if (typeHint.includes("poll")) {
      return "poll";
    }
    if (typeHint.includes("prediction")) {
      return "prediction";
    }
    if (
      (Array.isArray(event?.pollChoices) && event.pollChoices.length > 0) ||
      (Array.isArray(event?.choices) && event.choices.length > 0)
    ) {
      return "poll";
    }
    if (
      Array.isArray(event?.predictionOutcomes) &&
      event.predictionOutcomes.length > 0
    ) {
      return "prediction";
    }
    return "";
  }

  // Twitch responses are a few levels deep; anything far deeper or larger is
  // not a shape this adapter understands and must not exhaust the stack.
  const WALK_MAX_DEPTH = 48;
  const WALK_MAX_NODES = 200000;
  const MAX_PAYLOAD_CHARS = 8 * 1024 * 1024;

  function walkJson(node, visitor, budget = { nodes: 0 }, depth = 0) {
    if (!node || typeof node !== "object") {
      return;
    }
    if (depth > WALK_MAX_DEPTH || ++budget.nodes > WALK_MAX_NODES) {
      return;
    }
    visitor(node);
    const children = Array.isArray(node) ? node : Object.values(node);
    for (const child of children) {
      walkJson(child, visitor, budget, depth + 1);
    }
  }

  function loginOf(value) {
    return typeof value === "string" && value ? value.toLowerCase() : null;
  }

  // Ids are published to the content script, which only accepts this shape
  // (page-data.js sanitizeId). Numeric GraphQL ids are normalised to strings.
  function normalizeId(value) {
    if (typeof value === "number" && Number.isSafeInteger(value) && value >= 0) {
      return String(value);
    }
    return typeof value === "string" && /^[A-Za-z0-9_-]{1,128}$/.test(value)
      ? value
      : "";
  }

  // What an observed request was for: the channel generation when it was
  // issued and, per operation, the channel its variables name.
  function requestScopeOf(entry) {
    const vars =
      entry && typeof entry === "object" && entry.variables &&
      typeof entry.variables === "object"
        ? entry.variables
        : null;
    if (!vars) {
      return null;
    }
    const input =
      vars.input && typeof vars.input === "object" ? vars.input : {};
    const login = loginOf(
      vars.channelLogin ?? vars.login ?? input.channelLogin
    );
    const channelId = normalizeId(
      vars.channelID ?? vars.channelId ?? input.channelID ?? input.channelId
    );
    return login || channelId ? { login, channelId } : null;
  }

  function describeGqlRequest(payload) {
    const entries =
      payload == null ? [] : Array.isArray(payload) ? payload : [payload];
    return {
      generation: channelGeneration,
      // The channel the URL showed when the request was issued. Twitch fires
      // a new route's requests before content.js announces the change (its
      // sync is throttled), so the generation alone would drop them.
      issuedFor: getCurrentChannelLogin(),
      scopes: entries.map(requestScopeOf),
    };
  }

  // A response entry belongs to the current channel when its request named
  // the current channel; without a usable channel in the request it must
  // have been issued while the URL showed the current channel, or after the
  // last channel reset. Responses with no request context (e.g. our own
  // claim mutation) are always applied.
  function isEntryForCurrentChannel(request, index, count) {
    if (!request) {
      return true;
    }
    const scopes = Array.isArray(request.scopes) ? request.scopes : [];
    const scope = scopes.length === count ? scopes[index] : null;
    if (scope?.login) {
      const current = getCurrentChannelLogin();
      return !current || scope.login === current;
    }
    const knownChannelId = state.channelPoints.channelId;
    if (scope?.channelId && knownChannelId) {
      return scope.channelId === knownChannelId;
    }
    if (request.generation === channelGeneration) {
      return true;
    }
    const current = getCurrentChannelLogin();
    return Boolean(current) && request.issuedFor === current;
  }

  // Returns how many entries were applied (stale ones are skipped).
  function extractFromBody(body, request) {
    if (!body || typeof body !== "object") {
      return 0;
    }
    // Batched responses: one failed operation must not discard the others.
    const entries = Array.isArray(body) ? body : [body];
    const budget = { nodes: 0 };
    let applied = 0;
    for (let index = 0; index < entries.length; index += 1) {
      const entry = entries[index];
      if (!isEntryForCurrentChannel(request, index, entries.length)) {
        continue;
      }
      applied += 1;
      if (Array.isArray(entry?.errors) && entry.errors.length > 0) {
        continue;
      }
      walkJson(entry, extractVisitor, budget);
    }
    return applied;
  }

  function extractVisitor(obj) {
    {
      const responseChannelLogin = loginOf(obj.channel?.login);

      if (
        obj.community?.channel != null &&
        typeof obj.community.channel === "object"
      ) {
        const channel = obj.community.channel;
        const channelLogin = loginOf(channel.login) || responseChannelLogin;
        const cp = channel.self?.communityPoints ?? channel.communityPoints;
        applyCommunityPointsSnapshot(cp, channelLogin, channel.id);
      }

      if (
        obj.self?.communityPoints != null &&
        typeof obj.self.communityPoints === "object"
      ) {
        applyCommunityPointsSnapshot(
          obj.self.communityPoints,
          responseChannelLogin || state.channelPoints.channelLogin,
          state.channelPoints.channelId
        );
      }

      if (
        obj.communityPoints != null &&
        typeof obj.communityPoints === "object"
      ) {
        const cp = obj.communityPoints;
        if (
          shouldApplyPointsForChannel(
            responseChannelLogin || state.channelPoints.channelLogin
          )
        ) {
          if (cp.balance != null) {
            state.channelPoints.balance = formatBalance(cp.balance);
          }
          if (cp.availableClaim != null) {
            applyAvailableClaim(
              cp.availableClaim,
              responseChannelLogin || state.channelPoints.channelLogin
            );
          } else if (cp.claimAvailable != null) {
            applyAvailableClaim(
              cp.claimAvailable,
              responseChannelLogin || state.channelPoints.channelLogin
            );
          }
        }
      }

      if (
        obj.balance != null &&
        (obj.claimAvailable != null || obj.availableClaim != null) &&
        shouldApplyPointsForChannel(
          responseChannelLogin || state.channelPoints.channelLogin
        )
      ) {
        state.channelPoints.balance = formatBalance(obj.balance);
        applyAvailableClaim(
          obj.availableClaim ?? obj.claimAvailable,
          responseChannelLogin || state.channelPoints.channelLogin
        );
      }

      if (obj.channel != null && typeof obj.channel === "object") {
        const channelLogin = loginOf(obj.channel.login);
        if (channelLogin && !shouldApplyPointsForChannel(channelLogin)) {
          return;
        }
        if (normalizeId(obj.channel.id)) {
          state.channelPoints.channelId = normalizeId(obj.channel.id);
        }
        if (channelLogin) {
          state.channelPoints.channelLogin = channelLogin;
        }
        if (obj.channel.communityPoints?.balance != null) {
          state.channelPoints.balance = formatBalance(
            obj.channel.communityPoints.balance
          );
        }
        if (obj.channel.communityPoints?.availableClaim != null) {
          applyAvailableClaim(
            obj.channel.communityPoints.availableClaim,
            channelLogin
          );
        }
        if (obj.channel.self?.communityPoints != null) {
          applyCommunityPointsSnapshot(
            obj.channel.self.communityPoints,
            channelLogin,
            obj.channel.id
          );
        }
      }

      if (obj.communityPredictionEvent != null) {
        applyActivity("prediction", parsePredictionEvent(obj.communityPredictionEvent));
      }

      if (obj.communityPollEvent != null) {
        applyActivity("poll", parsePollEvent(obj.communityPollEvent));
      }

      const genericEventKind =
        obj.event != null && typeof obj.event === "object"
          ? getEventKind(obj, obj.event)
          : "";

      if (genericEventKind === "prediction") {
        applyActivity("prediction", parsePredictionEvent(obj.event));
      }

      if (genericEventKind === "poll") {
        applyActivity("poll", parsePollEvent(obj.event));
      }

      if (
        obj.prediction != null &&
        typeof obj.prediction === "object" &&
        getEventKind(obj, obj.prediction) === "prediction"
      ) {
        applyActivity("prediction", parsePredictionEvent(obj.prediction));
      }

      if (
        obj.poll != null &&
        typeof obj.poll === "object" &&
        getEventKind(obj, obj.poll) === "poll"
      ) {
        applyActivity("poll", parsePollEvent(obj.poll));
      }

      if (
        obj.claimCommunityPoints != null &&
        typeof obj.claimCommunityPoints === "object"
      ) {
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
          .slice(0, MAX_REWARDS)
          .map(mapReward);
      }

      if (Array.isArray(obj.communityPointsSettings?.customRewards)) {
        state.rewards = obj.communityPointsSettings.customRewards
          .filter((r) => r && (r.title || r.name))
          .slice(0, MAX_REWARDS)
          .map(mapReward);
      }

      extractRedemptionFromPayload(obj);
    }
  }

  function handleGqlPayload(payload, options = {}) {
    try {
      if (typeof payload === "string" && payload.length > MAX_PAYLOAD_CHARS) {
        // Too large to be a response this adapter reads; skip it silently.
        return false;
      }
      const body = typeof payload === "string" ? JSON.parse(payload) : payload;
      if (!body || typeof body !== "object" || !hasUsableEntry(body)) {
        recordAdapterFailure("schema", options);
        emitUpdate();
        return false;
      }
      if (extractFromBody(body, options.request) === 0) {
        // Every operation was for another (previous) channel: it says
        // nothing about the current one, so neither state nor health change.
        return false;
      }
      recordAdapterSuccess();
      emitUpdate();
      return true;
    } catch (_) {
      recordAdapterFailure("schema", options);
      try {
        emitUpdate();
      } catch (_) {
        // publishing must never throw into Twitch's request lifecycle
      }
      return false;
    }
  }

  function handleObservedPayload(payload, request) {
    return handleGqlPayload(payload, { observed: true, request });
  }

  // A single response with errors is a failure; a batch is usable as long as
  // one operation in it succeeded.
  function hasUsableEntry(body) {
    if (!Array.isArray(body)) {
      return !hasGraphqlErrors(body);
    }
    return body.some(
      (entry) =>
        entry &&
        typeof entry === "object" &&
        !(Array.isArray(entry.errors) && entry.errors.length > 0)
    );
  }

  function captureGqlClientId(headers) {
    if (!headers) {
      return;
    }
    if (headers instanceof Headers) {
      const clientId = headers.get("Client-Id") || headers.get("client-id");
      if (clientId) {
        gqlClientId = clientId;
      }
      for (const name of [
        "Client-Integrity",
        "X-Device-Id",
        "Client-Session-Id",
        "Client-Version",
        "Authorization",
      ]) {
        const value = headers.get(name);
        if (value) {
          gqlHeaders[name] = value;
        }
      }
      return;
    }
    if (typeof headers !== "object") {
      return;
    }
    const pairs = Array.isArray(headers) ? headers : Object.entries(headers);
    for (const pair of pairs) {
      if (!Array.isArray(pair) || typeof pair[0] !== "string") {
        continue;
      }
      const [key, value] = pair;
      if (!value || typeof value !== "string") {
        continue;
      }
      const normalized = key.toLowerCase();
      if (normalized === "client-id") {
        gqlClientId = value;
      } else if (normalized === "client-integrity") {
        gqlHeaders["Client-Integrity"] = value;
      } else if (normalized === "x-device-id") {
        gqlHeaders["X-Device-Id"] = value;
      } else if (normalized === "client-session-id") {
        gqlHeaders["Client-Session-Id"] = value;
      } else if (normalized === "client-version") {
        gqlHeaders["Client-Version"] = value;
      } else if (normalized === "authorization") {
        gqlHeaders.Authorization = value;
      }
    }
  }

  function captureOperationHash(payload) {
    if (!payload) {
      return;
    }
    const entries = Array.isArray(payload) ? payload : [payload];
    for (const entry of entries) {
      if (
        entry?.operationName === "ChannelPointsContext" &&
        entry?.extensions?.persistedQuery?.sha256Hash
      ) {
        channelPointsContextHash = entry.extensions.persistedQuery.sha256Hash;
      }
      if (
        entry?.operationName === "ClaimCommunityPoints" &&
        entry?.extensions?.persistedQuery?.sha256Hash
      ) {
        claimCommunityPointsHash = entry.extensions.persistedQuery.sha256Hash;
      }
    }
  }

  function buildGqlHeaders() {
    return {
      "Content-Type": "text/plain;charset=UTF-8",
      "Client-Id": gqlHeaders["Client-Id"] || gqlClientId,
      ...(gqlHeaders["Client-Integrity"]
        ? { "Client-Integrity": gqlHeaders["Client-Integrity"] }
        : {}),
      ...(gqlHeaders["X-Device-Id"]
        ? { "X-Device-Id": gqlHeaders["X-Device-Id"] }
        : {}),
      ...(gqlHeaders["Client-Session-Id"]
        ? { "Client-Session-Id": gqlHeaders["Client-Session-Id"] }
        : {}),
      ...(gqlHeaders["Client-Version"]
        ? { "Client-Version": gqlHeaders["Client-Version"] }
        : {}),
      ...(gqlHeaders.Authorization
        ? { Authorization: gqlHeaders.Authorization }
        : {}),
    };
  }

  function isPersistedQueryNotFound(body) {
    const entries = Array.isArray(body) ? body : [body];
    return entries.some((entry) =>
      entry?.errors?.some(
        (error) =>
          error?.message?.includes("PersistedQueryNotFound") ||
          error?.extensions?.code === "PERSISTED_QUERY_NOT_FOUND"
      )
    );
  }

  function getChannelPointsContextHashes() {
    const hashes = [];
    if (channelPointsContextHash) {
      hashes.push(channelPointsContextHash);
    }
    for (const hash of CHANNEL_POINTS_CONTEXT_HASHES) {
      if (!hashes.includes(hash)) {
        hashes.push(hash);
      }
    }
    return hashes;
  }

  async function refreshChannelPointsContext() {
    const channelLogin = getCurrentChannelLogin();
    if (!channelLogin || document.visibilityState === "hidden") {
      return false;
    }
    if (adapterCircuitOpen()) {
      return false;
    }

    const request = {
      generation: channelGeneration,
      issuedFor: getCurrentChannelLogin(),
      scopes: [{ login: loginOf(channelLogin), channelId: "" }],
    };
    let failure = "network";
    for (const sha256Hash of getChannelPointsContextHashes()) {
      try {
        const response = await fetchWithTimeout("https://gql.twitch.tv/gql", {
          method: "POST",
          credentials: "include",
          headers: buildGqlHeaders(),
          body: JSON.stringify({
            operationName: "ChannelPointsContext",
            variables: { channelLogin },
            extensions: {
              persistedQuery: {
                version: 1,
                sha256Hash,
              },
            },
          }),
        });
        if (!response.ok) {
          failure = response.status === 429 ? "rate-limited" : "network";
          break;
        }
        let body;
        try {
          body = await response.json();
        } catch (_) {
          failure = "schema";
          continue;
        }
        if (isPersistedQueryNotFound(body)) {
          failure = "hash";
          continue;
        }
        channelPointsContextHash = sha256Hash;
        return handleGqlPayload(body, { request });
      } catch (_) {
        failure = "network";
        break;
      }
    }
    recordAdapterFailure(failure);
    emitUpdate();
    return false;
  }

  function hasGraphqlErrors(body) {
    const entries = Array.isArray(body) ? body : [body];
    return entries.some(
      (entry) => Array.isArray(entry?.errors) && entry.errors.length > 0
    );
  }

  function scheduleContextPoll(delayMs = CONTEXT_POLL_MS) {
    clearTimeout(contextPollTimer);
    contextPollTimer = null;
    if (!isPollingAllowed()) {
      return;
    }
    const cooldownDelay = Math.max(0, state.adapter.cooldownUntil - Date.now());
    contextPollTimer = setTimeout(
      async () => {
        try {
          await refreshChannelPointsContext();
        } finally {
          scheduleContextPoll();
        }
      },
      Math.max(delayMs, cooldownDelay)
    );
  }

  function restartContextPolling() {
    clearTimeout(contextPollTimer);
    contextPollTimer = null;
    scheduleContextPoll(CONTEXT_POLL_INITIAL_MS);
  }

  async function claimViaGql(channelId, claimId) {
    if (!channelId || !claimId || claimInFlight) {
      return false;
    }
    claimInFlight = true;
    try {
      const response = await fetchWithTimeout("https://gql.twitch.tv/gql", {
        method: "POST",
        credentials: "include",
        headers: buildGqlHeaders(),
        body: JSON.stringify({
          operationName: "ClaimCommunityPoints",
          variables: {
            input: {
              channelID: String(channelId),
              claimID: claimId,
            },
          },
          extensions: {
            persistedQuery: {
              version: 1,
              sha256Hash: claimCommunityPointsHash,
            },
          },
        }),
      });
      if (!response.ok) {
        recordAdapterFailure(
          response.status === 429 ? "rate-limited" : "network"
        );
        emitUpdate();
        return false;
      }
      let body;
      try {
        body = await response.json();
      } catch (_) {
        recordAdapterFailure("schema");
        emitUpdate();
        return false;
      }
      if (!handleGqlPayload(body)) {
        return false;
      }
      const entries = Array.isArray(body) ? body : [body];
      const claimFailed = entries.some(
        (entry) =>
          entry?.errors?.length || entry?.data?.claimCommunityPoints?.error
      );
      if (claimFailed) {
        return false;
      }
      state.channelPoints.claimAvailable = false;
      state.channelPoints.claimId = null;
      emitUpdate();
      return true;
    } catch (_) {
      recordAdapterFailure("network");
      emitUpdate();
      return false;
    } finally {
      claimInFlight = false;
    }
  }

  async function claimChannelPoints() {
    const now = Date.now();
    if (now - lastClaimAttemptAt < CLAIM_RETRY_MIN_MS) {
      return false;
    }

    let { channelId, claimId, claimAvailable } = state.channelPoints;
    if (!claimAvailable) {
      return false;
    }

    // Throttle every attempt, including the context refresh below, so a burst
    // of claim-request events cannot turn into a burst of GraphQL requests.
    lastClaimAttemptAt = now;
    if (!channelId || !claimId) {
      await refreshChannelPointsContext();
      ({ channelId, claimId, claimAvailable } = state.channelPoints);
    }

    if (!claimAvailable || !channelId || !claimId) {
      return false;
    }

    const claimed = await claimViaGql(channelId, claimId);
    if (claimed) {
      return true;
    }

    await refreshChannelPointsContext();
    ({ channelId, claimId, claimAvailable } = state.channelPoints);
    if (!claimAvailable || !channelId || !claimId) {
      return false;
    }

    return claimViaGql(channelId, claimId);
  }

  // Returns the parsed request payload (or null) for describeGqlRequest.
  function captureGqlRequestBody(body) {
    if (!body) {
      return null;
    }
    try {
      const payload = typeof body === "string" ? JSON.parse(body) : body;
      captureOperationHash(payload);
      return payload && typeof payload === "object" ? payload : null;
    } catch (_) {
      // ignore malformed bodies
      return null;
    }
  }

  // Only Twitch's own GraphQL traffic is observed. Other requests on the page
  // (analytics beacons, 7TV/BTTV APIs) are neither parsed nor have their
  // Authorization headers captured, so a third-party token can never be
  // replayed to Twitch.
  const GQL_URL_PATTERN = /^https:\/\/gql\.twitch\.tv(?::443)?(?:[/?#]|$)/i;

  function isGqlUrl(input) {
    try {
      const raw =
        typeof input === "string" ? input : input?.href || input?.url || "";
      return GQL_URL_PATTERN.test(String(raw));
    } catch (_) {
      return false;
    }
  }

  const originalFetch = window.fetch;
  window.fetch = async function (...args) {
    let isGql = false;
    let request = null;
    try {
      isGql = isGqlUrl(args[0]);
      if (isGql) {
        // Captured before anything else can throw: a response must never be
        // attributed to a channel generation later than its request's.
        request = describeGqlRequest(null);
        const input = args[0];
        if (input && typeof input === "object" && input.headers) {
          // fetch(new Request(...)): headers live on the Request object.
          captureGqlClientId(input.headers);
        }
        captureGqlClientId(args[1]?.headers);
        request = describeGqlRequest(captureGqlRequestBody(args[1]?.body));
      }
    } catch (_) {
      // ignore header capture errors
    }
    const response = await originalFetch.apply(this, args);
    try {
      if (isGql) {
        const clone = response.clone();
        clone
          .text()
          .then((text) => handleObservedPayload(text, request))
          .catch(() => {});
      }
    } catch (_) {
      // ignore hook errors
    }
    return response;
  };

  const originalOpen = XMLHttpRequest.prototype.open;
  const originalSend = XMLHttpRequest.prototype.send;

  XMLHttpRequest.prototype.open = function (method, url, ...rest) {
    try {
      this.__ccIsGql = isGqlUrl(url);
    } catch (_) {
      // never block Twitch's own request
    }
    return originalOpen.call(this, method, url, ...rest);
  };

  function onGqlXhrLoad() {
    try {
      // open() may since have targeted a different URL on a reused XHR.
      if (!this.__ccIsGql) {
        return;
      }
      const request = this.__ccRequest || null;
      if (this.responseType === "json") {
        if (this.response && typeof this.response === "object") {
          handleObservedPayload(this.response, request);
        }
        return;
      }
      if (this.responseType && this.responseType !== "text") {
        return;
      }
      if (this.responseText) {
        handleObservedPayload(this.responseText, request);
      }
    } catch (_) {
      // Never let a private GraphQL observer alter Twitch's XHR lifecycle.
    }
  }

  XMLHttpRequest.prototype.send = function (...args) {
    if (!this.__ccIsGql) {
      return originalSend.apply(this, args);
    }
    try {
      this.__ccRequest = describeGqlRequest(null);
      this.__ccRequest = describeGqlRequest(captureGqlRequestBody(args[0]));
    } catch (_) {
      // ignore
    }
    try {
      // A reused XHR is sent repeatedly; attach the observer only once.
      if (!this.__ccLoadHooked) {
        this.addEventListener("load", onGqlXhrLoad);
        this.__ccLoadHooked = true;
      }
    } catch (_) {
      // Preserve the original XHR behavior if listener instrumentation fails.
    }
    return originalSend.apply(this, args);
  };

  // Twitch expects one minute-watched event per stream per minute. Anything
  // on the page can dispatch the trigger event, so sends are throttled per
  // stream, capped overall, limited to the channel this page shows, and back
  // off globally after rate limiting or server errors.
  //
  // A shared secret between content script and this script was considered
  // and rejected: every channel between the two worlds (a dataset attribute
  // or URL on the injected <script>, window events, postMessage ports) is
  // readable by page scripts before or while it is used - a page
  // MutationObserver sees the <script> before it runs, and an earlier
  // listener sees each event's detail first - so a nonce would only look
  // like protection. The limits below bound what a spoofed event can do.
  const MINUTE_WATCHED_MIN_INTERVAL_MS = 50000;
  const MINUTE_WATCHED_BACKOFF_BASE_MS = 60000;
  const MINUTE_WATCHED_BACKOFF_MAX_MS = 15 * 60000;
  const MINUTE_WATCHED_MAX_TRACKED = 50;
  const MINUTE_WATCHED_WINDOW_MS = 60000;
  const MINUTE_WATCHED_MAX_PER_WINDOW = 3;
  const minuteWatchedLastSent = new Map();
  let minuteWatchedRecentSends = [];
  let minuteWatchedBackoffUntil = 0;
  let minuteWatchedBackoffMs = 0;

  function minuteWatchedThrottle(key) {
    const now = Date.now();
    if (minuteWatchedBackoffUntil > now) {
      return "backoff";
    }
    const last = minuteWatchedLastSent.get(key) || 0;
    if (now - last < MINUTE_WATCHED_MIN_INTERVAL_MS) {
      return "rate-limited";
    }
    // One tab watches one stream; a stream restart or channel switch may
    // add another key, a stream of distinct fake keys may not.
    minuteWatchedRecentSends = minuteWatchedRecentSends.filter(
      (sentAt) => now - sentAt < MINUTE_WATCHED_WINDOW_MS
    );
    if (minuteWatchedRecentSends.length >= MINUTE_WATCHED_MAX_PER_WINDOW) {
      return "rate-limited";
    }
    minuteWatchedRecentSends.push(now);
    minuteWatchedLastSent.delete(key);
    minuteWatchedLastSent.set(key, now);
    while (minuteWatchedLastSent.size > MINUTE_WATCHED_MAX_TRACKED) {
      minuteWatchedLastSent.delete(minuteWatchedLastSent.keys().next().value);
    }
    return "";
  }

  function recordMinuteWatchedOutcome(status) {
    if (status === 429 || status >= 500) {
      minuteWatchedBackoffMs = Math.min(
        MINUTE_WATCHED_BACKOFF_MAX_MS,
        minuteWatchedBackoffMs ? minuteWatchedBackoffMs * 2 : MINUTE_WATCHED_BACKOFF_BASE_MS
      );
      minuteWatchedBackoffUntil = Date.now() + minuteWatchedBackoffMs;
    } else if (status >= 200 && status < 300) {
      minuteWatchedBackoffMs = 0;
      minuteWatchedBackoffUntil = 0;
    }
  }

  async function sendMinuteWatched(detail) {
    if (!detail || typeof detail !== "object") {
      return { ok: false, error: "invalid-payload" };
    }
    const channelId = normalizeId(detail.channelId);
    const channelLogin = String(detail.channel || detail.channelLogin || "")
      .toLowerCase()
      .slice(0, 64);
    const streamId = normalizeId(detail.streamId);
    const userId = normalizeId(detail.userId);

    if (!channelId || !streamId) {
      return { ok: false, error: "missing-parameters" };
    }
    // background.js only dispatches for the channel of this tab's session.
    const currentChannel = getCurrentChannelLogin();
    if (channelLogin && currentChannel && channelLogin !== currentChannel) {
      return { ok: false, error: "channel-mismatch" };
    }
    const throttled = minuteWatchedThrottle(`${channelId}:${streamId}`);
    if (throttled) {
      return { ok: false, error: throttled };
    }

    const spadeEvent = {
      event: "minute-watched",
      properties: {
        channel_id: channelId,
        broadcast_id: streamId,
        channel: channelLogin,
        user_id: userId,
        live: true,
        is_live: true,
        player: "site",
        client_time: new Date().toISOString(),
        logged_in: Boolean(userId),
        minutes_logged: 1,
        hidden: false,
        muted: false,
      },
    };

    const base64Data = btoa(JSON.stringify([spadeEvent]));
    const mutationQuery = `mutation SendEvents($input: SendSpadeEventsInput!) {
      sendSpadeEvents(input: $input) {
        statusCode
      }
    }`;

    try {
      const response = await fetchWithTimeout("https://gql.twitch.tv/gql", {
        method: "POST",
        credentials: "include",
        headers: buildGqlHeaders(),
        body: JSON.stringify({
          query: mutationQuery,
          variables: {
            input: {
              data: base64Data,
            },
          },
        }),
      });

      recordMinuteWatchedOutcome(response.status);
      if (!response.ok) {
        return { ok: false, statusCode: response.status, error: `http-${response.status}` };
      }

      let json;
      try {
        json = await response.json();
      } catch (_) {
        return { ok: false, error: "parse-error" };
      }

      const statusCode = json?.data?.sendSpadeEvents?.statusCode;
      if (statusCode === 200 || statusCode === 204 || (!json?.errors && !statusCode)) {
        return { ok: true, statusCode: statusCode || response.status };
      }

      if (json?.errors?.length) {
        const message = json.errors[0]?.message;
        return {
          ok: false,
          statusCode,
          error: typeof message === "string" ? message.slice(0, 200) : "gql-error",
        };
      }

      return { ok: true, statusCode: statusCode || response.status };
    } catch (err) {
      // Only a generic reason leaves this closure.
      return {
        ok: false,
        error: err?.name === "AbortError" ? "timeout" : "network-error",
      };
    }
  }

  window.__chatterinoCompanionGql = {
    __version: 2,
    getState() {
      return structuredClone(state);
    },
    getAdapterStatus() {
      return adapterStatus();
    },
    async refreshContext() {
      return refreshChannelPointsContext();
    },
    async claimChannelPoints() {
      return claimChannelPoints();
    },
    async sendMinuteWatched(detail) {
      return sendMinuteWatched(detail);
    },
    redeemReward(rewardId, channelId) {
      window.dispatchEvent(
        new CustomEvent("chatterino-companion-redeem", {
          detail: { rewardId, channelId },
        })
      );
    },
  };

  window.addEventListener("chatterino-companion-minute-watched", async (event) => {
    let result;
    try {
      result = await sendMinuteWatched(event.detail);
    } catch (_) {
      result = { ok: false, error: "internal-error" };
    }
    window.dispatchEvent(
      new CustomEvent("chatterino-companion-minute-watched-result", {
        detail: {
          ...result,
          requestId: event.detail?.requestId,
          channel: event.detail?.channel,
          streamId: event.detail?.streamId,
          sessionId: event.detail?.sessionId,
        },
      })
    );
  });

  window.addEventListener("chatterino-companion-claim-request", () => {
    void window.__chatterinoCompanionGql.claimChannelPoints();
  });

  window.addEventListener("chatterino-companion-channel-change", (event) => {
    cancelPendingReward("channel-change");
    const requested = event.detail?.channel;
    resetStateForChannel(
      typeof requested === "string" && requested
        ? requested
        : getCurrentChannelLogin()
    );
    emitUpdate();
    restartContextPolling();
  });

  window.addEventListener("chatterino-companion-reward-cancelled", () => {
    clearPendingRewardTimeout();
  });

  document.addEventListener("visibilitychange", () => {
    if (document.visibilityState === "hidden") {
      clearTimeout(contextPollTimer);
      contextPollTimer = null;
      return;
    }
    restartContextPolling();
  });

  document.addEventListener(
    "keydown",
    (event) => {
      if (event.key === "Escape" && pendingRewardTimeoutId) {
        cancelPendingReward("escape");
      }
    },
    true
  );

  emitUpdate();
  restartContextPolling();
})();
