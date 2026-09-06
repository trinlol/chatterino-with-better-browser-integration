// Ported from Moltorino (https://codeberg.org/MoltoBenne/Moltorino)
// Copyright (c) MoltoBenne - MIT License
// Adapted for Chatterino Better Browser: implemented as a chat command
// (list + redeem by title) instead of Moltorino's ChannelPointsDialog, and
// authenticates with the logged-in Chatterino account.
#include "controllers/commands/builtin/twitch/Redeem.hpp"

#include "Application.hpp"
#include "common/Channel.hpp"
#include "controllers/accounts/AccountController.hpp"
#include "controllers/commands/CommandContext.hpp"
#include "messages/Message.hpp"
#include "messages/MessageBuilder.hpp"
#include "messages/MessageElement.hpp"
#include "providers/twitch/TwitchAccount.hpp"
#include "providers/twitch/TwitchChannel.hpp"
#include "providers/twitch/api/TwitchGql.hpp"
#include "util/Helpers.hpp"
#include "util/PostToThread.hpp"

#include <QVector>

#include <algorithm>

namespace chatterino::commands {
namespace {

QString formatBalance(qint64 balance)
{
    if (balance < 0)
    {
        return {};
    }
    return QString::number(balance);
}

const GqlChannelPointReward *findRewardByTitle(
    const QVector<GqlChannelPointReward> &rewards, const QString &title)
{
    const auto lowered = title.trimmed().toLower();

    // Exact (case-insensitive) match first.
    const auto exact = std::find_if(
        rewards.cbegin(), rewards.cend(), [&](const auto &reward) {
            return reward.title.compare(lowered, Qt::CaseInsensitive) == 0;
        });
    if (exact != rewards.cend())
    {
        return &*exact;
    }

    // Then a prefix match so "/redeem high <message>" style usage works.
    const auto prefix = std::find_if(
        rewards.cbegin(), rewards.cend(), [&](const auto &reward) {
            return reward.title.toLower().startsWith(lowered);
        });
    if (prefix != rewards.cend())
    {
        return &*prefix;
    }

    return nullptr;
}

void addRewardsListMessage(const ChannelPtr &channel,
                           const GqlChannelPointRewards &rewards)
{
    if (rewards.rewards.isEmpty())
    {
        channel->addSystemMessage("This channel has no point rewards.");
        return;
    }

    MessageBuilder builder;
    QString searchText;

    QString balanceText;
    if (rewards.balance >= 0)
    {
        balanceText =
            QStringLiteral(" (%1 points)").arg(formatBalance(rewards.balance));
    }

    builder.emplace<TextElement>(
        QStringLiteral("Channel point rewards%1:").arg(balanceText),
        MessageElementFlag::Text, MessageColor::System,
        FontStyle::ChatMediumBold);
    searchText += QStringLiteral("Channel point rewards%1:").arg(balanceText);

    for (const auto &reward : rewards.rewards)
    {
        QString state;
        if (!reward.isEnabled || !reward.isInStock)
        {
            state = QStringLiteral(" (unavailable)");
        }
        else if (reward.isUserInputRequired)
        {
            state = QStringLiteral(" (needs text)");
        }

        const auto row =
            QStringLiteral("%1 - %2 points%3 (\"/redeem %1 [text]\")")
                .arg(reward.title)
                .arg(reward.cost)
                .arg(state);

        builder.emplace<LinebreakElement>(MessageElementFlag::Text);
        builder.emplace<TextElement>(row, MessageElementFlag::Text,
                                     MessageColor::System);
        searchText += '\n';
        searchText += row;
    }

    builder->flags.set(MessageFlag::System);
    builder->flags.set(MessageFlag::DoNotTriggerNotification);
    builder->messageText = searchText;
    builder->searchText = searchText;

    channel->addMessage(builder.release(), MessageContext::Original);
}

}  // namespace

QString redeem(const CommandContext &ctx)
{
    if (ctx.channel == nullptr)
    {
        return "";
    }

    if (ctx.twitchChannel == nullptr)
    {
        ctx.channel->addSystemMessage(
            "The /redeem command only works in Twitch channels.");
        return "";
    }

    auto current = getApp()->getAccounts()->twitch.getCurrent();
    if (!current || current->isAnon() ||
        current->getOAuthToken().trimmed().isEmpty())
    {
        ctx.channel->addSystemMessage(
            "You need to be logged in to redeem channel point rewards. Log "
            "in and try again.");
        return "";
    }
    const auto token = current->getOAuthToken();

    const auto channelLogin = ctx.twitchChannel->getName();
    const auto hasTitle = ctx.words.size() > 1;
    const auto title = ctx.words.value(1).trimmed();
    const auto userInput =
        ctx.words.size() > 2 ? ctx.words.mid(2).join(' ').trimmed() : QString();

    if (!hasTitle)
    {
        ctx.channel->addSystemMessage("Fetching channel point rewards...");
    }

    TwitchGql::getChannelPointRewards(
        channelLogin, token,
        [channel = ctx.channel, hasTitle, title,
         userInput](GqlChannelPointRewards rewards) mutable {
            runInGuiThread([channel, hasTitle, title, userInput,
                            rewards{std::move(rewards)}]() mutable {
                if (channel == nullptr)
                {
                    return;
                }

                if (!hasTitle)
                {
                    addRewardsListMessage(channel, rewards);
                    return;
                }

                if (title.isEmpty())
                {
                    addRewardsListMessage(channel, rewards);
                    return;
                }

                const auto *reward = findRewardByTitle(rewards.rewards, title);
                if (reward == nullptr)
                {
                    channel->addSystemMessage(
                        QStringLiteral("No reward named \"%1\". Use /redeem "
                                       "without arguments to list rewards.")
                            .arg(title));
                    return;
                }

                if (!reward->isEnabled || !reward->isInStock)
                {
                    channel->addSystemMessage(
                        QStringLiteral("\"%1\" is not available right now.")
                            .arg(reward->title));
                    return;
                }

                if (reward->isUserInputRequired && userInput.isEmpty())
                {
                    channel->addSystemMessage(
                        QStringLiteral(
                            "\"%1\" needs text: /redeem %1 <text>")
                            .arg(reward->title));
                    return;
                }

                if (rewards.balance >= 0 && reward->cost > rewards.balance)
                {
                    channel->addSystemMessage(
                        QStringLiteral(
                            "\"%1\" costs %2 points but you only have %3.")
                            .arg(reward->title)
                            .arg(reward->cost)
                            .arg(rewards.balance));
                    return;
                }

                channel->addSystemMessage(
                    QStringLiteral("Redeeming \"%1\" for %2 points...")
                        .arg(reward->title)
                        .arg(reward->cost));

                TwitchGql::redeemChannelPointReward(
                    rewards.channelId, *reward, userInput,
                    getApp()->getAccounts()->twitch.getCurrent()
                        ->getOAuthToken(),
                    [channel, rewardTitle = reward->title](qint64 balance) {
                        runInGuiThread([channel, rewardTitle, balance] {
                            if (channel == nullptr)
                            {
                                return;
                            }
                            QString message =
                                QStringLiteral("Redeemed \"%1\".")
                                    .arg(rewardTitle);
                            if (balance >= 0)
                            {
                                message += QStringLiteral(" Balance: %1.")
                                               .arg(balance);
                            }
                            channel->addSystemMessage(message);
                        });
                    },
                    [channel, rewardTitle = reward->title](
                        const QString &error) {
                        runInGuiThread([channel, rewardTitle, error] {
                            if (channel != nullptr)
                            {
                                channel->addSystemMessage(
                                    QStringLiteral("Failed to redeem \"%1\": "
                                                   "%2")
                                        .arg(rewardTitle, error));
                            }
                        });
                    });
            });
        },
        [channel = ctx.channel](const QString &error) {
            runInGuiThread([channel, error] {
                if (channel != nullptr)
                {
                    channel->addSystemMessage(
                        QStringLiteral("Failed to load channel point rewards: "
                                       "%1")
                            .arg(error));
                }
            });
        });

    return "";
}

}  // namespace chatterino::commands
