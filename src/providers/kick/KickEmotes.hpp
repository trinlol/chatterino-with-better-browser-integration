// SPDX-FileCopyrightText: 2026 Contributors to Chatterino <https://chatterino.com>
//
// SPDX-License-Identifier: MIT

#pragma once

#include "messages/Emote.hpp"

#include <QRegularExpression>
#include <QString>

#include <memory>
#include <mutex>
#include <unordered_map>

namespace chatterino {

class KickEmotes
{
public:
    KickEmotes();

    EmotePtr getOrCreateEmote(const QString &id, const QString &name);

    static const QRegularExpression &emoteRegex();

private:
    std::mutex mutex_;
    std::unordered_map<QString, EmotePtr> cache_;
};

}  // namespace chatterino
