(function () {
  "use strict";

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
  };

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

  function resetStateForChannel(channelLogin) {
    lastKnownClaimId = null;
    state.channelPoints = {
      balance: null,
      claimAvailable: false,
      claimId: null,
      channelId: null,
      channelLogin: channelLogin || null,
    };
    state.prediction = null;
    state.poll = null;
    state.rewards = [];
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
    if (typeof availableClaim === "object") {
      const claimId =
        availableClaim.id ||
        availableClaim.claimID ||
        availableClaim.claimId ||
        "";
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
    if (channelId) {
      state.channelPoints.channelId = channelId;
    }
    if (channelLogin) {
      state.channelPoints.channelLogin = channelLogin.toLowerCase();
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
    if (value == null || Number.isNaN(Number(value))) {
      return null;
    }
    const num = Number(value);
    if (num >= 1_000_000) {
      return (num / 1_000_000).toFixed(1).replace(/\.0$/, "") + "M";
    }
    if (num >= 1_000) {
      return (num / 1_000).toFixed(1).replace(/\.0$/, "") + "K";
    }
    return String(num);
  }

  function mapReward(r) {
    return {
      id: r.id || r.rewardID || "",
      title: r.title || r.name || "Reward",
      cost: r.cost ?? r.defaultCost ?? 0,
      prompt: r.prompt || r.defaultPrompt || "",
      isUserInputRequired: Boolean(r.isUserInputRequired),
      channelId: r.channelID || r.channel_id || "",
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

  let lastKnownClaimId = null;

  function emitUpdate() {
    state.lastUpdate = Date.now();

    const root = document.documentElement;
    root.setAttribute("data-cc-gql-balance", state.channelPoints.balance || "");
    root.setAttribute(
      "data-cc-gql-claim",
      state.channelPoints.claimAvailable ? "1" : "0"
    );
    root.setAttribute(
      "data-cc-gql-claim-id",
      state.channelPoints.claimId || ""
    );
    root.setAttribute(
      "data-cc-gql-channel-id",
      state.channelPoints.channelId || ""
    );
    root.setAttribute(
      "data-cc-gql-prediction",
      state.prediction?.title ? JSON.stringify(state.prediction) : ""
    );
    root.setAttribute(
      "data-cc-gql-poll",
      state.poll?.title ? JSON.stringify(state.poll) : ""
    );
    root.setAttribute(
      "data-cc-gql-rewards",
      JSON.stringify(state.rewards.slice(0, 80))
    );
    root.setAttribute("data-cc-gql-updated", String(state.lastUpdate));

    window.dispatchEvent(
      new CustomEvent("chatterino-companion-gql", {
        detail: structuredClone(state),
      })
    );

    const claimId = state.channelPoints.claimId;
    if (
      state.channelPoints.claimAvailable &&
      claimId &&
      claimId !== lastKnownClaimId
    ) {
      lastKnownClaimId = claimId;
      window.dispatchEvent(
        new CustomEvent("chatterino-companion-claim-request")
      );
    }
    if (!state.channelPoints.claimAvailable) {
      lastKnownClaimId = null;
    }
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

  function parsePredictionEvent(event) {
    if (!event || typeof event !== "object") {
      return null;
    }
    const statusRaw = (
      event.status ||
      event.predictionStatus ||
      ""
    ).toLowerCase();
    let status = "started";
    if (statusRaw.includes("lock") || statusRaw.includes("close")) {
      status = "locked";
    } else if (
      statusRaw.includes("end") ||
      statusRaw.includes("resolve") ||
      statusRaw.includes("cancel")
    ) {
      status = "ended";
    }

    const outcomes = event.outcomes || event.predictionOutcomes || [];
    const options = outcomes
      .map((o) => o.title || o.name || o.label)
      .filter(Boolean);
    const title = String(event.title || event.predictionTitle || "").trim();
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
      winner: event.winningOutcome?.title || event.winner || "",
    };
  }

  function parsePollEvent(event) {
    if (!event || typeof event !== "object") {
      return null;
    }
    const statusRaw = (event.status || event.pollStatus || "").toLowerCase();
    let status = "started";
    if (statusRaw.includes("lock") || statusRaw.includes("close")) {
      status = "locked";
    } else if (
      statusRaw.includes("end") ||
      statusRaw.includes("complete") ||
      statusRaw.includes("cancel")
    ) {
      status = "ended";
    }

    const choices = event.choices || event.pollChoices || event.outcomes || [];
    const options = choices
      .map(
        (choice) =>
          choice.title || choice.name || choice.label || choice.choiceText
      )
      .filter(Boolean);
    const title = String(event.title || event.pollTitle || "").trim();
    if (!title && options.length < 2) {
      return null;
    }
    const duration = Number(
      event.durationSeconds || event.remainingDurationSeconds || 0
    );

    return {
      title: title || "Poll",
      options,
      status,
      duration: Number.isFinite(duration) && duration > 0 ? duration : 0,
      closesAt: eventDeadline(event, duration),
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

  function walkJson(node, visitor) {
    if (!node || typeof node !== "object") {
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
    if (!body || typeof body !== "object") {
      return;
    }

    walkJson(body, (obj) => {
      const responseChannelLogin = obj.channel?.login?.toLowerCase?.() || null;

      if (
        obj.community?.channel != null &&
        typeof obj.community.channel === "object"
      ) {
        const channel = obj.community.channel;
        const channelLogin =
          channel.login?.toLowerCase?.() || responseChannelLogin;
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
          state.channelPoints.balance = formatBalance(
            obj.channel.communityPoints.balance
          );
        }
        if (obj.channel.communityPoints?.availableClaim != null) {
          applyAvailableClaim(
            obj.channel.communityPoints.availableClaim,
            obj.channel.login?.toLowerCase?.() || channelLogin
          );
        }
        if (obj.channel.self?.communityPoints != null) {
          applyCommunityPointsSnapshot(
            obj.channel.self.communityPoints,
            obj.channel.login?.toLowerCase?.() || channelLogin,
            obj.channel.id
          );
        }
      }

      if (obj.communityPredictionEvent != null) {
        const parsed = parsePredictionEvent(obj.communityPredictionEvent);
        if (parsed) {
          state.prediction = parsed;
        }
      }

      if (obj.communityPollEvent != null) {
        const parsed = parsePollEvent(obj.communityPollEvent);
        if (parsed) {
          state.poll = parsed;
        }
      }

      const genericEventKind =
        obj.event != null && typeof obj.event === "object"
          ? getEventKind(obj, obj.event)
          : "";

      if (genericEventKind === "prediction") {
        const parsed = parsePredictionEvent(obj.event);
        if (parsed) {
          state.prediction = parsed;
        }
      }

      if (genericEventKind === "poll") {
        const parsed = parsePollEvent(obj.event);
        if (parsed) {
          state.poll = parsed;
        }
      }

      if (
        obj.prediction != null &&
        typeof obj.prediction === "object" &&
        getEventKind(obj, obj.prediction) === "prediction"
      ) {
        const parsed = parsePredictionEvent(obj.prediction);
        if (parsed) {
          state.prediction = parsed;
        }
      }

      if (
        obj.poll != null &&
        typeof obj.poll === "object" &&
        getEventKind(obj, obj.poll) === "poll"
      ) {
        const parsed = parsePollEvent(obj.poll);
        if (parsed) {
          state.poll = parsed;
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
          .map(mapReward);
      }

      if (Array.isArray(obj.communityPointsSettings?.customRewards)) {
        state.rewards = obj.communityPointsSettings.customRewards
          .filter((r) => r && (r.title || r.name))
          .map(mapReward);
      }

      extractRedemptionFromPayload(obj);
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
      const body = typeof payload === "string" ? JSON.parse(payload) : payload;
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
    const clientId = headers["Client-Id"] || headers["client-id"];
    if (clientId) {
      gqlClientId = clientId;
    }
    for (const [key, value] of Object.entries(headers)) {
      if (!value) {
        continue;
      }
      const normalized = key.toLowerCase();
      if (normalized === "client-integrity") {
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
    if (!channelLogin) {
      return false;
    }

    for (const sha256Hash of getChannelPointsContextHashes()) {
      try {
        const response = await fetch("https://gql.twitch.tv/gql", {
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
          continue;
        }
        const body = await response.json();
        if (isPersistedQueryNotFound(body)) {
          continue;
        }
        channelPointsContextHash = sha256Hash;
        handleGqlPayload(body);
        return true;
      } catch (_) {
        // try next hash
      }
    }
    return false;
  }

  function scheduleContextPoll() {
    clearTimeout(contextPollTimer);
    contextPollTimer = setTimeout(async () => {
      await refreshChannelPointsContext();
      scheduleContextPoll();
    }, CONTEXT_POLL_MS);
  }

  function restartContextPolling() {
    clearTimeout(contextPollTimer);
    void refreshChannelPointsContext();
    contextPollTimer = setTimeout(async () => {
      await refreshChannelPointsContext();
      scheduleContextPoll();
    }, CONTEXT_POLL_INITIAL_MS);
  }

  async function claimViaGql(channelId, claimId) {
    if (!channelId || !claimId || claimInFlight) {
      return false;
    }
    claimInFlight = true;
    try {
      const response = await fetch("https://gql.twitch.tv/gql", {
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
        return false;
      }
      const body = await response.json();
      handleGqlPayload(body);
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

    if (!channelId || !claimId) {
      await refreshChannelPointsContext();
      ({ channelId, claimId, claimAvailable } = state.channelPoints);
    }

    if (!claimAvailable || !channelId || !claimId) {
      return false;
    }

    lastClaimAttemptAt = now;
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

  function captureGqlRequestBody(body) {
    if (!body) {
      return;
    }
    try {
      const payload = typeof body === "string" ? JSON.parse(body) : body;
      captureOperationHash(payload);
    } catch (_) {
      // ignore malformed bodies
    }
  }

  const originalFetch = window.fetch;
  window.fetch = async function (...args) {
    try {
      captureGqlClientId(args[1]?.headers);
      captureGqlRequestBody(args[1]?.body);
    } catch (_) {
      // ignore header capture errors
    }
    const response = await originalFetch.apply(this, args);
    try {
      const url = typeof args[0] === "string" ? args[0] : args[0]?.url;
      if (url && url.includes("gql.twitch.tv")) {
        const clone = response.clone();
        clone
          .text()
          .then(handleGqlPayload)
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
    this.__ccGqlUrl = url;
    return originalOpen.call(this, method, url, ...rest);
  };

  XMLHttpRequest.prototype.send = function (...args) {
    try {
      captureGqlRequestBody(args[0]);
    } catch (_) {
      // ignore
    }
    this.addEventListener("load", function () {
      try {
        if (
          this.__ccGqlUrl &&
          String(this.__ccGqlUrl).includes("gql.twitch.tv") &&
          this.responseText
        ) {
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
      return claimChannelPoints();
    },
    redeemReward(rewardId, channelId) {
      window.dispatchEvent(
        new CustomEvent("chatterino-companion-redeem", {
          detail: { rewardId, channelId },
        })
      );
    },
  };

  window.addEventListener("chatterino-companion-claim-request", () => {
    void window.__chatterinoCompanionGql.claimChannelPoints();
  });

  window.addEventListener("chatterino-companion-channel-change", (event) => {
    cancelPendingReward("channel-change");
    resetStateForChannel(event.detail?.channel || getCurrentChannelLogin());
    emitUpdate();
    restartContextPolling();
  });

  window.addEventListener("chatterino-companion-reward-cancelled", () => {
    clearPendingRewardTimeout();
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
