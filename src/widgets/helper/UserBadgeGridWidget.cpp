// SPDX-FileCopyrightText: 2026 Contributors to Chatterino <https://chatterino.com>
//
// SPDX-License-Identifier: MIT

#include "widgets/helper/UserBadgeGridWidget.hpp"

#include "messages/Emote.hpp"
#include "messages/Image.hpp"
#include "util/LayoutCreator.hpp"
#include "util/LoadPixmap.hpp"
#include "widgets/buttons/LabelButton.hpp"
#include "widgets/buttons/PixmapButton.hpp"
#include "widgets/layout/FlowLayout.hpp"
#include "widgets/Label.hpp"

#include <QVBoxLayout>

#include <algorithm>

namespace chatterino {

namespace {

constexpr int BADGE_SIZE = 18;

}  // namespace

UserBadgeGridWidget::UserBadgeGridWidget(QWidget *parent)
    : BaseWidget(parent)
{
    auto layout = LayoutCreator(this).setLayoutType<QVBoxLayout>().withoutMargin();

    this->headerLabel_ = layout.emplace<Label>("Badges").getElement();
    this->headerLabel_->setFontStyle(FontStyle::UiMediumBold);
    this->headerLabel_->setVisible(false);

    this->badgeContainer_ = layout.emplace<BaseWidget>().getElement();
    this->badgeLayout_ =
        LayoutCreator(this->badgeContainer_)
            .setLayoutType<FlowLayout>()
            .getElement();
    this->badgeLayout_->setHorizontalSpacing(4);
    this->badgeLayout_->setVerticalSpacing(4);

    this->toggleButton_ = layout.emplace<LabelButton>("").getElement();
    this->toggleButton_->setVisible(false);
    this->toggleButton_->setPadding({0, 0});

    QObject::connect(this->toggleButton_, &LabelButton::leftClicked, [this] {
        this->expanded_ = !this->expanded_;
        this->rebuild();
    });
}

void UserBadgeGridWidget::setBadges(QVector<UserBadgeDisplayEntry> badges)
{
    this->badges_ = std::move(badges);
    this->expanded_ = false;
    this->rebuild();
}

void UserBadgeGridWidget::clearBadges()
{
    this->badges_.clear();
    this->expanded_ = false;
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
    this->headerLabel_->setVisible(hasBadges);
    this->setVisible(hasBadges);

    if (!hasBadges)
    {
        this->toggleButton_->setVisible(false);
        return;
    }

    const int visibleCount = this->expanded_
                                 ? static_cast<int>(this->badges_.size())
                                 : std::min(static_cast<int>(this->badges_.size()),
                                            COLLAPSED_BADGE_LIMIT);

    for (int i = 0; i < visibleCount; ++i)
    {
        const auto &entry = this->badges_.at(i);
        if (!entry.emote)
        {
            continue;
        }

        auto *button =
            new PixmapButton(static_cast<BaseWidget *>(this->badgeContainer_));
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

    this->updateToggleVisibility();
}

void UserBadgeGridWidget::updateToggleVisibility()
{
    if (this->badges_.size() <= COLLAPSED_BADGE_LIMIT)
    {
        this->toggleButton_->setVisible(false);
        return;
    }

    this->toggleButton_->setVisible(true);
    if (this->expanded_)
    {
        this->toggleButton_->setText("Show less");
    }
    else
    {
        const int hiddenCount =
            static_cast<int>(this->badges_.size()) - COLLAPSED_BADGE_LIMIT;
        this->toggleButton_->setText(
            QString("Show %1 more").arg(hiddenCount));
    }
}

}  // namespace chatterino
