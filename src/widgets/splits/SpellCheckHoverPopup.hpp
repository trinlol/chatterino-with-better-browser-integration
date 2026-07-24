// SPDX-FileCopyrightText: 2026 Contributors to Chatterino <https://chatterino.com>
//
// SPDX-License-Identifier: MIT

#pragma once

#include "widgets/BasePopup.hpp"
#include "widgets/listview/GenericListModel.hpp"

#include <pajlada/signals/signal.hpp>
#include <QString>

#include <vector>

class QLabel;

namespace chatterino {

class GenericListView;

class SpellCheckHoverPopup : public BasePopup
{
    Q_OBJECT

public:
    explicit SpellCheckHoverPopup(QWidget *parent = nullptr);
    ~SpellCheckHoverPopup() override = default;

    void showSuggestions(const QPoint &globalPos, const QString &word,
                         const std::vector<QString> &suggestions, int wordStart,
                         int wordEnd);
    void hidePopup();

    /// Forward key events from the text input to the suggestions list without
    /// taking focus away from the input box.
    bool eventFilter(QObject *watched, QEvent *event) override;

    pajlada::Signals::Signal<const QString &> suggestionSelected;
    pajlada::Signals::NoArgSignal mouseEnteredPopup;
    pajlada::Signals::NoArgSignal mouseLeftPopup;

    int wordStart() const
    {
        return this->wordStart_;
    }
    int wordEnd() const
    {
        return this->wordEnd_;
    }
    QString word() const
    {
        return this->word_;
    }

protected:
    void enterEvent(QEnterEvent *event) override;
    void leaveEvent(QEvent *event) override;
    void themeChangedEvent() override;

private:
    void initLayout();

    GenericListView *listView_ = nullptr;
    QLabel *headerLabel_ = nullptr;
    GenericListModel model_;
    QString word_;
    int wordStart_ = -1;
    int wordEnd_ = -1;
};

}  // namespace chatterino
