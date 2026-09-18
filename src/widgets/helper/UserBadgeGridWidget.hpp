// SPDX-FileCopyrightText: 2026 Contributors to Chatterino <https://chatterino.com>
//
// SPDX-License-Identifier: MIT

#pragma once

#include "widgets/BaseWidget.hpp"

#include <QString>
#include <QVector>

namespace chatterino {

struct Emote;
using EmotePtr = std::shared_ptr<const Emote>;

struct UserBadgeDisplayEntry {
    EmotePtr emote;
    QString tooltip;
    int priority{100};
};


class FlowLayout;

class UserBadgeGridWidget : public BaseWidget
{
public:
    explicit UserBadgeGridWidget(QWidget *parent = nullptr);

    void setBadges(QVector<UserBadgeDisplayEntry> badges);
    void clearBadges();

protected:
    void scaleChangedEvent(float scale) override;

private:
    void rebuild();

    QVector<UserBadgeDisplayEntry> badges_;
    FlowLayout *badgeLayout_ = nullptr;
};


}  // namespace chatterino
