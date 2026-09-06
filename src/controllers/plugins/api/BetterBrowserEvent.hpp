// SPDX-FileCopyrightText: 2026 Contributors to Chatterino <https://chatterino.com>
//
// SPDX-License-Identifier: MIT

#pragma once

#ifdef CHATTERINO_HAVE_PLUGINS

#    include <QString>
#    include <QtGlobal>

namespace chatterino {

/// A normalized, read-only Better Browser transition or activity projection.
/// `sessionId` is accepted from the integration boundary but is shortened
/// before it enters a Lua table.
struct BetterBrowserEvent {
    QString event;
    QString sessionId;
    qint64 generation{};
    QString channel;
    QString source;
    QString status;
    QString reason;
    QString activityKind;
    QString activityTitle;
    QString activityStatus;
};

}  // namespace chatterino

namespace chatterino::lua::api {

/**
 * @lua@class BetterBrowserEvent
 */
struct BetterBrowserEventDocumentation {
    /**
     * @lua@field schema_version integer The fixed schema version. It is `1` for this event shape.
     */
    int schemaVersion{};
    /**
     * @lua@field event string A normalized lifecycle or activity event name.
     */
    QString event;
    /**
     * @lua@field session_id string A shortened opaque session identifier, never the full browser session ID.
     */
    QString sessionId;
    /**
     * @lua@field generation integer The monotonic session generation.
     */
    qint64 generation{};
    /**
     * @lua@field channel string The normalized Twitch channel login, when known.
     */
    QString channel;
    /**
     * @lua@field source string The normalized data source, such as `dom` or `private-graphql`.
     */
    QString source;
    /**
     * @lua@field status string The normalized lifecycle or activity status.
     */
    QString status;
    /**
     * @lua@field reason string A bounded, non-sensitive transition reason.
     */
    QString reason;
    /**
     * @lua@field activity_kind? string The activity kind, when this is an activity event.
     */
    QString activityKind;
    /**
     * @lua@field activity_title? string The activity title, when this is an activity event.
     */
    QString activityTitle;
    /**
     * @lua@field activity_status? string The activity status, when this is an activity event.
     */
    QString activityStatus;
};

}  // namespace chatterino::lua::api

#endif
