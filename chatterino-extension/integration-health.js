(function (global) {
  'use strict';

  function formatAge(timestamp, now = Date.now()) {
    if (!timestamp) return 'never';
    const seconds = Math.max(0, Math.round((now - timestamp) / 1000));
    if (seconds < 5) return 'now';
    if (seconds < 60) return `${seconds}s ago`;
    return `${Math.round(seconds / 60)}m ago`;
  }

  function summarize(health, now = Date.now()) {
    const native = health?.native ?? {};
    const tab = health?.tab ?? null;
    const content = health?.content ?? null;
    let level = 'ok';
    let headline = 'Connected';
    if (native.blocked) {
      level = 'error';
      headline = 'Native host blocked';
    } else if (!native.connected) {
      level = 'warning';
      headline = 'Native host disconnected';
    } else if (!tab) {
      level = 'warning';
      headline = 'No active Twitch channel';
    } else if (!content) {
      level = 'warning';
      headline = 'Companion not responding';
    }

    const lines = [
      `Native: ${native.connected ? 'connected' : native.blocked ? 'blocked' : 'disconnected'}`,
      `Channel: ${content?.channel || tab?.channel || 'none'}`,
      `Chat: ${content?.companionActive ? 'attached' : 'not attached'}`,
      `Poll: ${content?.activities?.poll?.title || 'none'}`,
      `Prediction: ${content?.activities?.prediction?.title || 'none'}`,
      `GraphQL: ${formatAge(content?.graphqlUpdatedAt, now)}`,
    ];
    if (native.lastError) lines.push(`Error: ${native.lastError}`);
    return { level, headline, lines };
  }

  global.ChatterinoIntegrationHealth = { formatAge, summarize };
})(globalThis);
