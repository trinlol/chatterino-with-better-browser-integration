// SPDX-FileCopyrightText: 2026 Contributors to Chatterino <https://chatterino.com>
//
// SPDX-License-Identifier: MIT

#include "controllers/accounts/AccountController.hpp"
#include "controllers/highlights/HighlightController.hpp"
#include "mocks/BaseApplication.hpp"
#include "mocks/Logging.hpp"
#include "mocks/TwitchIrcServer.hpp"
#include "providers/twitch/MarqueeEvent.hpp"
#include "providers/twitch/TwitchChannel.hpp"
#include "providers/streamelements/StreamElementsManager.hpp"
#include "Test.hpp"

#include <QJsonObject>
#include <QString>

using namespace chatterino;

namespace {

class MockApplication : public mock::BaseApplication
{
public:
    MockApplication()
        : highlights(this->settings, &this->accounts)
    {
    }

    ILogging *getChatLogger() override
    {
        return &this->logging;
    }

    ITwitchIrcServer *getTwitch() override
    {
        return &this->twitch;
    }

    AccountController *getAccounts() override
    {
        return &this->accounts;
    }

    HighlightController *getHighlights() override
    {
        return &this->highlights;
    }

    mock::EmptyLogging logging;
    mock::MockTwitchIrcServer twitch;
    AccountController accounts;
    HighlightController highlights;
};

}  // namespace

TEST(MarqueeEventBuffer, CapacityLimitFifo)
{
    MockApplication app;
    auto chan = std::make_shared<TwitchChannel>("testchannel");

    EXPECT_TRUE(chan->recentMarqueeEvents().empty());

    for (int i = 1; i <= 20; ++i)
    {
        MarqueeEvent ev;
        ev.type = MarqueeEvent::Type::Subscription;
        ev.username = QStringLiteral("user%1").arg(i);
        ev.displayName = QStringLiteral("User%1").arg(i);
        ev.amountText = QStringLiteral("Tier 1");
        chan->addMarqueeEvent(ev);
    }

    const auto &recent = chan->recentMarqueeEvents();
    EXPECT_EQ(recent.size(), 15);
    // Oldest item retained should be user6, newest user20
    EXPECT_EQ(recent.front().username, QStringLiteral("user6"));
    EXPECT_EQ(recent.back().username, QStringLiteral("user20"));

    chan->clearMarqueeEvents();
    EXPECT_TRUE(chan->recentMarqueeEvents().empty());
}

TEST(MarqueeEventBuffer, Signals)
{
    MockApplication app;
    auto chan = std::make_shared<TwitchChannel>("testchannel");

    int addCount = 0;
    int clearCount = 0;
    QString lastUser;

    pajlada::Signals::SignalHolder holder;
    holder.managedConnect(chan->marqueeEventAdded, [&](const MarqueeEvent &ev) {
        ++addCount;
        lastUser = ev.username;
    });

    holder.managedConnect(chan->marqueeEventsCleared, [&]() {
        ++clearCount;
    });

    MarqueeEvent ev1;
    ev1.type = MarqueeEvent::Type::Bits;
    ev1.username = QStringLiteral("cheerer1");
    chan->addMarqueeEvent(ev1);

    EXPECT_EQ(addCount, 1);
    EXPECT_EQ(lastUser, QStringLiteral("cheerer1"));

    MarqueeEvent ev2;
    ev2.type = MarqueeEvent::Type::Subscription;
    ev2.username = QStringLiteral("gifter1");
    chan->addMarqueeEvent(ev2);

    EXPECT_EQ(addCount, 2);
    EXPECT_EQ(lastUser, QStringLiteral("gifter1"));

    chan->clearMarqueeEvents();
    EXPECT_EQ(clearCount, 1);
}

TEST(MarqueeStreamElements, ParseChannelTipsPayload)
{
    QJsonObject root;
    root[QStringLiteral("topic")] = QStringLiteral("channel.tips");

    QJsonObject userObj;
    userObj[QStringLiteral("username")] = QStringLiteral("generous_viewer");

    QJsonObject donationObj;
    donationObj[QStringLiteral("user")] = userObj;
    donationObj[QStringLiteral("amount")] = 15.50;
    donationObj[QStringLiteral("currency")] = QStringLiteral("USD");
    donationObj[QStringLiteral("message")] = QStringLiteral("Great stream!");

    QJsonObject dataObj;
    dataObj[QStringLiteral("donation")] = donationObj;
    root[QStringLiteral("data")] = dataObj;

    auto parsed = StreamElementsManager::parseTipPayload(root);
    ASSERT_TRUE(parsed.has_value());
    EXPECT_EQ(parsed->type, MarqueeEvent::Type::Tip);
    EXPECT_EQ(parsed->username, QStringLiteral("generous_viewer"));
    EXPECT_EQ(parsed->displayName, QStringLiteral("generous_viewer"));
    EXPECT_EQ(parsed->userMessage, QStringLiteral("Great stream!"));
    EXPECT_TRUE(parsed->amountText.contains(QStringLiteral("15.50")));
    EXPECT_TRUE(parsed->detailText.contains(QStringLiteral("tipped")));
}

TEST(MarqueeStreamElements, ParseTipTypePayload)
{
    QJsonObject root;
    root[QStringLiteral("type")] = QStringLiteral("tip");

    QJsonObject dataObj;
    dataObj[QStringLiteral("username")] = QStringLiteral("tipper42");
    dataObj[QStringLiteral("amount")] = 5.00;
    dataObj[QStringLiteral("currency")] = QStringLiteral("EUR");
    dataObj[QStringLiteral("message")] = QStringLiteral("Have a coffee");
    root[QStringLiteral("data")] = dataObj;

    auto parsed = StreamElementsManager::parseTipPayload(root);
    ASSERT_TRUE(parsed.has_value());
    EXPECT_EQ(parsed->type, MarqueeEvent::Type::Tip);
    EXPECT_EQ(parsed->username, QStringLiteral("tipper42"));
    EXPECT_EQ(parsed->userMessage, QStringLiteral("Have a coffee"));
    EXPECT_TRUE(parsed->amountText.contains(QStringLiteral("5.00")));
}

TEST(MarqueeStreamElements, ParseInvalidPayloads)
{
    // Empty object
    EXPECT_FALSE(StreamElementsManager::parseTipPayload(QJsonObject{}).has_value());

    // Irrelevant topic
    QJsonObject otherTopic;
    otherTopic[QStringLiteral("topic")] = QStringLiteral("channel.follows");
    EXPECT_FALSE(StreamElementsManager::parseTipPayload(otherTopic).has_value());

    // Zero amount
    QJsonObject zeroAmount;
    zeroAmount[QStringLiteral("type")] = QStringLiteral("tip");
    QJsonObject dataObj;
    dataObj[QStringLiteral("username")] = QStringLiteral("empty");
    dataObj[QStringLiteral("amount")] = 0.0;
    zeroAmount[QStringLiteral("data")] = dataObj;
    EXPECT_FALSE(StreamElementsManager::parseTipPayload(zeroAmount).has_value());

    // Empty username
    QJsonObject noUser;
    noUser[QStringLiteral("type")] = QStringLiteral("tip");
    QJsonObject dataObj2;
    dataObj2[QStringLiteral("username")] = QStringLiteral("");
    dataObj2[QStringLiteral("amount")] = 10.0;
    noUser[QStringLiteral("data")] = dataObj2;
    EXPECT_FALSE(StreamElementsManager::parseTipPayload(noUser).has_value());
}
