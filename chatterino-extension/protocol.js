(function (global) {
  "use strict";

  const CURRENT_VERSION = 1;
  const NATIVE_ACTIONS = new Set([
    "select",
    "detach",
    "sync",
    "engagement",
    "prediction",
    "pin",
    "rewardPending",
    "rewardClear",
  ]);

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
    if (
      !Number.isInteger(version) ||
      version < 0 ||
      version > CURRENT_VERSION
    ) {
      return { ok: false, error: `unsupported protocol version: ${version}` };
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
    const normalized = { ...message, protocolVersion: CURRENT_VERSION };
    const validation = validate(normalized);
    if (!validation.ok) {
      throw new TypeError(validation.error);
    }
    return normalized;
  }

  function create(action, payload = {}) {
    return normalizeOutbound({ action, ...payload });
  }

  global.ChatterinoProtocol = {
    CURRENT_VERSION,
    NATIVE_ACTIONS,
    create,
    normalizeOutbound,
    validate,
  };
})(globalThis);
