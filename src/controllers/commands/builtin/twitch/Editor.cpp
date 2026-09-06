// Ported from Moltorino (https://codeberg.org/MoltoBenne/Moltorino)
// Copyright (c) MoltoBenne - MIT License
// Adapted for Chatterino Better Browser: authenticates with the logged-in
// Chatterino account instead of Moltorino's saved-account store.
#include "controllers/commands/builtin/twitch/Editor.hpp"

#include "Application.hpp"
#include "common/Channel.hpp"
#include "controllers/accounts/AccountController.hpp"
#include "controllers/commands/CommandContext.hpp"
#include "providers/twitch/TwitchAccount.hpp"
#include "providers/twitch/TwitchChannel.hpp"
#include "providers/twitch/api/TwitchGql.hpp"
#include "util/PostToThread.hpp"
#include "util/Twitch.hpp"

#include <functional>
#include <utility>

namespace chatterino::commands {
namespace {

enum class EditorAction {
    Add,
    Remove,
};

QString usageForAction(EditorAction action)
{
    switch (action)
    {
        case EditorAction::Add:
            return QStringLiteral(
                "Usage: \"/editor <username>\" - Add a channel editor.");
        case EditorAction::Remove:
            return QStringLiteral(
                "Usage: \"/uneditor <username>\" - Remove a channel editor.");
    }
    return {};
}

QString successMessage(EditorAction action, const QString &target)
{
    switch (action)
    {
        case EditorAction::Add:
            return QString("Added %1 as editor.").arg(target);
        case EditorAction::Remove:
            return QString("Removed editor from %1.").arg(target);
    }
    return {};
}

QString broadcasterPermissionMessage()
{
    return QStringLiteral("This action needs the broadcaster account.");
}

QString normalizeEditorError(EditorAction action, const QString &target,
                             const QString &error)
{
    const auto lowered = error.toLower();
    const auto upper = error.toUpper();

    if (lowered.contains("permission") ||
        lowered.contains("not authorized") ||
        lowered.contains("not_authorized") ||
        lowered.contains("unauthorized") || lowered.contains("forbidden") ||
        lowered.contains("unauthenticated") || lowered.contains("401") ||
        lowered.contains("403"))
    {
        return broadcasterPermissionMessage();
    }

    switch (action)
    {
        case EditorAction::Add:
            if ((upper.contains("ALREADY") || upper.contains("EXISTS")) &&
                upper.contains("EDITOR"))
            {
                return QString("%1 is already an editor of this channel.")
                    .arg(target);
            }
            break;

        case EditorAction::Remove:
            if ((upper.contains("NOT") || upper.contains("MISSING") ||
                 upper.contains("DOES_NOT_EXIST")) &&
                upper.contains("EDITOR"))
            {
                return QString("%1 is not an editor of this channel.")
                    .arg(target);
            }
            break;
    }

    return error;
}

void runEditorMutation(EditorAction action, const QString &channelId,
                       const QString &targetLogin, const QString &token,
                       const ChannelPtr &channel)
{
    auto failure = [channel, action, targetLogin](const QString &error) {
        runInGuiThread([channel, action, targetLogin, error]() mutable {
            if (channel != nullptr)
            {
                channel->addSystemMessage(
                    normalizeEditorError(action, targetLogin, error));
            }
        });
    };

    if (action == EditorAction::Add)
    {
        TwitchGql::addEditorUser(
            channelId, targetLogin, token,
            [channel, targetLogin]() mutable {
                runInGuiThread([channel, targetLogin]() mutable {
                    if (channel != nullptr)
                    {
                        channel->addSystemMessage(
                            successMessage(EditorAction::Add, targetLogin));
                    }
                });
            },
            std::move(failure));
        return;
    }

    TwitchGql::removeEditorUser(
        channelId, targetLogin, token,
        [channel, targetLogin]() mutable {
            runInGuiThread([channel, targetLogin]() mutable {
                if (channel != nullptr)
                {
                    channel->addSystemMessage(
                        successMessage(EditorAction::Remove, targetLogin));
                }
            });
        },
        std::move(failure));
}

QString runEditorCommand(const CommandContext &ctx, EditorAction action)
{
    if (ctx.channel == nullptr)
    {
        return "";
    }

    if (ctx.twitchChannel == nullptr)
    {
        ctx.channel->addSystemMessage(
            "This role command only works in Twitch channels.");
        return "";
    }

    if (!ctx.twitchChannel->isBroadcaster())
    {
        ctx.channel->addSystemMessage(broadcasterPermissionMessage());
        return "";
    }

    if (ctx.words.size() < 2)
    {
        ctx.channel->addSystemMessage(usageForAction(action));
        return "";
    }

    const auto channelId = ctx.twitchChannel->roomId();
    if (channelId.isEmpty())
    {
        ctx.channel->addSystemMessage(
            "Channel ID is still loading. Try again in a moment.");
        return "";
    }

    auto current = getApp()->getAccounts()->twitch.getCurrent();
    if (!current || current->isAnon() ||
        current->getOAuthToken().trimmed().isEmpty())
    {
        ctx.channel->addSystemMessage(
            "You need to be logged in to a Twitch account to manage editors. "
            "Log in and try again.");
        return "";
    }
    const auto token = current->getOAuthToken();

    auto target = ctx.words.at(1).trimmed();
    stripChannelName(target);
    if (target.isEmpty())
    {
        ctx.channel->addSystemMessage(usageForAction(action));
        return "";
    }

    // Editor mutations take a login name, never a user id.
    auto targetLogin = target.toLower();
    if (!twitchUserNameRegexp().match(targetLogin).hasMatch())
    {
        ctx.channel->addSystemMessage(
            QString("Invalid Twitch username: %1").arg(targetLogin));
        return "";
    }

    if (targetLogin.compare(ctx.twitchChannel->getName(),
                            Qt::CaseInsensitive) == 0)
    {
        ctx.channel->addSystemMessage(
            "The broadcaster already owns the channel.");
        return "";
    }

    runEditorMutation(action, channelId, targetLogin, token, ctx.channel);
    return "";
}

}  // namespace

QString addEditorUser(const CommandContext &ctx)
{
    return runEditorCommand(ctx, EditorAction::Add);
}

QString removeEditorUser(const CommandContext &ctx)
{
    return runEditorCommand(ctx, EditorAction::Remove);
}

}  // namespace chatterino::commands
