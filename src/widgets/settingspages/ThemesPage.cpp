// SPDX-FileCopyrightText: 2026 Contributors to Chatterino <https://chatterino.com>
//
// SPDX-License-Identifier: MIT

#include "widgets/settingspages/ThemesPage.hpp"

#include "Application.hpp"
#include "singletons/Theme.hpp"
#include "widgets/dialogs/ColorPickerDialog.hpp"
#include "widgets/helper/color/ColorButton.hpp"
#include "widgets/settingspages/ThemePreviewWidget.hpp"

#include <QFileDialog>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QScrollArea>
#include <QVBoxLayout>

namespace chatterino {

ThemesPage::ThemesPage()
    : customizer_(std::make_unique<ThemeCustomizer>())
{
    this->initUi();

    this->managedConnections_.managedConnect(
        getTheme()->availableThemesChanged, [this]() {
            this->refreshThemeList();
        });

    this->refreshThemeList();
}

void ThemesPage::onShow()
{
    getTheme()->reloadAvailableThemes();
}

void ThemesPage::initUi()
{
    auto *pageLayout = new QVBoxLayout(this);
    pageLayout->setContentsMargins(16, 16, 16, 16);
    pageLayout->setSpacing(10);

    // --- Top Control Bar ---
    auto *topBarLayout = new QHBoxLayout;
    topBarLayout->setSpacing(6);

    auto *themeLabel = new QLabel("Theme:", this);
    themeLabel->setStyleSheet("font-weight: bold;");
    topBarLayout->addWidget(themeLabel);

    this->themeCombo_ = new QComboBox(this);
    this->themeCombo_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    topBarLayout->addWidget(this->themeCombo_, 1);

    this->applyButton_ = new QPushButton("Apply", this);
    this->applyButton_->setToolTip("Apply this theme to Chatterino");
    topBarLayout->addWidget(this->applyButton_);

    this->saveButton_ = new QPushButton("Save", this);
    this->saveButton_->setToolTip("Save modifications to current custom theme");
    topBarLayout->addWidget(this->saveButton_);

    this->saveAsButton_ = new QPushButton("Save As...", this);
    this->saveAsButton_->setToolTip("Save as a new custom theme");
    topBarLayout->addWidget(this->saveAsButton_);

    this->importButton_ = new QPushButton("Import...", this);
    this->importButton_->setToolTip("Import a theme JSON file");
    topBarLayout->addWidget(this->importButton_);

    this->exportButton_ = new QPushButton("Export...", this);
    this->exportButton_->setToolTip("Export current theme to a JSON file");
    topBarLayout->addWidget(this->exportButton_);

    this->deleteButton_ = new QPushButton("Delete", this);
    this->deleteButton_->setToolTip("Delete this custom theme");
    topBarLayout->addWidget(this->deleteButton_);

    this->resetButton_ = new QPushButton("Revert", this);
    this->resetButton_->setToolTip("Revert unsaved edits");
    topBarLayout->addWidget(this->resetButton_);

    pageLayout->addLayout(topBarLayout);

    // --- Main Content Area: Horizontal Layout ---
    auto *mainSplitLayout = new QHBoxLayout;
    mainSplitLayout->setSpacing(16);

    // Left: Scroll Area for Token Pickers
    this->scrollArea_ = new QScrollArea(this);
    this->scrollArea_->setWidgetResizable(true);
    this->scrollArea_->setFrameShape(QFrame::NoFrame);

    this->scrollContent_ = new QWidget(this->scrollArea_);
    auto *scrollLayout = new QVBoxLayout(this->scrollContent_);
    scrollLayout->setContentsMargins(0, 0, 8, 0);
    scrollLayout->setSpacing(10);

    this->buildTokenWidgets(scrollLayout);
    scrollLayout->addStretch(1);

    this->scrollArea_->setWidget(this->scrollContent_);
    mainSplitLayout->addWidget(this->scrollArea_, 3);

    // Right: Live Preview Panel
    auto *previewContainer = new QWidget(this);
    auto *previewLayout = new QVBoxLayout(previewContainer);
    previewLayout->setContentsMargins(0, 0, 0, 0);
    previewLayout->setSpacing(6);

    auto *previewHeader = new QLabel("Live Preview", previewContainer);
    previewHeader->setStyleSheet("font-weight: bold; font-size: 13px;");
    previewLayout->addWidget(previewHeader);

    this->previewWidget_ =
        new ThemePreviewWidget(this->customizer_.get(), previewContainer);
    this->previewWidget_->setMinimumWidth(320);
    this->previewWidget_->setMinimumHeight(440);
    this->previewWidget_->setSizePolicy(QSizePolicy::Expanding,
                                        QSizePolicy::Expanding);
    previewLayout->addWidget(this->previewWidget_, 1);

    mainSplitLayout->addWidget(previewContainer, 2);

    pageLayout->addLayout(mainSplitLayout, 1);

    // Status Label
    this->statusLabel_ = new QLabel(this);
    this->statusLabel_->setStyleSheet("color: #888; font-style: italic;");
    pageLayout->addWidget(this->statusLabel_);

    // --- Connections ---
    QObject::connect(this->themeCombo_,
                     QOverload<int>::of(&QComboBox::currentIndexChanged), this,
                     [this](int) {
                         this->loadSelectedTheme();
                     });

    QObject::connect(this->applyButton_, &QPushButton::clicked, this, [this]() {
        QString currentKey = this->themeCombo_->currentData().toString();
        if (!currentKey.isEmpty())
        {
            getTheme()->themeName.setValue(currentKey);
            this->statusLabel_->setText("Applied theme: " + currentKey);
        }
    });

    QObject::connect(this->saveButton_, &QPushButton::clicked, this, [this]() {
        if (this->customizer_->isBuiltIn())
        {
            return;
        }
        QString error;
        QString name = this->customizer_->currentDescriptor().name;
        if (this->customizer_->saveAsCustom(name, &error))
        {
            this->statusLabel_->setText("Saved changes to theme: " + name);
        }
        else
        {
            QMessageBox::warning(this, "Save Theme Error", error);
        }
    });

    QObject::connect(this->saveAsButton_, &QPushButton::clicked, this, [this]() {
        bool ok = false;
        QString currentName = this->customizer_->currentDescriptor().name;
        QString defaultName = this->customizer_->isBuiltIn()
                                  ? (currentName + " Custom")
                                  : currentName;
        QString name = QInputDialog::getText(
            this, "Save Custom Theme", "Enter a name for your custom theme:",
            QLineEdit::Normal, defaultName, &ok);

        if (ok && !name.trimmed().isEmpty())
        {
            QString error;
            if (this->customizer_->saveAsCustom(name.trimmed(), &error))
            {
                this->refreshThemeList();
                int idx = this->themeCombo_->findData(name.trimmed());
                if (idx >= 0)
                {
                    this->themeCombo_->setCurrentIndex(idx);
                }
                this->statusLabel_->setText("Saved custom theme: " +
                                            name.trimmed());
            }
            else
            {
                QMessageBox::warning(this, "Save Theme Error", error);
            }
        }
    });

    QObject::connect(this->importButton_, &QPushButton::clicked, this, [this]() {
        QString path = QFileDialog::getOpenFileName(
            this, "Import Theme", QString(), "Chatterino Theme (*.json)");
        if (!path.isEmpty())
        {
            QString error;
            if (this->customizer_->importFromFile(path, &error))
            {
                this->refreshThemeList();
                int idx = this->themeCombo_->findData(
                    this->customizer_->currentDescriptor().key);
                if (idx >= 0)
                {
                    this->themeCombo_->setCurrentIndex(idx);
                }
                this->statusLabel_->setText("Imported theme successfully.");
            }
            else
            {
                QMessageBox::warning(this, "Import Theme Error", error);
            }
        }
    });

    QObject::connect(this->exportButton_, &QPushButton::clicked, this, [this]() {
        QString defaultFileName =
            this->customizer_->currentDescriptor().name + ".json";
        QString path = QFileDialog::getSaveFileName(
            this, "Export Theme", defaultFileName,
            "Chatterino Theme (*.json)");
        if (!path.isEmpty())
        {
            QString error;
            if (this->customizer_->exportToFile(path, &error))
            {
                this->statusLabel_->setText("Exported theme to: " + path);
            }
            else
            {
                QMessageBox::warning(this, "Export Theme Error", error);
            }
        }
    });

    QObject::connect(this->deleteButton_, &QPushButton::clicked, this, [this]() {
        if (this->customizer_->isBuiltIn())
        {
            return;
        }

        auto ret = QMessageBox::question(
            this, "Delete Custom Theme",
            QString("Are you sure you want to delete the theme \"%1\"?")
                .arg(this->customizer_->currentDescriptor().name),
            QMessageBox::Yes | QMessageBox::No);

        if (ret == QMessageBox::Yes)
        {
            QString error;
            if (this->customizer_->deleteCustomTheme(&error))
            {
                this->refreshThemeList();
                this->statusLabel_->setText("Theme deleted.");
            }
            else
            {
                QMessageBox::warning(this, "Delete Theme Error", error);
            }
        }
    });

    QObject::connect(this->resetButton_, &QPushButton::clicked, this, [this]() {
        this->loadSelectedTheme();
        this->statusLabel_->setText("Reverted edits to theme.");
    });
}

void ThemesPage::buildTokenWidgets(QVBoxLayout *parentLayout)
{
    const auto &groups = ThemeCustomizer::tokenGroups();
    for (const auto &group : groups)
    {
        auto *groupBox = new QGroupBox(group.name, this->scrollContent_);
        auto *groupLayout = new QGridLayout(groupBox);
        groupLayout->setColumnStretch(0, 1);
        groupLayout->setColumnStretch(1, 0);
        groupLayout->setContentsMargins(8, 8, 8, 8);
        groupLayout->setSpacing(6);

        int rowIdx = 0;
        for (const auto &token : group.tokens)
        {
            auto *lbl = new QLabel(token.title, groupBox);
            lbl->setToolTip(token.path);

            auto *btn = new ColorButton(QColor(128, 128, 128), groupBox);
            btn->setFixedSize(38, 22);

            groupLayout->addWidget(lbl, rowIdx, 0);
            groupLayout->addWidget(btn, rowIdx, 1);

            TokenRow tr;
            tr.path = token.path;
            tr.title = token.title;
            tr.label = lbl;
            tr.button = btn;
            tr.container = groupBox;
            this->tokenRows_.push_back(tr);

            QObject::connect(
                btn, &ColorButton::clicked, this,
                [this, path = token.path, btn]() {
                    QColor initial = this->customizer_->getColor(path);
                    auto *dialog = new ColorPickerDialog(initial, this);
                    auto confirmed = std::make_shared<bool>(false);

                    QObject::connect(dialog, &ColorPickerDialog::colorChanged,
                                     this, [this, path, btn](const QColor &c) {
                                         this->customizer_->setColor(path, c);
                                         btn->setColor(c);
                                     });

                    QObject::connect(
                        dialog, &ColorPickerDialog::colorConfirmed, this,
                        [this, path, btn, confirmed](const QColor &c) {
                            *confirmed = true;
                            this->customizer_->setColor(path, c);
                            btn->setColor(c);
                            if (this->customizer_->isBuiltIn())
                            {
                                this->statusLabel_->setText(
                                    "Modified preview. Click "
                                    "'Save As...' to save as a "
                                    "custom theme.");
                            }
                        });

                    QObject::connect(
                        dialog, &QObject::destroyed, this,
                        [this, path, btn, initial, confirmed]() {
                            if (!*confirmed)
                            {
                                this->customizer_->setColor(path, initial);
                                btn->setColor(initial);
                            }
                        });

                    dialog->show();
                });

            rowIdx++;
        }

        this->groupBoxes_.push_back(groupBox);
        parentLayout->addWidget(groupBox);
    }
}

void ThemesPage::refreshThemeList()
{
    QSignalBlocker blocker(this->themeCombo_);
    this->themeCombo_->clear();

    const auto &descriptors = getTheme()->availableThemeDescriptors();
    QString activeTheme = getTheme()->themeName.getValue();

    int selectedIdx = 0;
    for (size_t i = 0; i < descriptors.size(); ++i)
    {
        const auto &desc = descriptors[i];
        QString label = desc.name + (desc.custom ? " (Custom)" : " (Built-in)");
        this->themeCombo_->addItem(label, desc.key);

        if (desc.key.compare(activeTheme, Qt::CaseInsensitive) == 0 ||
            desc.name.compare(activeTheme, Qt::CaseInsensitive) == 0)
        {
            selectedIdx = static_cast<int>(i);
        }
    }

    if (this->themeCombo_->count() > 0)
    {
        this->themeCombo_->setCurrentIndex(selectedIdx);
    }

    this->loadSelectedTheme();
}

void ThemesPage::loadSelectedTheme()
{
    QString key = this->themeCombo_->currentData().toString();
    if (key.isEmpty())
    {
        return;
    }

    const auto &descriptors = getTheme()->availableThemeDescriptors();
    auto it = std::find_if(descriptors.begin(), descriptors.end(),
                           [&key](const ThemeDescriptor &d) {
                               return d.key == key;
                           });

    if (it != descriptors.end())
    {
        QString error;
        if (this->customizer_->load(*it, &error))
        {
            this->updateTokenSwatches();
            this->saveButton_->setEnabled(it->custom);
            this->deleteButton_->setEnabled(it->custom);
            this->statusLabel_->setText(it->custom
                                            ? "Custom theme loaded."
                                            : "Built-in preset loaded.");
        }
        else
        {
            this->statusLabel_->setText("Error loading theme: " + error);
        }
    }
}

void ThemesPage::updateTokenSwatches()
{
    for (auto &row : this->tokenRows_)
    {
        QColor c = this->customizer_->getColor(row.path);
        row.button->setColor(c);
    }
}

bool ThemesPage::filterElements(const QString &query)
{
    if (query.trimmed().isEmpty())
    {
        for (auto *box : this->groupBoxes_)
        {
            box->setVisible(true);
        }
        for (auto &row : this->tokenRows_)
        {
            row.label->setVisible(true);
            row.button->setVisible(true);
        }
        return true;
    }

    QString trimmed = query.trimmed();
    bool anyMatched = false;

    if (QString("themes color palette preset customizer")
            .contains(trimmed, Qt::CaseInsensitive))
    {
        for (auto *box : this->groupBoxes_)
        {
            box->setVisible(true);
        }
        for (auto &row : this->tokenRows_)
        {
            row.label->setVisible(true);
            row.button->setVisible(true);
        }
        return true;
    }

    for (auto *box : this->groupBoxes_)
    {
        bool boxMatched = box->title().contains(trimmed, Qt::CaseInsensitive);
        bool anyRowVisibleInBox = false;

        for (auto &row : this->tokenRows_)
        {
            if (row.container == box)
            {
                bool rowMatched =
                    boxMatched ||
                    row.title.contains(trimmed, Qt::CaseInsensitive) ||
                    row.path.contains(trimmed, Qt::CaseInsensitive);
                row.label->setVisible(rowMatched);
                row.button->setVisible(rowMatched);
                if (rowMatched)
                {
                    anyRowVisibleInBox = true;
                }
            }
        }

        box->setVisible(anyRowVisibleInBox);
        if (anyRowVisibleInBox)
        {
            anyMatched = true;
        }
    }

    return anyMatched;
}

}  // namespace chatterino
