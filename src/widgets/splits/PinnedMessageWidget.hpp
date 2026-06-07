#pragma once

#include "widgets/BaseWidget.hpp"

#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>

#include <memory>

namespace chatterino {

class Channel;
using ChannelPtr = std::shared_ptr<Channel>;

class PinnedMessageWidget : public BaseWidget
{
    Q_OBJECT

public:
    explicit PinnedMessageWidget(QWidget *parent = nullptr);

    void setChannel(const ChannelPtr &channel);

protected:
    void paintEvent(QPaintEvent *event) override;
    void themeChangedEvent() override;

private:
    void updateState();

    ChannelPtr channel_;
    QLabel *textLabel_ = nullptr;
    QPushButton *closeButton_ = nullptr;
    pajlada::Signals::ScopedConnection channelConnection_;
};

}  // namespace chatterino
