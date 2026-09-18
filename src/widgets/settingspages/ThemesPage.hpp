// SPDX-FileCopyrightText: 2026 Contributors to Chatterino <https://chatterino.com>
//
// SPDX-License-Identifier: MIT

#pragma once

#include "controllers/themes/ThemeCustomizer.hpp"
#include "widgets/settingspages/SettingsPage.hpp"

#include <QComboBox>
#include <QGroupBox>
#include <QLabel>
#include <QPushButton>
#include <QScrollArea>

#include <memory>
#include <vector>

namespace chatterino {

class ColorButton;
class ThemePreviewWidget;

class ThemesPage : public SettingsPage
{
    Q_OBJECT

public:
    ThemesPage();

    bool filterElements(const QString &query) override;
    void onShow() override;

private:
    void initUi();
    void refreshThemeList();
    void loadSelectedTheme();
    void buildTokenWidgets(QVBoxLayout *parentLayout);
    void updateTokenSwatches();

    std::unique_ptr<ThemeCustomizer> customizer_;

    // Top control bar
    QComboBox *themeCombo_{};
    QPushButton *applyButton_{};
    QPushButton *saveButton_{};
    QPushButton *saveAsButton_{};
    QPushButton *importButton_{};
    QPushButton *exportButton_{};
    QPushButton *deleteButton_{};
    QPushButton *resetButton_{};
    QLabel *statusLabel_{};

    // Main layout
    QScrollArea *scrollArea_{};
    QWidget *scrollContent_{};
    ThemePreviewWidget *previewWidget_{};

    // Color buttons and label rows mapped by token path
    struct TokenRow {
        QString path;
        QString title;
        QLabel *label{};
        ColorButton *button{};
        QWidget *container{};
    };
    std::vector<TokenRow> tokenRows_;
    std::vector<QGroupBox *> groupBoxes_;
};

}  // namespace chatterino
