// SPDX-FileCopyrightText: 2026 Contributors to Chatterino <https://chatterino.com>
//
// SPDX-License-Identifier: MIT

#include "widgets/helper/UserBadgeGridWidget.hpp"

#include "messages/Emote.hpp"
#include "messages/Image.hpp"
#include "util/LayoutCreator.hpp"
#include "util/LoadPixmap.hpp"
#include "widgets/buttons/PixmapButton.hpp"
#include "widgets/layout/FlowLayout.hpp"

#include <algorithm>

namespace chatterino {

namespace {

constexpr int BADGE_SIZE = 18;

}  // namespace

UserBadgeGridWidget::UserBadgeGridWidget(QWidget *parent)
    : BaseWidget(parent)
{
    auto layout = LayoutCreator(this).setLayoutType<FlowLayout>();
    this->badgeLayout_ = layout.getElement();
    this->badgeLayout_->setHorizontalSpacing(2);
    this->badgeLayout_->setVerticalSpacing(2);
    this->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Minimum);
}

void UserBadgeGridWidget::setBadges(QVector<UserBadgeDisplayEntry> badges)
{
    this->badges_ = std::move(badges);
    this->rebuild();
}

void UserBadgeGridWidget::clearBadges()
{
    this->badges_.clear();
    this->rebuild();
}

void UserBadgeGridWidget::scaleChangedEvent(float /*scale*/)
{
    this->rebuild();
}

void UserBadgeGridWidget::rebuild()
{
    while (QLayoutItem *item = this->badgeLayout_->takeAt(0))
    {
        if (item->widget())
        {
            item->widget()->deleteLater();
        }
        delete item;
    }

    const bool hasBadges = !this->badges_.isEmpty();
    this->setVisible(hasBadges);

    if (!hasBadges)
    {
        return;
    }

    const int visibleCount =
        std::min(static_cast<int>(this->badges_.size()), COLLAPSED_BADGE_LIMIT);

    for (int i = 0; i < visibleCount; ++i)
    {
        const auto &entry = this->badges_.at(i);
        if (!entry.emote)
        {
            continue;
        }

        auto *button = new PixmapButton(this);
        button->setScaleIndependentSize(BADGE_SIZE, BADGE_SIZE);
        button->setDim(DimButton::Dim::None);
        button->setMarginEnabled(false);
        button->setToolTip(entry.tooltip);

        const auto image = entry.emote->images.getImage2();
        if (auto pixmap = image->pixmapOrLoad())
        {
            button->setPixmap(*pixmap);
        }
        else
        {
            loadPixmapFromUrl(image->url(), [button](const QPixmap &pixmap) {
                if (button)
                {
                    button->setPixmap(pixmap);
                }
            });
        }

        this->badgeLayout_->addWidget(button);
    }
}

}  // namespace chatterino
