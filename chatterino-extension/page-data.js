(function (global) {
  "use strict";

  // Data from twitch-api.js reaches the content script through <html>
  // attributes and a window CustomEvent. Both live in the page's world, so any
  // script on twitch.tv can write them. Everything read from them is reduced
  // to plain, bounded JSON before the extension uses it.
  const LIMITS = Object.freeze({
    depth: 6,
    arrayLength: 200,
    objectKeys: 100,
    stringLength: 2000,
  });
  const BLOCKED_KEYS = new Set(["__proto__", "prototype", "constructor"]);

  function sanitizeJson(value, depth = 0) {
    if (value === null) return null;
    switch (typeof value) {
      case "string":
        return value.length > LIMITS.stringLength
          ? value.slice(0, LIMITS.stringLength)
          : value;
      case "number":
        return Number.isFinite(value) ? value : null;
      case "boolean":
        return value;
      case "object":
        break;
      default:
        return undefined;
    }
    if (depth >= LIMITS.depth) return undefined;
    if (Array.isArray(value)) {
      const out = [];
      for (const item of value.slice(0, LIMITS.arrayLength)) {
        const clean = sanitizeJson(item, depth + 1);
        if (clean !== undefined) out.push(clean);
      }
      return out;
    }
    // Compare by tag, not prototype: event details and test fixtures come from
    // another JavaScript realm whose Object.prototype differs from ours.
    if (Object.prototype.toString.call(value) !== "[object Object]") {
      return undefined;
    }
    const out = {};
    let keys = 0;
    for (const key of Object.keys(value)) {
      if (BLOCKED_KEYS.has(key)) continue;
      if (++keys > LIMITS.objectKeys) break;
      const clean = sanitizeJson(value[key], depth + 1);
      if (clean !== undefined) out[key] = clean;
    }
    return out;
  }

  function parseJson(raw) {
    if (typeof raw !== "string" || !raw) return undefined;
    try {
      return JSON.parse(raw);
    } catch (_) {
      return undefined;
    }
  }

  function isPlainObject(value) {
    return Boolean(value) && typeof value === "object" && !Array.isArray(value);
  }

  // A poll or prediction is only usable with a non-empty string title.
  function sanitizeActivity(value) {
    const clean = sanitizeJson(value);
    if (!isPlainObject(clean)) return null;
    if (typeof clean.title !== "string" || !clean.title.trim()) return null;
    if (clean.options !== undefined && !Array.isArray(clean.options)) {
      clean.options = [];
    }
    return clean;
  }

  // The legacy attribute fallback accepts a bare title string.
  function parseActivityAttribute(raw) {
    if (typeof raw !== "string" || !raw) return null;
    const parsed = parseJson(raw);
    if (parsed === undefined) {
      return sanitizeActivity({ title: raw, options: [], status: "started" });
    }
    return sanitizeActivity(parsed);
  }

  function sanitizeRewards(value) {
    const clean = sanitizeJson(value);
    return Array.isArray(clean) ? clean.filter(isPlainObject) : [];
  }

  function parseRewardsAttribute(raw) {
    return sanitizeRewards(parseJson(raw));
  }

  // Balances are shown as text ("1,240", "12.3K"), so only digits, separators
  // and a magnitude suffix are kept.
  function sanitizeBalance(value) {
    if (typeof value === "number" && Number.isFinite(value)) {
      return String(Math.max(0, Math.trunc(value)));
    }
    if (typeof value !== "string") return "";
    const trimmed = value.trim().slice(0, 32);
    return /^[0-9][0-9.,\s  ]*[kKmMbB]?$/.test(trimmed) ? trimmed : "";
  }

  function sanitizeId(value) {
    return typeof value === "string" && /^[A-Za-z0-9_-]{1,128}$/.test(value)
      ? value
      : "";
  }

  // Full snapshot from the chatterino-companion-gql event.
  function sanitizeSnapshot(value) {
    const clean = sanitizeJson(value);
    if (!isPlainObject(clean)) return null;
    const points = isPlainObject(clean.channelPoints) ? clean.channelPoints : {};
    return {
      ...clean,
      channelPoints: {
        ...points,
        balance: sanitizeBalance(points.balance),
        claimAvailable: points.claimAvailable === true,
        claimId: sanitizeId(points.claimId),
        channelId: sanitizeId(points.channelId),
      },
      prediction: sanitizeActivity(clean.prediction),
      poll: sanitizeActivity(clean.poll),
      rewards: sanitizeRewards(clean.rewards),
      adapter: isPlainObject(clean.adapter) ? clean.adapter : undefined,
    };
  }

  // Twitch can keep an ended poll's results card mounted indefinitely. The
  // DOM copy of an ended activity is dropped after the same grace period the
  // page script applies to GraphQL activities (twitch-api.js), so the desktop
  // banner clears. filter() returns the details, or null once the same ended
  // activity has been seen for ttlMs; anything not ended resets the clock.
  function createEndedActivityExpiry({
    ttlMs = 30000,
    now = () => Date.now(),
  } = {}) {
    const seen = new Map();
    const identity = (details) =>
      JSON.stringify([
        String(details.title || "").trim(),
        Array.isArray(details.options)
          ? details.options.map((option) =>
              String(
                typeof option === "string" ? option : option?.title || ""
              ).trim()
            )
          : [],
      ]);
    return {
      filter(kind, details) {
        const status =
          details && typeof details.status === "string"
            ? details.status.trim().toLowerCase()
            : "";
        if (status !== "ended") {
          seen.delete(kind);
          return details;
        }
        const key = identity(details);
        const entry = seen.get(kind);
        if (!entry || entry.key !== key) {
          seen.set(kind, { key, since: now() });
          return details;
        }
        return now() - entry.since >= ttlMs ? null : details;
      },
      reset() {
        seen.clear();
      },
    };
  }

  global.ChatterinoPageData = {
    createEndedActivityExpiry,
    LIMITS,
    parseActivityAttribute,
    parseRewardsAttribute,
    sanitizeActivity,
    sanitizeBalance,
    sanitizeId,
    sanitizeJson,
    sanitizeSnapshot,
  };
})(window);
