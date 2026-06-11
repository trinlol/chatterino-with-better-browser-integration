#pragma once

#include "widgets/BaseWidget.hpp"

#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>

#include <memory>

namespace chatterino {

class Channel;
using ChannelPtr = std::shared_ptr<Channel>;

/// Announcement-style banner shown directly above the chat input while a
/// Twitch prediction is running in the channel. Driven by
/// Channel::setPredictionState (fed from the browser extension via native
/// messaging).
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
    pajlada::Signals::ScopedConnection channelConnection_;
};

}  // namespace chatterino
