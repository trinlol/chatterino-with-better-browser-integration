// SPDX-FileCopyrightText: 2026 Contributors to Chatterino <https://chatterino.com>
//
// SPDX-License-Identifier: MIT

#include "widgets/dialogs/ChannelDevToolsDialog.hpp"

#include "Application.hpp"
#include "controllers/accounts/AccountController.hpp"
#include "providers/twitch/TwitchAccount.hpp"
#include "providers/twitch/TwitchChannel.hpp"
#include "util/Clipboard.hpp"
#include "util/Helpers.hpp"

#include <QDateTime>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QPushButton>
#include <QScrollArea>
#include <QVBoxLayout>

namespace chatterino {

ChannelDevToolsDialog::ChannelDevToolsDialog(const ChannelPtr &channel,
                                             QWidget *parent)
    : BaseWindow(
          {
              BaseWindow::Flags::EnableCustomFrame,
              BaseWindow::Flags::Dialog,
              BaseWindow::DisableLayoutSave,
              BaseWindow::BoundsCheckOnShow,
          },
          parent)
    , channel_(channel)
{
    this->resize(680, 720);
    this->setupUi();
    this->attachChannel(channel);

    connect(&this->timer_, &QTimer::timeout, this,
            &ChannelDevToolsDialog::onTickSecond);
    this->timer_.start(1000);
}

ChannelDevToolsDialog::~ChannelDevToolsDialog()
{
    this->timer_.stop();
}

void ChannelDevToolsDialog::attachChannel(const ChannelPtr &channel)
{
    this->channel_ = channel;

    this->messageTimestamps_.clear();
    this->totalSessionMessages_ = 0;

    QString chanName = this->channel_ ? this->channel_->getName()
                                      : QStringLiteral("(none)");
    this->setWindowTitle(
        QStringLiteral("[Channel Developer Tools] - #%1").arg(chanName));

    if (this->channel_)
    {
        this->messageConnection_ =
            std::make_unique<pajlada::Signals::ScopedConnection>(
                this->channel_->messageAppended.connect(
                    [this](MessagePtr /*msg*/, auto) {
                        this->onMessageReceived();
                    }));
    }
    else
    {
        this->messageConnection_.reset();
    }

    this->onTickSecond();
}

void ChannelDevToolsDialog::onMessageReceived()
{
    qint64 now = QDateTime::currentMSecsSinceEpoch();
    this->messageTimestamps_.push_back(now);
    this->totalSessionMessages_++;
}

void ChannelDevToolsDialog::setupUi()
{
    auto *rootLayout = new QVBoxLayout(this->getLayoutContainer());
    rootLayout->setContentsMargins(12, 12, 12, 12);
    rootLayout->setSpacing(10);

    auto *scrollArea = new QScrollArea(this);
    scrollArea->setWidgetResizable(true);
    scrollArea->setFrameShape(QFrame::NoFrame);

    auto *container = new QWidget(scrollArea);
    auto *layout = new QVBoxLayout(container);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(12);

    // 1. Identity & Room Metadata
    auto *idGroup =
        new QGroupBox(QStringLiteral("Channel & Room Identity"), container);
    auto *idForm = new QFormLayout(idGroup);
    idForm->setLabelAlignment(Qt::AlignRight);

    this->channelNameLabel_ = new QLabel(idGroup);
    this->channelNameLabel_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    idForm->addRow(QStringLiteral("Channel Name:"), this->channelNameLabel_);

    auto *roomRow = new QHBoxLayout;
    this->roomIdLabel_ = new QLabel(idGroup);
    this->roomIdLabel_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    roomRow->addWidget(this->roomIdLabel_, 1);
    auto *copyRoomBtn = new QPushButton(QStringLiteral("Copy Room ID"), idGroup);
    copyRoomBtn->setMaximumWidth(100);
    connect(copyRoomBtn, &QPushButton::clicked, this, [this] {
        if (this->roomIdLabel_)
        {
            crossPlatformCopy(this->roomIdLabel_->text());
        }
    });
    roomRow->addWidget(copyRoomBtn);
    idForm->addRow(QStringLiteral("Room ID (Broadcaster):"), roomRow);

    auto *userRow = new QHBoxLayout;
    this->userIdLabel_ = new QLabel(idGroup);
    this->userIdLabel_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    userRow->addWidget(this->userIdLabel_, 1);
    auto *copyUserBtn = new QPushButton(QStringLiteral("Copy User ID"), idGroup);
    copyUserBtn->setMaximumWidth(100);
    connect(copyUserBtn, &QPushButton::clicked, this, [this] {
        if (this->userIdLabel_)
        {
            crossPlatformCopy(this->userIdLabel_->text());
        }
    });
    userRow->addWidget(copyUserBtn);
    idForm->addRow(QStringLiteral("Current Logged-in User:"), userRow);

    layout->addWidget(idGroup);

    // 2. Room Modes (Raw IRC RoomState)
    auto *modesGroup =
        new QGroupBox(QStringLiteral("Room Modes (Raw Twitch State)"), container);
    auto *modesLayout = new QVBoxLayout(modesGroup);
    this->roomModesLabel_ = new QLabel(modesGroup);
    this->roomModesLabel_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    modesLayout->addWidget(this->roomModesLabel_);
    layout->addWidget(modesGroup);

    // 3. Live Chat Throughput & Telemetry
    auto *telemetryGroup =
        new QGroupBox(QStringLiteral("Live Chat Throughput"), container);
    auto *telemetryForm = new QFormLayout(telemetryGroup);
    telemetryForm->setLabelAlignment(Qt::AlignRight);

    this->velocityLabel_ = new QLabel(telemetryGroup);
    this->velocityLabel_->setStyleSheet(
        QStringLiteral("font-weight: bold; font-size: 13px; color: #40c463;"));
    telemetryForm->addRow(QStringLiteral("Message Velocity:"),
                          this->velocityLabel_);

    this->sessionMessagesLabel_ = new QLabel(telemetryGroup);
    telemetryForm->addRow(QStringLiteral("Messages Processed:"),
                          this->sessionMessagesLabel_);

    layout->addWidget(telemetryGroup);

    // 4. Stream & Category Health
    auto *streamGroup =
        new QGroupBox(QStringLiteral("Twitch Stream & API Metrics"), container);
    auto *streamForm = new QFormLayout(streamGroup);
    streamForm->setLabelAlignment(Qt::AlignRight);

    this->streamStatusLabel_ = new QLabel(streamGroup);
    streamForm->addRow(QStringLiteral("Stream Status:"),
                       this->streamStatusLabel_);

    this->viewerCountLabel_ = new QLabel(streamGroup);
    streamForm->addRow(QStringLiteral("Exact Viewers:"),
                       this->viewerCountLabel_);

    this->gameLabel_ = new QLabel(streamGroup);
    this->gameLabel_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    streamForm->addRow(QStringLiteral("Category / Game:"), this->gameLabel_);

    this->streamTitleLabel_ = new QLabel(streamGroup);
    this->streamTitleLabel_->setWordWrap(true);
    this->streamTitleLabel_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    streamForm->addRow(QStringLiteral("Stream Title:"),
                       this->streamTitleLabel_);

    layout->addWidget(streamGroup);

    // 5. Emote Asset Statistics
    auto *emotesGroup =
        new QGroupBox(QStringLiteral("Loaded Channel Emote Assets"), container);
    auto *emotesLayout = new QVBoxLayout(emotesGroup);
    this->emotesCountLabel_ = new QLabel(emotesGroup);
    this->emotesCountLabel_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    emotesLayout->addWidget(this->emotesCountLabel_);
    layout->addWidget(emotesGroup);

    scrollArea->setWidget(container);
    rootLayout->addWidget(scrollArea, 1);

    // Bottom Action Row
    auto *bottomRow = new QHBoxLayout;
    auto *copyJsonBtn =
        new QPushButton(QStringLiteral("Copy Diagnostics (JSON)"), this);
    connect(copyJsonBtn, &QPushButton::clicked, this, [this] {
        crossPlatformCopy(this->buildDiagnosticsJson());
    });
    bottomRow->addWidget(copyJsonBtn);

    bottomRow->addStretch(1);

    auto *closeBtn = new QPushButton(QStringLiteral("Close"), this);
    connect(closeBtn, &QPushButton::clicked, this, &QWidget::close);
    bottomRow->addWidget(closeBtn);

    rootLayout->addLayout(bottomRow);
}

void ChannelDevToolsDialog::onTickSecond()
{
    if (!this->channel_)
    {
        return;
    }

    auto *twitchChannel = dynamic_cast<TwitchChannel *>(this->channel_.get());

    // Channel name & ID
    this->channelNameLabel_->setText(
        QStringLiteral("#%1").arg(this->channel_->getName()));

    if (twitchChannel != nullptr)
    {
        this->roomIdLabel_->setText(twitchChannel->roomId().isEmpty()
                                        ? QStringLiteral("(pending / empty)")
                                        : twitchChannel->roomId());
    }
    else
    {
        this->roomIdLabel_->setText(QStringLiteral("(not a twitch channel)"));
    }

    auto currentAccount = getApp()->getAccounts()->twitch.getCurrent();
    if (currentAccount && !currentAccount->isAnon())
    {
        this->userIdLabel_->setText(QStringLiteral("%1 (ID: %2)")
                                        .arg(currentAccount->getUserName(),
                                             currentAccount->getUserId()));
    }
    else
    {
        this->userIdLabel_->setText(QStringLiteral("Anonymous / Not logged in"));
    }

    // Room modes
    if (twitchChannel != nullptr)
    {
        auto modes = twitchChannel->accessRoomModes();
        QStringList modeList;
        modeList.append(QStringLiteral("Sub-Only: %1")
                            .arg(modes->submode ? QStringLiteral("ON")
                                                : QStringLiteral("OFF")));
        modeList.append(QStringLiteral("Emote-Only: %1")
                            .arg(modes->emoteOnly ? QStringLiteral("ON")
                                                  : QStringLiteral("OFF")));
        modeList.append(
            QStringLiteral("Followers-Only: %1")
                .arg(modes->followerOnly == -1
                         ? QStringLiteral("OFF")
                         : (modes->followerOnly == 0
                                ? QStringLiteral("ON (0m)")
                                : QStringLiteral("ON (%1m)")
                                      .arg(modes->followerOnly))));
        modeList.append(QStringLiteral("Slow Mode: %1")
                            .arg(modes->slowMode == 0
                                     ? QStringLiteral("OFF")
                                     : QStringLiteral("ON (%1s)")
                                           .arg(modes->slowMode)));
        modeList.append(QStringLiteral("R9K (Unique): %1")
                            .arg(modes->r9k ? QStringLiteral("ON")
                                            : QStringLiteral("OFF")));

        this->roomModesLabel_->setText(modeList.join(QStringLiteral("  |  ")));
    }
    else
    {
        this->roomModesLabel_->setText(QStringLiteral("(unavailable)"));
    }

    // Chat velocity (messages in last 5s and last 60s)
    qint64 now = QDateTime::currentMSecsSinceEpoch();
    while (!this->messageTimestamps_.empty() &&
           (now - this->messageTimestamps_.front()) > 60000)
    {
        this->messageTimestamps_.pop_front();
    }

    int countLast60s = static_cast<int>(this->messageTimestamps_.size());
    int countLast5s = 0;
    for (auto it = this->messageTimestamps_.rbegin();
         it != this->messageTimestamps_.rend(); ++it)
    {
        if (now - *it <= 5000)
        {
            countLast5s++;
        }
        else
        {
            break;
        }
    }

    double msgsPerSec = countLast5s / 5.0;
    this->velocityLabel_->setText(
        QStringLiteral("%1 msg/s  (%2 msg/min)")
            .arg(QString::number(msgsPerSec, 'f', 1),
                 QString::number(countLast60s)));

    this->sessionMessagesLabel_->setText(
        QStringLiteral("%1 messages this session (in-memory buffer: %2)")
            .arg(QString::number(this->totalSessionMessages_),
                 QString::number(
                     this->channel_->getMessageSnapshot().size())));

    // Stream status
    if (twitchChannel != nullptr)
    {
        auto stream = twitchChannel->accessStreamStatus();
        this->streamStatusLabel_->setText(
            stream->live ? QStringLiteral("<span style='color: #40c463; font-weight: bold;'>LIVE</span> (Uptime: %1)")
                               .arg(stream->uptime)
                         : QStringLiteral("<span style='color: #888;'>OFFLINE</span>"));
        this->viewerCountLabel_->setText(
            stream->live ? QStringLiteral("%1 viewers").arg(stream->viewerCount)
                         : QStringLiteral("0"));
        this->gameLabel_->setText(
            stream->game.isEmpty() ? QStringLiteral("(none)")
                                   : QStringLiteral("%1 (ID: %2)")
                                         .arg(stream->game, stream->gameId));
        this->streamTitleLabel_->setText(
            stream->title.isEmpty() ? QStringLiteral("(no title)")
                                    : stream->title);

        // Emote assets
        int subCount = twitchChannel->localTwitchEmotes()
                           ? static_cast<int>(
                                 twitchChannel->localTwitchEmotes()->size())
                           : 0;
        int sevenCount =
            twitchChannel->seventvEmotes()
                ? static_cast<int>(twitchChannel->seventvEmotes()->size())
                : 0;
        int bttvCount =
            twitchChannel->bttvEmotes()
                ? static_cast<int>(twitchChannel->bttvEmotes()->size())
                : 0;
        int ffzCount =
            twitchChannel->ffzEmotes()
                ? static_cast<int>(twitchChannel->ffzEmotes()->size())
                : 0;

        this->emotesCountLabel_->setText(
            QStringLiteral("Twitch Sub Emotes: <b>%1</b>  |  7TV: <b>%2</b>  |  BTTV: <b>%3</b>  |  FFZ: <b>%4</b>  |  Total: <b>%5</b>")
                .arg(QString::number(subCount), QString::number(sevenCount),
                     QString::number(bttvCount), QString::number(ffzCount),
                     QString::number(subCount + sevenCount + bttvCount +
                                     ffzCount)));
    }
    else
    {
        this->streamStatusLabel_->setText(QStringLiteral("(unavailable)"));
        this->viewerCountLabel_->setText(QStringLiteral("(unavailable)"));
        this->gameLabel_->setText(QStringLiteral("(unavailable)"));
        this->streamTitleLabel_->setText(QStringLiteral("(unavailable)"));
        this->emotesCountLabel_->setText(QStringLiteral("(unavailable)"));
    }
}

QString ChannelDevToolsDialog::buildDiagnosticsJson() const
{
    QJsonObject root;
    if (!this->channel_)
    {
        return QStringLiteral("{}");
    }

    root[QStringLiteral("channel_name")] = this->channel_->getName();

    auto *twitchChannel = dynamic_cast<TwitchChannel *>(this->channel_.get());
    if (twitchChannel != nullptr)
    {
        root[QStringLiteral("room_id")] = twitchChannel->roomId();

        auto modes = twitchChannel->accessRoomModes();
        QJsonObject modesObj;
        modesObj[QStringLiteral("sub_only")] = modes->submode;
        modesObj[QStringLiteral("emote_only")] = modes->emoteOnly;
        modesObj[QStringLiteral("follower_only_minutes")] = modes->followerOnly;
        modesObj[QStringLiteral("slow_mode_seconds")] = modes->slowMode;
        modesObj[QStringLiteral("r9k_unique")] = modes->r9k;
        root[QStringLiteral("room_modes")] = modesObj;

        auto stream = twitchChannel->accessStreamStatus();
        QJsonObject streamObj;
        streamObj[QStringLiteral("live")] = stream->live;
        streamObj[QStringLiteral("viewer_count")] =
            static_cast<qint64>(stream->viewerCount);
        streamObj[QStringLiteral("uptime")] = stream->uptime;
        streamObj[QStringLiteral("stream_id")] = stream->streamId;
        streamObj[QStringLiteral("game")] = stream->game;
        streamObj[QStringLiteral("game_id")] = stream->gameId;
        streamObj[QStringLiteral("title")] = stream->title;
        root[QStringLiteral("stream")] = streamObj;

        QJsonObject emotesObj;
        emotesObj[QStringLiteral("twitch_sub_emotes")] =
            twitchChannel->localTwitchEmotes()
                ? static_cast<qint64>(twitchChannel->localTwitchEmotes()->size())
                : 0;
        emotesObj[QStringLiteral("seventv_emotes")] =
            twitchChannel->seventvEmotes()
                ? static_cast<qint64>(twitchChannel->seventvEmotes()->size())
                : 0;
        emotesObj[QStringLiteral("bttv_emotes")] =
            twitchChannel->bttvEmotes()
                ? static_cast<qint64>(twitchChannel->bttvEmotes()->size())
                : 0;
        emotesObj[QStringLiteral("ffz_emotes")] =
            twitchChannel->ffzEmotes()
                ? static_cast<qint64>(twitchChannel->ffzEmotes()->size())
                : 0;
        root[QStringLiteral("emotes")] = emotesObj;
    }

    QJsonObject telemetryObj;
    telemetryObj[QStringLiteral("total_session_messages")] =
        this->totalSessionMessages_;
    telemetryObj[QStringLiteral("recent_messages_in_last_minute")] =
        static_cast<qint64>(this->messageTimestamps_.size());
    root[QStringLiteral("telemetry")] = telemetryObj;

    auto currentAccount = getApp()->getAccounts()->twitch.getCurrent();
    if (currentAccount && !currentAccount->isAnon())
    {
        root[QStringLiteral("current_user_id")] = currentAccount->getUserId();
        root[QStringLiteral("current_user_name")] =
            currentAccount->getUserName();
    }

    return QString::fromUtf8(
        QJsonDocument(root).toJson(QJsonDocument::Indented));
}

}  // namespace chatterino
