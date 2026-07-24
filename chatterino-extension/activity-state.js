(function (global) {
  'use strict';

  const ACTIVITY_KINDS = Object.freeze(['poll', 'prediction']);

  function normalizeOptions(options) {
    if (!Array.isArray(options)) return [];
    return options
      .map((option) => {
        if (typeof option === 'string') return option.trim();
        return String(option?.title || option?.name || option?.label || '').trim();
      })
      .filter(Boolean);
  }

  function normalizeActivity(kind, input, source = 'unknown') {
    if (!ACTIVITY_KINDS.includes(kind) || !input) return null;
    const title = String(input.title || '').trim();
    if (!title) return null;
    const duration = Number(input.durationSeconds ?? input.duration ?? 0);
    return {
      kind,
      title,
      options: normalizeOptions(input.options),
      status: String(input.status || 'started').trim().toLowerCase(),
      durationSeconds: Number.isFinite(duration) && duration > 0 ? Math.round(duration) : 0,
      winner: String(input.winner || '').trim(),
      source,
    };
  }

  function mergeActivities(kind, domActivity, graphqlActivity) {
    const dom = normalizeActivity(kind, domActivity, 'dom');
    const graphql = normalizeActivity(kind, graphqlActivity, 'graphql');
    if (!dom) return graphql;
    if (!graphql) return dom;
    return {
      kind,
      title: graphql.title || dom.title,
      options: graphql.options.length ? graphql.options : dom.options,
      status: graphql.status || dom.status,
      durationSeconds: graphql.durationSeconds || dom.durationSeconds,
      winner: graphql.winner || dom.winner,
      source: 'dom+graphql',
    };
  }

  function publicationFingerprint(activity) {
    if (!activity) return 'removed';
    return JSON.stringify({
      kind: activity.kind,
      title: activity.title,
      options: activity.options,
      status: activity.status,
      winner: activity.winner,
      timed: activity.durationSeconds > 0,
    });
  }

  class ActivityStore {
    constructor() {
      this.channel = '';
      this.dom = { poll: null, prediction: null };
      this.graphql = { poll: null, prediction: null };
      this.published = { poll: 'removed', prediction: 'removed' };
    }

    setChannel(channel) {
      const normalized = String(channel || '').trim().toLowerCase();
      if (normalized === this.channel) return false;
      this.channel = normalized;
      this.dom = { poll: null, prediction: null };
      this.graphql = { poll: null, prediction: null };
      this.resetPublications();
      return true;
    }

    applyGraphql(snapshot) {
      for (const kind of ACTIVITY_KINDS) {
        this.graphql[kind] = normalizeActivity(kind, snapshot?.[kind], 'graphql');
      }
    }

    observeDom(kind, details) {
      if (!ACTIVITY_KINDS.includes(kind)) return null;
      this.dom[kind] = normalizeActivity(kind, details, 'dom');
      return this.current(kind);
    }

    removeDom(kind) {
      if (ACTIVITY_KINDS.includes(kind)) this.dom[kind] = null;
      return this.current(kind);
    }

    current(kind) {
      if (!ACTIVITY_KINDS.includes(kind)) return null;
      return mergeActivities(kind, this.dom[kind], this.graphql[kind]);
    }

    nextPublication(kind) {
      const activity = this.current(kind);
      const fingerprint = publicationFingerprint(activity);
      if (fingerprint === this.published[kind]) return null;
      this.published[kind] = fingerprint;
      return activity
        ? { lifecycle: 'upsert', activity }
        : { lifecycle: 'remove', activity: null };
    }

    resetPublications() {
      this.published = {
        poll: this.current('poll') ? '' : 'removed',
        prediction: this.current('prediction') ? '' : 'removed',
      };
    }

    snapshot() {
      return {
        channel: this.channel,
        poll: this.current('poll'),
        prediction: this.current('prediction'),
      };
    }
  }

  global.ChatterinoActivity = {
    ACTIVITY_KINDS,
    ActivityStore,
    mergeActivities,
    normalizeActivity,
    publicationFingerprint,
  };
})(window);
