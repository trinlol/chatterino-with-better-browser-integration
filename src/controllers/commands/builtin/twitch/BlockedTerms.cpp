// Ported from Moltorino (https://codeberg.org/MoltoBenne/Moltorino)
// Copyright (c) MoltoBenne - MIT License
// Adapted for Chatterino Better Browser: authenticates with the logged-in
// Chatterino account instead of Moltorino's saved-account store.
#include "controllers/commands/builtin/twitch/BlockedTerms.hpp"

#include "Application.hpp"
#include "common/Channel.hpp"
#include "controllers/accounts/AccountController.hpp"
#include "controllers/commands/CommandContext.hpp"
#include "providers/twitch/TwitchAccount.hpp"
#include "providers/twitch/TwitchChannel.hpp"
#include "providers/twitch/api/TwitchGql.hpp"
#include "util/PostToThread.hpp"

#include <algorithm>
#include <optional>

namespace {

using namespace chatterino;

QString usage(const QString &command)
{
    return QStringLiteral("Usage: %1 <phrase>").arg(command);
}

QString phraseFromCommand(const CommandContext &ctx, const QString &)
{
    QString phrase;
    if (ctx.words.size() > 1)
    {
        phrase = ctx.words.mid(1).join(QLatin1Char(' ')).trimmed();
    }

    while (phrase.size() >= 2 &&
           ((phrase.startsWith(QLatin1Char('"')) &&
             phrase.endsWith(QLatin1Char('"'))) ||
            (phrase.startsWith(QLatin1Char('\'')) &&
             phrase.endsWith(QLatin1Char('\'')))))
    {
        phrase = phrase.mid(1, phrase.size() - 2).trimmed();
    }

    return phrase;
}

QString currentAccountToken()
{
    auto current = getApp()->getAccounts()->twitch.getCurrent();
    if (current && !current->isAnon() &&
        !current->getOAuthToken().trimmed().isEmpty())
    {
        return current->getOAuthToken();
    }
    return {};
}

std::optional<QString> moderationTokenOrWarn(const CommandContext &ctx)
{
    auto token = currentAccountToken();
    if (token.isEmpty())
    {
        if (ctx.channel != nullptr)
        {
            ctx.channel->addSystemMessage(
                "You need to be logged in to a Twitch account to manage "
                "blocked terms. Log in and try again.");
        }
        return std::nullopt;
    }

    return token;
}

QString normalizeAuthError(const QString &error)
{
    const auto lowered = error.toLower();
    const bool looksLikeAuthError =
        lowered.contains("unauthenticated") ||
        lowered.contains("unauthorized") ||
        lowered.contains("authorization") || lowered.contains("forbidden") ||
        lowered.contains("401") || lowered.contains("403");
    if (looksLikeAuthError)
    {
        return QStringLiteral(
            "your login may have expired or is missing the required "
            "permission - try logging out and back in");
    }
    return error;
}

std::optional<GqlBlockedTerm> findBlockedTerm(
    const QVector<GqlBlockedTerm> &terms, const QString &phrase)
{
    const auto needle = phrase.trimmed();
    auto found = std::find_if(terms.begin(), terms.end(), [&](const auto &term) {
        return term.phrase.trimmed() == needle;
    });
    if (found != terms.end())
    {
        return *found;
    }

    found = std::find_if(terms.begin(), terms.end(), [&](const auto &term) {
        return term.phrase.trimmed().compare(needle, Qt::CaseInsensitive) == 0;
    });
    if (found != terms.end())
    {
        return *found;
    }

    return std::nullopt;
}

}  // namespace

namespace chatterino::commands {

QString blockTerm(const CommandContext &ctx)
{
    if (ctx.channel == nullptr)
    {
        return "";
    }

    if (ctx.twitchChannel == nullptr)
    {
        ctx.channel->addSystemMessage(
            "The /blockterm command only works in Twitch channels.");
        return "";
    }

    const auto phrase = phraseFromCommand(ctx, QStringLiteral("/blockterm"));
    if (phrase.isEmpty())
    {
        ctx.channel->addSystemMessage(usage(QStringLiteral("/blockterm")));
        return "";
    }

    const auto token = moderationTokenOrWarn(ctx);
    if (!token)
    {
        return "";
    }

    TwitchGql::addChannelBlockedTerm(
        ctx.twitchChannel->roomId(), phrase, *token,
        [channel{ctx.channel}, phrase]() {
            runInGuiThread([channel, phrase]() mutable {
                if (channel != nullptr)
                {
                    channel->addSystemMessage(
                        QStringLiteral("Added blocked term: \"%1\"")
                            .arg(phrase));
                }
            });
        },
        [channel{ctx.channel}](const QString &error) mutable {
            runInGuiThread([channel, error]() mutable {
                if (channel != nullptr)
                {
                    channel->addSystemMessage(
                        "Failed to add blocked term: " +
                        normalizeAuthError(error));
                }
            });
        });

    return "";
}

QString unblockTerm(const CommandContext &ctx)
{
    if (ctx.channel == nullptr)
    {
        return "";
    }

    if (ctx.twitchChannel == nullptr)
    {
        ctx.channel->addSystemMessage(
            "The /unblockterm command only works in Twitch channels.");
        return "";
    }

    const auto phrase = phraseFromCommand(ctx, QStringLiteral("/unblockterm"));
    if (phrase.isEmpty())
    {
        ctx.channel->addSystemMessage(usage(QStringLiteral("/unblockterm")));
        return "";
    }

    const auto token = moderationTokenOrWarn(ctx);
    if (!token)
    {
        return "";
    }

    const auto channelId = ctx.twitchChannel->roomId();
    TwitchGql::getChannelBlockedTerms(
        channelId, *token,
        [channel{ctx.channel}, channelId, phrase,
         token = *token](QVector<GqlBlockedTerm> terms) mutable {
            const auto term = findBlockedTerm(terms, phrase);
            if (!term)
            {
                runInGuiThread([channel, phrase]() mutable {
                    if (channel != nullptr)
                    {
                        channel->addSystemMessage(QStringLiteral(
                            "Could not find a blocked term matching \"%1\"")
                            .arg(phrase));
                    }
                });
                return;
            }

            TwitchGql::deleteChannelBlockedTerm(
                channelId, term->id, token,
                [channel, phrase]() mutable {
                    runInGuiThread([channel, phrase]() mutable {
                        if (channel != nullptr)
                        {
                            channel->addSystemMessage(
                                QStringLiteral(
                                    "Removed blocked term: \"%1\"")
                                    .arg(phrase));
                        }
                    });
                },
                [channel](const QString &error) mutable {
                    runInGuiThread([channel, error]() mutable {
                        if (channel != nullptr)
                        {
                            channel->addSystemMessage(
                                "Failed to remove blocked term: " +
                                normalizeAuthError(error));
                        }
                    });
                });
        },
        [channel{ctx.channel}](const QString &error) mutable {
            runInGuiThread([channel, error]() mutable {
                if (channel != nullptr)
                {
                    channel->addSystemMessage(
                        "Failed to fetch blocked terms: " +
                        normalizeAuthError(error));
                }
            });
        });

    return "";
}

}  // namespace chatterino::commands
