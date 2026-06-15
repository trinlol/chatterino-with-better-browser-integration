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

class FlowLayout;
class Label;
class LabelButton;

struct UserBadgeDisplayEntry {
    EmotePtr emote;
    QString tooltip;
};

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
    void updateToggleVisibility();

    static constexpr int COLLAPSED_BADGE_LIMIT = 12;

    QVector<UserBadgeDisplayEntry> badges_;
    bool expanded_ = false;

    QWidget *badgeContainer_ = nullptr;
    FlowLayout *badgeLayout_ = nullptr;
    Label *headerLabel_ = nullptr;
    LabelButton *toggleButton_ = nullptr;
};

}  // namespace chatterino
