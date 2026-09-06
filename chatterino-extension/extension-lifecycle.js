(function (global) {
  "use strict";

  const MAX_RING_ENTRIES = 64;
  const SAFE_STATES = new Set([
    "idle",
    "connecting",
    "connected",
    "backoff",
    "blocked",
    "degraded",
  ]);

  function nowOr(clock) {
    return typeof clock === "function" ? clock : Date.now;
  }

  function createBackoffPolicy({
    baseMs = 1000,
    maxMs = 60000,
    maxAttempts = 8,
    clock = Date.now,
  } = {}) {
    let attempt = 0;
    const now = nowOr(clock);
    return {
      reset() {
        attempt = 0;
      },
      next(reason = "retry") {
        attempt += 1;
        const delay = Math.min(maxMs, baseMs * 2 ** Math.max(0, attempt - 1));
        return {
          attempt,
          delay,
          retryAt: now() + delay,
          reason,
          terminal: attempt >= maxAttempts,
        };
      },
      get attempts() {
        return attempt;
      },
    };
  }

  function createSessionStore(storageArea, { key = "desiredSessions" } = {}) {
    const area = storageArea ?? {
      async get() {
        return {};
      },
      async set() {},
    };

    async function load() {
      try {
        const value = (await area.get(key))?.[key];
        return value && typeof value === "object" ? { ...value } : {};
      } catch {
        return {};
      }
    }

    async function save(sessions) {
      try {
        await area.set({ [key]: sessions });
      } catch (error) {
        console.warn("[Chatterino] Failed to persist session state", error);
      }
    }

    return {
      async all() {
        return load();
      },
      async get(sessionId) {
        const sessions = await load();
        return sessions[sessionId] ?? null;
      },
      async put(session) {
        if (!session?.sessionId) return;
        const sessions = await load();
        sessions[session.sessionId] = {
          sessionId: String(session.sessionId),
          windowId: Number.isInteger(session.windowId)
            ? session.windowId
            : undefined,
          tabId: Number.isInteger(session.tabId) ? session.tabId : undefined,
          channel: typeof session.channel === "string" ? session.channel : "",
          generation: Number.isInteger(session.generation)
            ? session.generation
            : 0,
          desired: session.desired !== false,
          attached: session.attached === true,
          retryAt: Number.isFinite(session.retryAt) ? session.retryAt : 0,
          retryAttempt: Number.isInteger(session.retryAttempt)
            ? session.retryAttempt
            : 0,
          leaseExpiresAt: Number.isFinite(session.leaseExpiresAt)
            ? session.leaseExpiresAt
            : 0,
        };
        await save(sessions);
      },
      async remove(sessionId) {
        const sessions = await load();
        delete sessions[sessionId];
        await save(sessions);
      },
      async clear() {
        await save({});
      },
    };
  }

  function redact(value) {
    if (value === undefined || value === null) return undefined;
    const text = String(value);
    if (text.length <= 12) return text;
    return `${text.slice(0, 6)}…${text.slice(-4)}`;
  }

  function createTransitionRing({
    limit = MAX_RING_ENTRIES,
    clock = Date.now,
  } = {}) {
    const entries = [];
    const now = nowOr(clock);
    function record(component, event, details = {}) {
      const allowed = {};
      for (const key of [
        "state",
        "from",
        "to",
        "reason",
        "generation",
        "attempt",
        "retryAt",
        "protocolVersion",
      ]) {
        if (details[key] !== undefined) {
          if (key === "reason") {
            const candidate = String(details[key]);
            allowed[key] = /^[a-z0-9._-]{1,48}$/i.test(candidate)
              ? candidate
              : "redacted";
          } else {
            allowed[key] = details[key];
          }
        }
      }
      if (details.sessionId !== undefined) {
        allowed.sessionId = redact(details.sessionId);
      }
      const entry = {
        at: now(),
        component: String(component || "unknown").slice(0, 32),
        event: String(event || "transition").slice(0, 48),
        ...allowed,
      };
      entries.push(entry);
      while (entries.length > Math.max(1, Math.min(limit, MAX_RING_ENTRIES))) {
        entries.shift();
      }
      return entry;
    }
    return {
      record,
      snapshot() {
        return entries.map((entry) => ({ ...entry }));
      },
      clear() {
        entries.length = 0;
      },
    };
  }

  function createConnectionState({ clock = Date.now } = {}) {
    let state = "idle";
    let retryAt = 0;
    let retryAttempt = 0;
    const now = nowOr(clock);
    return {
      get state() {
        return state;
      },
      get retryAt() {
        return retryAt;
      },
      get retryAttempt() {
        return retryAttempt;
      },
      transition(next, details = {}) {
        if (!SAFE_STATES.has(next)) {
          throw new TypeError(`unknown connection state: ${next}`);
        }
        const previous = state;
        state = next;
        if (details.retryAt !== undefined) retryAt = details.retryAt;
        if (details.retryAttempt !== undefined) {
          retryAttempt = details.retryAttempt;
        }
        if (next === "connected" || next === "idle") {
          retryAt = 0;
          retryAttempt = 0;
        }
        return {
          from: previous,
          to: state,
          at: now(),
          retryAt,
          retryAttempt,
        };
      },
      snapshot() {
        return { state, retryAt, retryAttempt };
      },
    };
  }

  function leaseDelay(
    leaseExpiresAt,
    { clock = Date.now, minMs = 1000, maxMs = 2147483647 } = {}
  ) {
    const expiresAt = Number(leaseExpiresAt);
    if (!Number.isFinite(expiresAt) || expiresAt <= 0) {
      return null;
    }
    return Math.min(maxMs, Math.max(minMs, expiresAt - clock()));
  }

  global.ChatterinoLifecycle = {
    MAX_RING_ENTRIES,
    SAFE_STATES,
    createBackoffPolicy,
    createConnectionState,
    createSessionStore,
    createTransitionRing,
    leaseDelay,
    redact,
  };
})(globalThis);
