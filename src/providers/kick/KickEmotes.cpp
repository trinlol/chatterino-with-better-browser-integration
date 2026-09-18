// SPDX-FileCopyrightText: 2026 Contributors to Chatterino <https://chatterino.com>
//
// SPDX-License-Identifier: MIT

#include "providers/kick/KickEmotes.hpp"

#include "messages/Image.hpp"

namespace chatterino {

KickEmotes::KickEmotes() = default;

EmotePtr KickEmotes::getOrCreateEmote(const QString &id, const QString &name)
{
    std::lock_guard<std::mutex> lock(this->mutex_);
    auto it = this->cache_.find(id);
    if (it != this->cache_.end())
    {
        return it->second;
    }

    QString urlString = QStringLiteral("https://files.kick.com/emotes/%1/fullsize").arg(id);
    auto img = Image::fromUrl(Url{urlString}, 1, QSize(28, 28));

    auto emote = std::make_shared<Emote>(Emote{
        .name = EmoteName{name},
        .images = ImageSet{img},
        .tooltip = Tooltip{name},
        .homePage = Url{QStringLiteral("https://kick.com")},
        .zeroWidth = false,
        .id = EmoteId{id},
        .author = EmoteAuthor{},
    });

    this->cache_.emplace(id, emote);
    return emote;
}

const QRegularExpression &KickEmotes::emoteRegex()
{
    static const QRegularExpression regex(QStringLiteral(R"(\[emote:([0-9]+):([^\]]+)\])"));
    return regex;
}

}  // namespace chatterino
