// Ported from Moltorino (https://codeberg.org/MoltoBenne/Moltorino)
// Copyright (c) MoltoBenne - MIT License
// Adapted for Chatterino Better Browser: the default /translate target
// language uses this fork's settings namespace.
#include "controllers/commands/builtin/twitch/Translate.hpp"

#include "Application.hpp"
#include "common/Channel.hpp"
#include "controllers/accounts/AccountController.hpp"
#include "controllers/commands/CommandContext.hpp"
#include "messages/Message.hpp"
#include "messages/MessageBuilder.hpp"
#include "messages/MessageElement.hpp"
#include "providers/twitch/TwitchAccount.hpp"
#include "providers/twitch/TwitchChannel.hpp"
#include "providers/twitch/TwitchCommon.hpp"
#include "providers/twitch/TwitchNameHistory.hpp"
#include "providers/twitch/api/TwitchGql.hpp"
#include "providers/translation/Translator.hpp"
#include "singletons/Settings.hpp"
#include "util/PostToThread.hpp"
#include "util/Twitch.hpp"

#include <QString>
#include <QUrl>
#include <QUrlQuery>

namespace chatterino::commands {
namespace {

QString commandWordsAfter(const CommandContext &ctx, int wordCount)
{
    return ctx.words.mid(wordCount).join(QLatin1Char(' ')).trimmed();
}

QString supportedTranslationLanguageText()
{
    return QStringLiteral(
        "examples: en, es, pt, fr, de, ja, ko, zh-cn, zh-tw, ar");
}

void addTranslationSystemMessage(const ChannelPtr &channel,
                                 const TranslationResult &result,
                                 const QString &targetLanguage)
{
    if (channel == nullptr)
    {
        return;
    }

    const auto targetName = translationLanguageName(targetLanguage);
    const auto detectedLanguage =
        normalizedLanguageCode(result.detectedLanguage);
    const auto detectedName = translationLanguageName(detectedLanguage);

    QString prefix = QStringLiteral("Translation");
    if (!detectedName.isEmpty() && detectedLanguage != targetLanguage)
    {
        prefix += QStringLiteral(" (%1 -> %2)").arg(detectedName, targetName);
    }
    else if (!targetName.isEmpty())
    {
        prefix += QStringLiteral(" (%1)").arg(targetName);
    }

    channel->addSystemMessage(QStringLiteral("%1: %2")
                                  .arg(prefix,
                                       result.translatedText.trimmed()));
}

QString runTranslatePreviewCommand(const CommandContext &ctx,
                                   const QString &targetLanguage,
                                   const QString &message,
                                   const QString &usage)
{
    if (ctx.channel == nullptr)
    {
        return "";
    }

    if (message.isEmpty())
    {
        ctx.channel->addSystemMessage(usage);
        return "";
    }

    requestTextTranslation(
        message, targetLanguage, nullptr,
        [channel = ctx.channel, targetLanguage](
            const TranslationResult &result) {
            runInGuiThread([channel, targetLanguage, result] {
                addTranslationSystemMessage(channel, result, targetLanguage);
            });
        },
        [channel = ctx.channel](const QString &) {
            runInGuiThread([channel] {
                if (channel != nullptr)
                {
                    channel->addSystemMessage(
                        QStringLiteral("Translation failed. Try again later."));
                }
            });
        });

    return "";
}

QString runTranslateSendCommand(const CommandContext &ctx,
                                const QString &targetLanguage,
                                const QString &message)
{
    if (ctx.channel == nullptr)
    {
        return "";
    }

    if (message.isEmpty())
    {
        ctx.channel->addSystemMessage("Usage: /tl <language> <message>");
        return "";
    }

    requestTextTranslation(
        message, targetLanguage, nullptr,
        [channel = ctx.channel](const TranslationResult &result) {
            runInGuiThread([channel, result] {
                if (channel == nullptr)
                {
                    return;
                }
                auto translatedText = result.translatedText.trimmed();
                translatedText.replace('\n', ' ');
                if (translatedText.isEmpty())
                {
                    channel->addSystemMessage(
                        QStringLiteral(
                            "Translation failed, so nothing was sent."));
                    return;
                }
                if (translatedText.size() > TWITCH_MESSAGE_LIMIT)
                {
                    channel->addSystemMessage(QStringLiteral(
                        "The translated message is too long for Twitch."));
                    return;
                }

                channel->sendMessage(translatedText);
            });
        },
        [channel = ctx.channel](const QString &) {
            runInGuiThread([channel] {
                if (channel != nullptr)
                {
                    channel->addSystemMessage(
                        QStringLiteral(
                            "Translation failed, so nothing was sent."));
                }
            });
        });

    return "";
}

QString formatNameHistoryRow(const TwitchNameHistoryEntry &entry)
{
    return QStringLiteral("%1: %2 - %3")
        .arg(entry.login, entry.leftText, entry.rightText);
}

void addNameHistorySystemMessage(const ChannelPtr &channel,
                                 const TwitchNameHistory &history)
{
    if (history.entries.empty())
    {
        channel->addSystemMessage("No name history found.");
        return;
    }

    MessageBuilder builder;
    QString searchText;

    for (auto it = history.entries.cbegin(); it != history.entries.cend(); ++it)
    {
        const auto row = formatNameHistoryRow(*it);
        if (!searchText.isEmpty())
        {
            searchText += '\n';
            builder.emplace<LinebreakElement>(MessageElementFlag::Text);
        }
        searchText += row;

        builder.emplace<TextElement>(it->login + ':', MessageElementFlag::Text,
                                     MessageColor::System,
                                     FontStyle::ChatMediumBold);
        builder.emplace<TextElement>(" " + it->leftText + " - " + it->rightText,
                                     MessageElementFlag::Text,
                                     MessageColor::System);
    }

    builder->flags.set(MessageFlag::System);
    builder->flags.set(MessageFlag::DoNotTriggerNotification);
    builder->messageText = searchText;
    builder->searchText = searchText;

    channel->addMessage(builder.release(), MessageContext::Original);
}

void runNameHistoryLookup(const ChannelPtr &channel, const QString &userId,
                          const QString &targetName,
                          const QString &expectedLogin, bool announceFetch)
{
    if (channel == nullptr)
    {
        return;
    }

    if (const auto cached =
            getCachedTwitchNameHistory(userId, expectedLogin))
    {
        addNameHistorySystemMessage(channel, *cached);
        return;
    }

    if (announceFetch)
    {
        channel->addSystemMessage("Fetching name history...");
    }

    fetchTwitchNameHistoryByUserId(
        userId, expectedLogin,
        [channel](TwitchNameHistory history) {
            runInGuiThread([channel, history{std::move(history)}]() mutable {
                if (channel != nullptr)
                {
                    addNameHistorySystemMessage(channel, history);
                }
            });
        },
        [channel, targetName](const QString &error) {
            runInGuiThread([channel, targetName, error] {
                if (channel != nullptr)
                {
                    channel->addSystemMessage(
                        QString("Failed to fetch name history for %1: %2")
                            .arg(targetName, error));
                }
            });
        });
}

}  // namespace

QString translate(const CommandContext &ctx)
{
    const auto targetLanguage = normalizedTranslationTargetLanguage(
        getSettings()->messageTranslationTargetLanguage.getValue());
    return runTranslatePreviewCommand(
        ctx, targetLanguage, commandWordsAfter(ctx, 1),
        QStringLiteral("Usage: /translate <message>"));
}

QString translateTo(const CommandContext &ctx)
{
    if (ctx.channel == nullptr)
    {
        return "";
    }

    const auto targetLanguage =
        translationLanguageCodeFromInput(ctx.words.value(1));
    if (targetLanguage.isEmpty())
    {
        ctx.channel->addSystemMessage(
            QStringLiteral("Usage: /translateto <language> <message> (%1)")
                .arg(supportedTranslationLanguageText()));
        return "";
    }

    return runTranslatePreviewCommand(
        ctx, targetLanguage, commandWordsAfter(ctx, 2),
        QStringLiteral("Usage: /translateto <language> <message>"));
}

QString sayTranslate(const CommandContext &ctx)
{
    if (ctx.channel == nullptr)
    {
        return "";
    }

    const auto targetLanguage =
        translationLanguageCodeFromInput(ctx.words.value(1));
    if (targetLanguage.isEmpty())
    {
        ctx.channel->addSystemMessage(
            QStringLiteral("Usage: /tl <language> <message> (%1)")
                .arg(supportedTranslationLanguageText()));
        return "";
    }

    return runTranslateSendCommand(ctx, targetLanguage,
                                   commandWordsAfter(ctx, 2));
}

QString nameHistory(const CommandContext &ctx)
{
    if (ctx.channel == nullptr)
    {
        return "";
    }

    auto target = ctx.words.value(1).trimmed();
    if (target.isEmpty())
    {
        ctx.channel->addSystemMessage("Usage: /namehistory <username>");
        return "";
    }

    auto [targetLogin, targetId] = parseUserNameOrID(target);
    targetLogin = targetLogin.trimmed().toLower();
    targetId = targetId.trimmed();

    if (!targetId.isEmpty())
    {
        runNameHistoryLookup(ctx.channel, targetId,
                             QString("id:%1").arg(targetId), QString(), true);
        return "";
    }

    if (targetLogin.isEmpty() ||
        !twitchUserLoginRegexp().match(targetLogin).hasMatch())
    {
        ctx.channel->addSystemMessage("Usage: /namehistory <username>");
        return "";
    }

    if (const auto cached =
            getCachedTwitchNameHistory(QString(), targetLogin))
    {
        addNameHistorySystemMessage(ctx.channel, *cached);
        return "";
    }

    ctx.channel->addSystemMessage("Fetching name history...");

    auto current = getApp()->getAccounts()->twitch.getCurrent();
    const auto token =
        (current && !current->isAnon()) ? current->getOAuthToken() : QString();

    TwitchGql::getUserByLogin(
        targetLogin, token,
        [channel = ctx.channel, targetLogin](std::optional<GqlUser> user) {
            runInGuiThread([channel, targetLogin, user{std::move(user)}](
                               ) mutable {
                if (!user)
                {
                    if (channel != nullptr)
                    {
                        channel->addSystemMessage(QString(
                            "Could not find Twitch user %1.")
                                                      .arg(targetLogin));
                    }
                    return;
                }

                const auto targetName = user->displayName.isEmpty()
                                            ? user->login
                                            : user->displayName;
                runNameHistoryLookup(channel, user->id, targetName, user->login,
                                     false);
            });
        },
        [channel = ctx.channel, targetLogin](const QString &error) {
            runInGuiThread([channel, targetLogin, error] {
                if (channel != nullptr)
                {
                    channel->addSystemMessage(
                        QString("Failed to look up %1: %2")
                            .arg(targetLogin, error));
                }
            });
        });

    return "";
}

QString logs(const CommandContext &ctx)
{
    if (ctx.channel == nullptr)
    {
        return "";
    }

    if (ctx.words.size() < 2)
    {
        ctx.channel->addSystemMessage("Usage: /logs <user> [channel]");
        return "";
    }

    QString userName = ctx.words[1];
    stripUserName(userName);
    userName = userName.trimmed();

    QString channelName;
    if (ctx.words.size() > 2)
    {
        channelName = ctx.words[2];
        stripChannelName(channelName);
        channelName = channelName.trimmed();
    }
    else if (ctx.twitchChannel != nullptr)
    {
        channelName = ctx.twitchChannel->getName();
    }

    if (userName.isEmpty() || channelName.isEmpty())
    {
        ctx.channel->addSystemMessage("Usage: /logs <user> [channel]");
        return "";
    }

    QUrl url(QStringLiteral("https://tv.supa.sh/logs"));
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("c"), channelName);
    query.addQueryItem(QStringLiteral("u"), userName);
    url.setQuery(query);

    const auto link = url.toString();
    ctx.channel->addSystemMessage(
        QStringLiteral("Logs from %1 in %2: %3")
            .arg(userName, channelName, link));

    return "";
}

}  // namespace chatterino::commands
