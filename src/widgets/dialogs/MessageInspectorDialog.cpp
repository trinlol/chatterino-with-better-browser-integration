// SPDX-FileCopyrightText: 2026 Contributors to Chatterino <https://chatterino.com>
//
// SPDX-License-Identifier: MIT

#include "widgets/dialogs/MessageInspectorDialog.hpp"

#include "providers/twitch/TwitchBadge.hpp"
#include "singletons/Theme.hpp"
#include "util/Clipboard.hpp"

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
#include <QTextEdit>
#include <QVBoxLayout>

namespace chatterino {

MessageInspectorDialog::MessageInspectorDialog(MessagePtr message,
                                               QWidget *parent)
    : BaseWindow(
          {
              BaseWindow::Flags::EnableCustomFrame,
              BaseWindow::Flags::Dialog,
              BaseWindow::DisableLayoutSave,
              BaseWindow::BoundsCheckOnShow,
          },
          parent)
    , message_(std::move(message))
{
    this->setWindowTitle(
        QStringLiteral("[Message Inspector] - %1")
            .arg(this->message_->id.isEmpty() ? QStringLiteral("Local/System")
                                              : this->message_->id.left(8)));
    this->resize(620, 680);
    this->setupUi();
}

void MessageInspectorDialog::setupUi()
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

    // 1. Identity Group
    auto *idGroup = new QGroupBox(QStringLiteral("Message Identity"), container);
    auto *idForm = new QFormLayout(idGroup);
    idForm->setLabelAlignment(Qt::AlignRight);

    // UUID
    auto *uuidRow = new QHBoxLayout;
    auto *uuidLabel = new QLabel(
        this->message_->id.isEmpty() ? QStringLiteral("(none)")
                                     : this->message_->id,
        idGroup);
    uuidLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    uuidRow->addWidget(uuidLabel, 1);
    if (!this->message_->id.isEmpty())
    {
        auto *copyUuidBtn = new QPushButton(QStringLiteral("Copy ID"), idGroup);
        copyUuidBtn->setMaximumWidth(80);
        connect(copyUuidBtn, &QPushButton::clicked, this, [id = this->message_->id] {
            crossPlatformCopy(id);
        });
        uuidRow->addWidget(copyUuidBtn);
    }
    idForm->addRow(QStringLiteral("Message UUID:"), uuidRow);

    // User ID
    auto *userRow = new QHBoxLayout;
    auto *userIdLabel = new QLabel(
        this->message_->userID.isEmpty() ? QStringLiteral("(none)")
                                         : this->message_->userID,
        idGroup);
    userIdLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    userRow->addWidget(userIdLabel, 1);
    if (!this->message_->userID.isEmpty())
    {
        auto *copyUidBtn =
            new QPushButton(QStringLiteral("Copy User ID"), idGroup);
        copyUidBtn->setMaximumWidth(100);
        connect(copyUidBtn, &QPushButton::clicked, this,
                [uid = this->message_->userID] {
                    crossPlatformCopy(uid);
                });
        userRow->addWidget(copyUidBtn);
    }
    idForm->addRow(QStringLiteral("Sender User ID:"), userRow);

    // Sender
    QString senderInfo = QStringLiteral("%1 (%2)")
                             .arg(this->message_->displayName,
                                  this->message_->loginName);
    auto *senderLabel = new QLabel(senderInfo, idGroup);
    if (this->message_->usernameColor.isValid())
    {
        senderLabel->setStyleSheet(QStringLiteral("color: %1; font-weight: bold;")
                                       .arg(this->message_->usernameColor.name()));
    }
    idForm->addRow(QStringLiteral("Sender:"), senderLabel);

    // Channel
    idForm->addRow(
        QStringLiteral("Channel:"),
        new QLabel(QStringLiteral("#%1").arg(this->message_->channelName), idGroup));

    layout->addWidget(idGroup);

    // 2. Timing & Latency Group
    auto *timeGroup =
        new QGroupBox(QStringLiteral("Timing & Transit Telemetry"), container);
    auto *timeForm = new QFormLayout(timeGroup);
    timeForm->setLabelAlignment(Qt::AlignRight);

    QString serverTimeStr =
        this->message_->serverReceivedTime.isValid()
            ? this->message_->serverReceivedTime.toString(
                  QStringLiteral("yyyy-MM-dd HH:mm:ss.zzz UTC"))
            : QStringLiteral("(not present)");
    timeForm->addRow(QStringLiteral("Twitch Server Sent:"),
                     new QLabel(serverTimeStr, timeGroup));

    QString clientTimeStr = this->message_->parseTime.toString(
        QStringLiteral("HH:mm:ss.zzz"));
    timeForm->addRow(QStringLiteral("Client Parsed:"),
                     new QLabel(clientTimeStr, timeGroup));

    if (this->message_->serverReceivedTime.isValid())
    {
        QDateTime clientDate(this->message_->serverReceivedTime.toLocalTime().date(),
                             this->message_->parseTime, Qt::LocalTime);
        qint64 latencyMs =
            this->message_->serverReceivedTime.msecsTo(clientDate.toUTC());

        QString latencyDisplay;
        if (latencyMs >= 0 && latencyMs < 30000)
        {
            latencyDisplay = QStringLiteral("%1 ms").arg(latencyMs);
        }
        else if (latencyMs < 0 && latencyMs > -10000)
        {
            latencyDisplay = QStringLiteral("%1 ms (system clock ahead)").arg(latencyMs);
        }
        else
        {
            latencyDisplay = QStringLiteral("%1 ms").arg(latencyMs);
        }

        auto *latencyLabel = new QLabel(latencyDisplay, timeGroup);
        latencyLabel->setStyleSheet(QStringLiteral(
            "background-color: rgba(30, 180, 80, 60); border-radius: 4px; padding: 2px 6px; font-weight: bold;"));
        timeForm->addRow(QStringLiteral("Transit Latency:"), latencyLabel);
    }

    layout->addWidget(timeGroup);

    // 3. Badges & Badge-Info Group
    auto *badgeGroup =
        new QGroupBox(QStringLiteral("Twitch & External Badges"), container);
    auto *badgeLayout = new QVBoxLayout(badgeGroup);

    if (!this->message_->twitchBadges.empty())
    {
        QStringList twitchBadgeStrings;
        for (const auto &badge : this->message_->twitchBadges)
        {
            twitchBadgeStrings.append(
                QStringLiteral("%1/%2").arg(badge.key_, badge.value_));
        }
        badgeLayout->addWidget(new QLabel(
            QStringLiteral("<b>Twitch Badges:</b> %1")
                .arg(twitchBadgeStrings.join(QStringLiteral(", "))),
            badgeGroup));
    }
    else
    {
        badgeLayout->addWidget(
            new QLabel(QStringLiteral("<b>Twitch Badges:</b> (none)"), badgeGroup));
    }

    if (!this->message_->twitchBadgeInfos.empty())
    {
        QStringList badgeInfoStrings;
        for (const auto &[k, v] : this->message_->twitchBadgeInfos)
        {
            badgeInfoStrings.append(QStringLiteral("%1=%2").arg(k, v));
        }
        badgeLayout->addWidget(new QLabel(
            QStringLiteral("<b>Badge-Info:</b> %1")
                .arg(badgeInfoStrings.join(QStringLiteral(", "))),
            badgeGroup));
    }

    if (!this->message_->externalBadges.empty())
    {
        badgeLayout->addWidget(new QLabel(
            QStringLiteral("<b>External Badges:</b> %1")
                .arg(this->message_->externalBadges.join(QStringLiteral(", "))),
            badgeGroup));
    }

    layout->addWidget(badgeGroup);

    // 4. Message Content & Raw Text
    auto *contentGroup =
        new QGroupBox(QStringLiteral("Message Content"), container);
    auto *contentLayout = new QVBoxLayout(contentGroup);

    auto *msgText = new QTextEdit(contentGroup);
    msgText->setReadOnly(true);
    msgText->setPlainText(this->message_->messageText);
    msgText->setMaximumHeight(100);
    contentLayout->addWidget(msgText);

    layout->addWidget(contentGroup);

    scrollArea->setWidget(container);
    rootLayout->addWidget(scrollArea, 1);

    // Bottom Action Buttons
    auto *bottomRow = new QHBoxLayout;
    auto *copyJsonBtn =
        new QPushButton(QStringLiteral("Copy Full JSON Payload"), this);
    connect(copyJsonBtn, &QPushButton::clicked, this, [this] {
        crossPlatformCopy(this->buildJsonRepresentation());
    });
    bottomRow->addWidget(copyJsonBtn);

    bottomRow->addStretch(1);

    auto *closeBtn = new QPushButton(QStringLiteral("Close"), this);
    connect(closeBtn, &QPushButton::clicked, this, &QWidget::close);
    bottomRow->addWidget(closeBtn);

    rootLayout->addLayout(bottomRow);
}

QString MessageInspectorDialog::buildJsonRepresentation() const
{
    QJsonObject root;
    root[QStringLiteral("id")] = this->message_->id;
    root[QStringLiteral("channel")] = this->message_->channelName;
    root[QStringLiteral("user_id")] = this->message_->userID;
    root[QStringLiteral("login")] = this->message_->loginName;
    root[QStringLiteral("display_name")] = this->message_->displayName;
    root[QStringLiteral("color")] = this->message_->usernameColor.name();
    root[QStringLiteral("text")] = this->message_->messageText;

    if (this->message_->serverReceivedTime.isValid())
    {
        root[QStringLiteral("server_received_time")] =
            this->message_->serverReceivedTime.toString(Qt::ISODateWithMs);
    }
    root[QStringLiteral("client_parse_time")] =
        this->message_->parseTime.toString(QStringLiteral("HH:mm:ss.zzz"));

    QJsonArray badgesArray;
    for (const auto &badge : this->message_->twitchBadges)
    {
        QJsonObject b;
        b[QStringLiteral("key")] = badge.key_;
        b[QStringLiteral("value")] = badge.value_;
        badgesArray.append(b);
    }
    root[QStringLiteral("twitch_badges")] = badgesArray;

    QJsonObject badgeInfoObj;
    for (const auto &[k, v] : this->message_->twitchBadgeInfos)
    {
        badgeInfoObj[k] = v;
    }
    root[QStringLiteral("badge_info")] = badgeInfoObj;

    QJsonArray extBadgesArray;
    for (const auto &ext : this->message_->externalBadges)
    {
        extBadgesArray.append(ext);
    }
    root[QStringLiteral("external_badges")] = extBadgesArray;

    return QString::fromUtf8(
        QJsonDocument(root).toJson(QJsonDocument::Indented));
}

}  // namespace chatterino
