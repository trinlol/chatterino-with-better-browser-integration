(function (global) {
  "use strict";

  class ChunkReassembler {
    constructor() {
      this.requests = new Map();
    }

    cleanTimeouts(now = Date.now()) {
      for (const [requestId, entry] of this.requests.entries()) {
        if (now - entry.createdAt > 5000) {
          this.requests.delete(requestId);
        }
      }
    }

    processChunk(msg, now = Date.now()) {
      this.cleanTimeouts(now);

      if (!msg || typeof msg !== "object") {
        return null;
      }
      const requestId = String(msg.requestId ?? "");
      if (!requestId) {
        return null;
      }

      const total = Number(msg.total);
      const seq = Number(msg.seq);
      if (
        !Number.isInteger(total) ||
        total <= 0 ||
        !Number.isInteger(seq) ||
        seq < 0 ||
        seq >= total
      ) {
        return null;
      }

      let entry = this.requests.get(requestId);
      if (!entry) {
        entry = {
          createdAt: now,
          total,
          chunks: new Array(total).fill(undefined),
          received: 0,
        };
        this.requests.set(requestId, entry);
      } else if (now - entry.createdAt > 5000) {
        this.requests.delete(requestId);
        return null;
      }

      if (entry.chunks[seq] === undefined) {
        entry.chunks[seq] = typeof msg.data === "string" ? msg.data : "";
        entry.received++;
      }

      if (entry.received === entry.total) {
        const fullString = entry.chunks.join("");
        this.requests.delete(requestId);
        try {
          return JSON.parse(fullString);
        } catch {
          return null;
        }
      }

      return null;
    }
  }

  function mergeLogData(nativeSnapshot = null, localState = {}) {
    const native = nativeSnapshot || {};
    const local = localState || {};

    return {
      timestamp: Date.now(),
      session: {
        connectedSince: local.nativeConnectedSince || null,
        nativeSupportsV2: Boolean(local.nativeSupportsV2),
        lastError: local.lastNativeError || null,
        badgeState: local.badgeState ?? null,
        sessionStore: local.sessionStore || {},
      },
      channel: {
        name: native.channel?.name || "",
        roomId: native.channel?.roomId || "",
        live: Boolean(native.stream?.live),
        title: native.stream?.title || "",
        game: native.stream?.game || "",
        viewerCount: native.stream?.viewerCount ?? 0,
        uptimeSeconds: native.stream?.uptimeSeconds ?? 0,
        streamType: native.stream?.streamType || "",
      },
      chatAndModes: {
        submode: Boolean(native.modes?.submode),
        emoteOnly: Boolean(native.modes?.emoteOnly),
        r9k: Boolean(native.modes?.r9k),
        slowMode: native.modes?.slowMode ?? 0,
        followerOnly: native.modes?.followerOnly ?? -1,
        self: {
          subscribed: Boolean(native.self?.subscribed),
          mod: Boolean(native.self?.mod),
          vip: Boolean(native.self?.vip),
          broadcaster: Boolean(native.self?.broadcaster),
        },
        chatterCount: native.chat?.chatterCount ?? native.chat?.chatters ?? 0,
        messageCount: native.chat?.messageCount ?? 0,
        lastMessage: native.chat?.lastMessage || null,
      },
      events: Array.isArray(local.transitions) ? local.transitions : [],
      raw: {
        native: nativeSnapshot,
        local: localState,
      },
    };
  }

  function escapeHtml(str) {
    if (str === null || str === undefined) return "";
    return String(str)
      .replace(/&/g, "&amp;")
      .replace(/</g, "&lt;")
      .replace(/>/g, "&gt;")
      .replace(/"/g, "&quot;")
      .replace(/'/g, "&#39;");
  }

  function formatUptime(seconds) {
    if (!seconds || seconds <= 0) return "Offline";
    const h = Math.floor(seconds / 3600);
    const m = Math.floor((seconds % 3600) / 60);
    const s = Math.floor(seconds % 60);
    if (h > 0) return `${h}h ${m}m ${s}s`;
    if (m > 0) return `${m}m ${s}s`;
    return `${s}s`;
  }

  function formatTime(timestamp) {
    if (!timestamp) return "N/A";
    const date = new Date(timestamp);
    return isNaN(date.getTime()) ? String(timestamp) : date.toLocaleTimeString();
  }

  function renderLogConsole(panelElement, data, actions = {}) {
    if (!panelElement) return;

    const sessionStoreEntries = Object.entries(data.session?.sessionStore || {});
    const sessionListHtml =
      sessionStoreEntries.length === 0
        ? '<div class="chatterino-log-empty">No active sessions stored</div>'
        : sessionStoreEntries
            .map(
              ([key, val]) => `
          <div class="chatterino-log-session-item">
            <span class="chatterino-log-code">${escapeHtml(key)}</span>:
            ${val?.channel ? `<span class="chatterino-log-val">channel=${escapeHtml(val.channel)}</span>` : ""}
            ${val?.windowId !== undefined ? `<span class="chatterino-log-val">win=${escapeHtml(val.windowId)}</span>` : ""}
            ${val?.tabId !== undefined ? `<span class="chatterino-log-val">tab=${escapeHtml(val.tabId)}</span>` : ""}
            ${val?.attached ? '<span class="chatterino-pill-badge active">attached</span>' : '<span class="chatterino-pill-badge inactive">detached</span>'}
          </div>`
            )
            .join("");

    const selfRoles = data.chatAndModes?.self || {};
    const selfBadgesHtml = `
      <div class="chatterino-log-badges">
        <span class="chatterino-pill-badge ${selfRoles.subscribed ? "active" : "inactive"}">Sub</span>
        <span class="chatterino-pill-badge ${selfRoles.mod ? "active" : "inactive"}">Mod</span>
        <span class="chatterino-pill-badge ${selfRoles.vip ? "active" : "inactive"}">VIP</span>
        <span class="chatterino-pill-badge ${selfRoles.broadcaster ? "active" : "inactive"}">Broadcaster</span>
      </div>
    `;

    const recentEvents = data.events || [];
    const eventsHtml =
      recentEvents.length === 0
        ? '<div class="chatterino-log-empty">No transitions recorded</div>'
        : recentEvents
            .slice(-10)
            .reverse()
            .map(
              (ev) => `
          <div class="chatterino-log-event-row">
            <span class="chatterino-log-event-time">${escapeHtml(formatTime(ev.timestamp || ev.time))}</span>
            <span class="chatterino-log-event-name">${escapeHtml(ev.event || ev.name || "event")}</span>
            <span class="chatterino-log-event-details">${escapeHtml(JSON.stringify(ev.details || ev.data || {}))}</span>
          </div>`
            )
            .join("");

    const lastMsg = data.chatAndModes?.lastMessage;
    const lastMsgHtml = lastMsg
      ? `<span class="chatterino-log-code">${escapeHtml(lastMsg.user || "")}</span>: ${escapeHtml(lastMsg.text || "")}`
      : '<span class="chatterino-log-dim">None</span>';

    panelElement.innerHTML = `
      <div class="chatterino-log-header">
        <div class="chatterino-log-title">
          <span class="chatterino-log-term-prompt">›_</span> Chatterino Integration Logs
        </div>
        <button type="button" class="chatterino-log-close-btn" title="Close console">×</button>
      </div>

      <div class="chatterino-log-body">
        <!-- Section 1: Session (Blue #3b82f6) -->
        <section class="chatterino-log-section section-session">
          <h3 class="chatterino-section-title title-session">Session</h3>
          <div class="chatterino-log-grid">
            <div class="chatterino-log-row">
              <span class="chatterino-log-label">Connected Since:</span>
              <span class="chatterino-log-value">${escapeHtml(data.session?.connectedSince ? formatTime(data.session.connectedSince) : "Disconnected")}</span>
            </div>
            <div class="chatterino-log-row">
              <span class="chatterino-log-label">Native Supports v2:</span>
              <span class="chatterino-log-value">${data.session?.nativeSupportsV2 ? "Yes" : "No"}</span>
            </div>
            <div class="chatterino-log-row">
              <span class="chatterino-log-label">Last Error:</span>
              <span class="chatterino-log-value ${data.session?.lastError ? "chatterino-log-err" : ""}">${escapeHtml(data.session?.lastError || "None")}</span>
            </div>
            <div class="chatterino-log-row">
              <span class="chatterino-log-label">Badge State:</span>
              <span class="chatterino-log-value">${escapeHtml(data.session?.badgeState || "normal")}</span>
            </div>
          </div>
          <div class="chatterino-log-subhead">Session Store:</div>
          <div class="chatterino-log-session-list">
            ${sessionListHtml}
          </div>
        </section>

        <!-- Section 2: Channel (Purple #a855f7) -->
        <section class="chatterino-log-section section-channel">
          <h3 class="chatterino-section-title title-channel">Channel</h3>
          <div class="chatterino-log-grid">
            <div class="chatterino-log-row">
              <span class="chatterino-log-label">Name / Room ID:</span>
              <span class="chatterino-log-value">${escapeHtml(data.channel?.name || "N/A")} ${data.channel?.roomId ? `(${escapeHtml(data.channel.roomId)})` : ""}</span>
            </div>
            <div class="chatterino-log-row">
              <span class="chatterino-log-label">Stream Status:</span>
              <span class="chatterino-log-value">
                ${data.channel?.live ? '<span class="chatterino-status-pill live">LIVE</span>' : '<span class="chatterino-status-pill offline">OFFLINE</span>'}
              </span>
            </div>
            <div class="chatterino-log-row">
              <span class="chatterino-log-label">Title:</span>
              <span class="chatterino-log-value">${escapeHtml(data.channel?.title || "N/A")}</span>
            </div>
            <div class="chatterino-log-row">
              <span class="chatterino-log-label">Game:</span>
              <span class="chatterino-log-value">${escapeHtml(data.channel?.game || "N/A")}</span>
            </div>
            <div class="chatterino-log-row">
              <span class="chatterino-log-label">Viewers / Uptime:</span>
              <span class="chatterino-log-value">${data.channel?.viewerCount != null ? Number(data.channel.viewerCount).toLocaleString() : 0} viewers • ${escapeHtml(formatUptime(data.channel?.uptimeSeconds))}</span>
            </div>
          </div>
        </section>

        <!-- Section 3: Chat & Modes (Green #22c55e) -->
        <section class="chatterino-log-section section-chat">
          <h3 class="chatterino-section-title title-chat">Chat & Modes</h3>
          <div class="chatterino-log-grid">
            <div class="chatterino-log-row">
              <span class="chatterino-log-label">Room Modes:</span>
              <span class="chatterino-log-value">
                sub=${data.chatAndModes?.submode ? "on" : "off"},
                emote=${data.chatAndModes?.emoteOnly ? "on" : "off"},
                r9k=${data.chatAndModes?.r9k ? "on" : "off"},
                slow=${data.chatAndModes?.slowMode ? `${data.chatAndModes.slowMode}s` : "off"},
                follower=${data.chatAndModes?.followerOnly >= 0 ? `${data.chatAndModes.followerOnly}m` : "off"}
              </span>
            </div>
            <div class="chatterino-log-row">
              <span class="chatterino-log-label">Self Badges:</span>
              <span class="chatterino-log-value">${selfBadgesHtml}</span>
            </div>
            <div class="chatterino-log-row">
              <span class="chatterino-log-label">Chatters / Messages:</span>
              <span class="chatterino-log-value">${Number(data.chatAndModes?.chatterCount || 0).toLocaleString()} chatters • ${Number(data.chatAndModes?.messageCount || 0).toLocaleString()} messages</span>
            </div>
            <div class="chatterino-log-row">
              <span class="chatterino-log-label">Last Message:</span>
              <span class="chatterino-log-value chatterino-log-truncate">${lastMsgHtml}</span>
            </div>
          </div>
        </section>

        <!-- Section 4: Recent Events (Amber #f59e0b) -->
        <section class="chatterino-log-section section-events">
          <h3 class="chatterino-section-title title-events">Recent Events</h3>
          <div class="chatterino-log-events-list">
            ${eventsHtml}
          </div>
        </section>

        <!-- Section 5: Raw JSON (Gray #9ca3af) -->
        <section class="chatterino-log-section section-raw">
          <div class="chatterino-raw-header">
            <h3 class="chatterino-section-title title-raw">Raw JSON</h3>
            <button type="button" class="chatterino-log-copy-btn">Copy JSON</button>
          </div>
          <pre class="chatterino-log-json">${escapeHtml(JSON.stringify(data.raw || data, null, 2))}</pre>
        </section>
      </div>
    `;

    const closeBtn = panelElement.querySelector(".chatterino-log-close-btn");
    if (closeBtn) {
      closeBtn.addEventListener("click", (e) => {
        e.stopPropagation();
        actions?.onClose?.();
      });
    }

    const copyBtn = panelElement.querySelector(".chatterino-log-copy-btn");
    if (copyBtn) {
      copyBtn.addEventListener("click", (e) => {
        e.stopPropagation();
        const jsonStr = JSON.stringify(data.raw || data, null, 2);
        if (typeof actions?.onCopy === "function") {
          actions.onCopy(jsonStr);
        } else if (navigator?.clipboard?.writeText) {
          navigator.clipboard.writeText(jsonStr).catch(() => {});
        }
        copyBtn.textContent = "Copied!";
        setTimeout(() => {
          copyBtn.textContent = "Copy JSON";
        }, 2000);
      });
    }
  }

  const ChatterinoLogConsole = {
    ChunkReassembler,
    mergeLogData,
    renderLogConsole,
  };

  if (typeof window !== "undefined") {
    window.ChatterinoLogConsole = ChatterinoLogConsole;
  }
  if (typeof globalThis !== "undefined") {
    globalThis.ChatterinoLogConsole = ChatterinoLogConsole;
  }
  if (typeof module !== "undefined" && module.exports) {
    module.exports = ChatterinoLogConsole;
  }
})(typeof window !== "undefined" ? window : globalThis);
