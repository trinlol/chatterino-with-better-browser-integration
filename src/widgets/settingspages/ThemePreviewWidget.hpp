// SPDX-FileCopyrightText: 2026 Contributors to Chatterino <https://chatterino.com>
//
// SPDX-License-Identifier: MIT

#pragma once

#include <QFrame>

namespace chatterino {

class ThemeCustomizer;

class ThemePreviewWidget : public QFrame
{
    Q_OBJECT

public:
    explicit ThemePreviewWidget(ThemeCustomizer *customizer, QWidget *parent = nullptr);

    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    ThemeCustomizer *customizer_{};
};

}  // namespace chatterino
