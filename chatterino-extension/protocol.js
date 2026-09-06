(function (global) {
  "use strict";

  // CURRENT_VERSION remains the highest version understood by the currently
  // shipped native host.  Version 2 is additive and can be negotiated by a
  // newer host without making the v0/v1 migration path unusable.
  const CURRENT_VERSION = 1;
  const V2_VERSION = 2;
  const NATIVE_ACTIONS = new Set([
    "select",
    "detach",
    "sync",
    "engagement",
    "prediction",
    "pin",
    "rewardPending",
    "rewardClear",
    "leaseRenew",
    "reconcile",
    "nativeChatResult",
  ]);

  const SESSION_FIELDS = [
    "sessionId",
    "browserWindowId",
    "tabId",
    "generation",
  ];

  function validate(message) {
    if (!message || typeof message !== "object") {
      return { ok: false, error: "message must be an object" };
    }
    if (!NATIVE_ACTIONS.has(message.action)) {
      return {
        ok: false,
        error: `unknown native action: ${message.action || "<missing>"}`,
      };
    }
    const version = message.protocolVersion ?? 0;
    if (!Number.isInteger(version) || version < 0 || version > V2_VERSION) {
      return { ok: false, error: `unsupported protocol version: ${version}` };
    }
    if (version >= V2_VERSION) {
      if (
        ![
          "select",
          "detach",
          "leaseRenew",
          "reconcile",
          "nativeChatResult",
        ].includes(message.action)
      ) {
        return {
          ok: false,
          error: `protocol v2 does not support action: ${message.action}`,
        };
      }
      if (
        message.action !== "reconcile" &&
        !String(message.sessionId || "").trim()
      ) {
        return { ok: false, error: "sessionId is required for protocol v2" };
      }
      if (
        message.action !== "reconcile" &&
        (!Number.isInteger(message.generation) || message.generation < 0)
      ) {
        return { ok: false, error: "generation is required for protocol v2" };
      }
      if (
        message.action !== "reconcile" &&
        (message.browserWindowId === undefined ||
          (typeof message.browserWindowId !== "number" &&
            typeof message.browserWindowId !== "string") ||
          String(message.browserWindowId).trim() === "" ||
          !Number.isInteger(message.tabId))
      ) {
        return {
          ok: false,
          error: "browserWindowId and tabId are required for protocol v2",
        };
      }
      if (
        ["select", "leaseRenew"].includes(message.action) &&
        !String(message.name || message.channel || "").trim()
      ) {
        return { ok: false, error: "channel/name is required for protocol v2" };
      }
      if (
        message.action === "leaseRenew" &&
        !Number.isFinite(message.leaseExpiresAt)
      ) {
        return {
          ok: false,
          error: "leaseExpiresAt is required for lease renewal",
        };
      }
      if (message.action === "nativeChatResult") {
        if (
          typeof message.sessionId !== "string" ||
          !message.sessionId.trim() ||
          message.sessionId.length > 256 ||
          (!Number.isInteger(message.browserWindowId) &&
            !(
              typeof message.browserWindowId === "string" &&
              /^(?:0|[1-9][0-9]*)$/.test(message.browserWindowId)
            )) ||
          message.tabId < 0 ||
          typeof message.requestId !== "string" ||
          !/^[a-z0-9._:-]{1,128}$/i.test(message.requestId)
        ) {
          return {
            ok: false,
            error: "native chat result identity or requestId is invalid",
          };
        }
        if (!["accepted", "rejected", "uncertain"].includes(message.status)) {
          return { ok: false, error: "native chat result status is invalid" };
        }
        if (
          typeof message.reason !== "string" ||
          message.reason.length > 96 ||
          !/^[a-z0-9][a-z0-9._-]*$/i.test(message.reason)
        ) {
          return { ok: false, error: "native chat result reason is invalid" };
        }
      }
    }
    if (message.action === "engagement" && version >= 1) {
      if (!["poll", "prediction"].includes(message.kind)) {
        return {
          ok: false,
          error: "engagement kind must be poll or prediction",
        };
      }
      if (!["upsert", "remove"].includes(message.lifecycle)) {
        return {
          ok: false,
          error: "engagement lifecycle must be upsert or remove",
        };
      }
      if (!String(message.channel || "").trim()) {
        return { ok: false, error: "engagement channel is required" };
      }
      if (
        message.lifecycle === "upsert" &&
        !String(message.title || "").trim()
      ) {
        return { ok: false, error: "engagement title is required for upsert" };
      }
    }
    return { ok: true, version };
  }

  function normalizeOutbound(message) {
    const requestedVersion = message.protocolVersion ?? CURRENT_VERSION;
    const normalized = { ...message, protocolVersion: requestedVersion };
    const validation = validate(normalized);
    if (!validation.ok) {
      throw new TypeError(validation.error);
    }
    return normalized;
  }

  function create(action, payload = {}) {
    return normalizeOutbound({ action, ...payload });
  }

  function createV2(action, payload = {}) {
    return { ...payload, action, protocolVersion: V2_VERSION };
  }

  global.ChatterinoProtocol = {
    CURRENT_VERSION,
    V2_VERSION,
    NATIVE_ACTIONS,
    SESSION_FIELDS,
    create,
    createV2,
    normalizeOutbound,
    validate,
  };
})(globalThis);
