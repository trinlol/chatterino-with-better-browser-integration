// Ported from Moltorino (https://codeberg.org/MoltoBenne/Moltorino)
// Copyright (c) MoltoBenne - MIT License
// Adapted for Chatterino Better Browser: authenticates with the logged-in
// Chatterino account instead of Moltorino's saved-account store.
#include "controllers/commands/builtin/twitch/LeadModerator.hpp"

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
#include <optional>
#include <utility>

namespace chatterino::commands {
namespace {

enum class LeadModAction {
    Add,
    Remove,
};

QString usageForAction(LeadModAction action)
{
    switch (action)
    {
        case LeadModAction::Add:
            return QStringLiteral(
                "Usage: \"/leadmod <username>\" - Grant lead moderator "
                "status.");
        case LeadModAction::Remove:
            return QStringLiteral(
                "Usage: \"/unleadmod <username>\" - Revoke lead moderator "
                "status.");
    }
    return {};
}

QString successMessage(LeadModAction action, const QString &target)
{
    switch (action)
    {
        case LeadModAction::Add:
            return QString("Added %1 as lead moderator.").arg(target);
        case LeadModAction::Remove:
            return QString("Removed lead moderator from %1.").arg(target);
    }
    return {};
}

QString broadcasterPermissionMessage()
{
    return QStringLiteral("This action needs the broadcaster account.");
}

QString normalizeRoleError(LeadModAction action, const QString &target,
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
        case LeadModAction::Add:
            if (upper.contains("ALREADY") || upper.contains("ROLE_ASSIGNED"))
            {
                return QString(
                           "%1 is already a lead moderator of this channel.")
                    .arg(target);
            }
            break;

        case LeadModAction::Remove:
            if (upper.contains("UNASSIGNED") ||
                upper.contains("NOT_ASSIGNED") ||
                ((upper.contains("NOT") || upper.contains("MISSING")) &&
                 (upper.contains("LEAD") || upper.contains("ROLE") ||
                  upper.contains("MOD"))))
            {
                return QString(
                           "%1 is not a lead moderator of this channel.")
                    .arg(target);
            }
            break;
    }

    return error;
}

void runLeadModMutation(LeadModAction action, const QString &channelId,
                        const QString &targetUserId,
                        const QString &targetDisplay,
                        const QString &token, const ChannelPtr &channel)
{
    auto failure = [channel, action, targetDisplay](const QString &error) {
        runInGuiThread([channel, action, targetDisplay, error]() mutable {
            if (channel != nullptr)
            {
                channel->addSystemMessage(normalizeRoleError(
                    action, targetDisplay, error));
            }
        });
    };

    if (action == LeadModAction::Add)
    {
        TwitchGql::assignLeadModerator(
            channelId, targetUserId, token,
            [channel, targetDisplay]() mutable {
                runInGuiThread([channel, targetDisplay]() mutable {
                    if (channel != nullptr)
                    {
                        channel->addSystemMessage(
                            successMessage(LeadModAction::Add, targetDisplay));
                    }
                });
            },
            std::move(failure));
        return;
    }

    TwitchGql::unassignLeadModerator(
        channelId, targetUserId, token,
        [channel, targetDisplay]() mutable {
            runInGuiThread([channel, targetDisplay]() mutable {
                if (channel != nullptr)
                {
                    channel->addSystemMessage(successMessage(
                        LeadModAction::Remove, targetDisplay));
                }
            });
        },
        std::move(failure));
}

QString runLeadModCommand(const CommandContext &ctx, LeadModAction action)
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
            "You need to be logged in to a Twitch account to manage lead "
            "moderators. Log in and try again.");
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

    auto [targetLogin, targetUserId] = parseUserNameOrID(target);
    if (targetLogin.isEmpty() && targetUserId.isEmpty())
    {
        ctx.channel->addSystemMessage(usageForAction(action));
        return "";
    }

    if (!targetLogin.isEmpty())
    {
        targetLogin = targetLogin.trimmed().toLower();
        if (!twitchUserNameRegexp().match(targetLogin).hasMatch())
        {
            ctx.channel->addSystemMessage(
                QString("Invalid Twitch username: %1").arg(targetLogin));
            return "";
        }
    }

    if (!targetUserId.isEmpty())
    {
        if (targetUserId == channelId)
        {
            ctx.channel->addSystemMessage(
                "The broadcaster already owns the channel.");
            return "";
        }

        runLeadModMutation(action, channelId, targetUserId, targetUserId,
                           token, ctx.channel);
        return "";
    }

    TwitchGql::getUserByLogin(
        targetLogin, token,
        [channel{ctx.channel}, action, channelId, targetLogin,
         token](std::optional<GqlUser> user) mutable {
            if (!user)
            {
                runInGuiThread([channel, targetLogin]() mutable {
                    if (channel != nullptr)
                    {
                        channel->addSystemMessage(
                            QString("Could not look up user: %1.")
                                .arg(targetLogin));
                    }
                });
                return;
            }

            if (user->id == channelId)
            {
                runInGuiThread([channel]() mutable {
                    if (channel != nullptr)
                    {
                        channel->addSystemMessage(
                            "The broadcaster already owns the channel.");
                    }
                });
                return;
            }

            const auto display =
                user->displayName.isEmpty() ? user->login : user->displayName;
            runLeadModMutation(action, channelId, user->id, display, token,
                               channel);
        },
        [channel{ctx.channel}, action, targetLogin](const QString &error) {
            runInGuiThread([channel, action, targetLogin, error]() mutable {
                if (channel != nullptr)
                {
                    channel->addSystemMessage(normalizeRoleError(
                        action, targetLogin, error));
                }
            });
        });

    return "";
}

}  // namespace

QString addLeadModerator(const CommandContext &ctx)
{
    return runLeadModCommand(ctx, LeadModAction::Add);
}

QString removeLeadModerator(const CommandContext &ctx)
{
    return runLeadModCommand(ctx, LeadModAction::Remove);
}

}  // namespace chatterino::commands
