// SPDX-FileCopyrightText: 2026 Contributors to Chatterino <https://chatterino.com>
//
// SPDX-License-Identifier: MIT

#pragma once

#include "common/Channel.hpp"
#include "pajlada/signals/scoped-connection.hpp"
#include "widgets/BaseWindow.hpp"

#include <QDateTime>
#include <QElapsedTimer>
#include <QPointer>
#include <QTimer>

#include <deque>
#include <memory>

class QLabel;
class QPushButton;
class QTextEdit;

namespace chatterino {

class TwitchChannel;

class ChannelDevToolsDialog : public BaseWindow
{
    Q_OBJECT

public:
    explicit ChannelDevToolsDialog(const ChannelPtr &channel,
                                   QWidget *parent = nullptr);
    ~ChannelDevToolsDialog() override;

    void attachChannel(const ChannelPtr &channel);

private:
    void setupUi();
    void onTickSecond();
    void onMessageReceived();
    QString buildDiagnosticsJson() const;

    ChannelPtr channel_;
    QTimer timer_;
    std::unique_ptr<pajlada::Signals::ScopedConnection> messageConnection_;

    // Telemetry tracking
    std::deque<qint64> messageTimestamps_;
    qint64 totalSessionMessages_ = 0;

    // UI Labels
    QLabel *channelNameLabel_{};
    QLabel *roomIdLabel_{};
    QLabel *userIdLabel_{};
    QLabel *roomModesLabel_{};
    QLabel *velocityLabel_{};
    QLabel *sessionMessagesLabel_{};
    QLabel *streamStatusLabel_{};
    QLabel *viewerCountLabel_{};
    QLabel *gameLabel_{};
    QLabel *streamTitleLabel_{};
    QLabel *emotesCountLabel_{};
};

}  // namespace chatterino
