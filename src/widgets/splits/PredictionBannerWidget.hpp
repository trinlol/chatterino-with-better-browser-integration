#pragma once

#include "widgets/BaseWidget.hpp"

#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QTimer>

#include <memory>

namespace chatterino {

class Channel;
using ChannelPtr = std::shared_ptr<Channel>;

/// Announcement-style banner shown directly above the chat input for the
/// channel's active Twitch poll and prediction Activities.
class PredictionBannerWidget : public BaseWidget
{
    Q_OBJECT

public:
    explicit PredictionBannerWidget(QWidget *parent = nullptr);

    void setChannel(const ChannelPtr &channel);

protected:
    void paintEvent(QPaintEvent *event) override;
    void themeChangedEvent() override;

private:
    void updateState();
    QColor backgroundColor() const;
    QColor borderColor() const;

    ChannelPtr channel_;
    QLabel *iconLabel_ = nullptr;
    QLabel *textLabel_ = nullptr;
    QPushButton *closeButton_ = nullptr;
    QTimer refreshTimer_;
    pajlada::Signals::ScopedConnection channelConnection_;
};

}  // namespace chatterino
