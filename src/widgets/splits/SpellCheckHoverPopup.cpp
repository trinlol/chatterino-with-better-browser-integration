// SPDX-FileCopyrightText: 2026 Contributors to Chatterino <https://chatterino.com>
//
// SPDX-License-Identifier: MIT

#include "widgets/splits/SpellCheckHoverPopup.hpp"

#include "singletons/Theme.hpp"
#include "util/LayoutCreator.hpp"
#include "widgets/listview/GenericListView.hpp"
#include "widgets/splits/SpellCheckSuggestionItem.hpp"

#include <QLabel>
#include <QVBoxLayout>

namespace chatterino {

SpellCheckHoverPopup::SpellCheckHoverPopup(QWidget *parent)
    : BasePopup({BasePopup::EnableCustomFrame, BasePopup::Frameless,
                 BasePopup::DontFocus, BaseWindow::DisableLayoutSave},
                parent)
    , model_(this)
{
    this->initLayout();
    this->themeChangedEvent();
}

void SpellCheckHoverPopup::showSuggestions(
    const QPoint &globalPos, const QString &word,
    const std::vector<QString> &suggestions, int wordStart, int wordEnd)
{
    this->word_ = word;
    this->wordStart_ = wordStart;
    this->wordEnd_ = wordEnd;

    this->headerLabel_->setText(
        QStringLiteral("Replace \"%1\" with:").arg(word));

    this->model_.clear();
    this->model_.reserve(suggestions.size());

    for (const auto &sugg : suggestions)
    {
        this->model_.addItem(std::make_unique<SpellCheckSuggestionItem>(
            sugg, [this, sugg](const QString &suggestion) {
                this->suggestionSelected.invoke(suggestion);
            }));
    }

    if (this->model_.rowCount() > 0)
    {
        this->listView_->setCurrentIndex(this->model_.index(0));
    }

    this->listView_->doItemsLayout();
    this->adjustSize();
    this->showAndMoveTo(globalPos, widgets::BoundsChecking::CursorPosition);
}

void SpellCheckHoverPopup::hidePopup()
{
    this->hide();
    this->word_.clear();
    this->wordStart_ = -1;
    this->wordEnd_ = -1;
}

bool SpellCheckHoverPopup::eventFilter(QObject *watched, QEvent *event)
{
    return this->listView_->eventFilter(watched, event);
}

void SpellCheckHoverPopup::enterEvent(QEnterEvent *event)
{
    this->mouseEnteredPopup.invoke();
    BasePopup::enterEvent(event);
}

void SpellCheckHoverPopup::leaveEvent(QEvent *event)
{
    this->mouseLeftPopup.invoke();
    BasePopup::leaveEvent(event);
}

void SpellCheckHoverPopup::themeChangedEvent()
{
    BasePopup::themeChangedEvent();

    auto *theme = getTheme();
    QPalette headerPalette = this->headerLabel_->palette();
    headerPalette.setColor(QPalette::WindowText,
                           theme->messages.textColors.system);
    this->headerLabel_->setPalette(headerPalette);

    this->listView_->refreshTheme(*theme);
}

void SpellCheckHoverPopup::initLayout()
{
    LayoutCreator creator = {this};
    auto layoutCreator = creator.setLayoutType<QVBoxLayout>()
                               .withoutMargin()
                               .withoutSpacing();
    layoutCreator->setContentsMargins(4, 4, 4, 4);

    auto header = layoutCreator.emplace<QLabel>().assign(&this->headerLabel_);
    QFont headerFont = header->font();
    headerFont.setItalic(true);
    headerFont.setPointSize(headerFont.pointSize() - 1);
    header->setFont(headerFont);

    auto listView =
        layoutCreator.emplace<GenericListView>().assign(&this->listView_);
    listView->setModel(&this->model_);
    listView->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    listView->setMinimumWidth(120);

    QObject::connect(listView.getElement(), &GenericListView::closeRequested,
                     this, [this] {
                         this->hidePopup();
                     });
}

}  // namespace chatterino
