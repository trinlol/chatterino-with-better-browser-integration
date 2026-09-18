// SPDX-FileCopyrightText: 2018 Contributors to Chatterino <https://chatterino.com>
//
// SPDX-License-Identifier: MIT

#include "widgets/settingspages/AccountsPage.hpp"

#include "Application.hpp"
#include "controllers/accounts/AccountController.hpp"
#include "controllers/accounts/AccountModel.hpp"
#include "providers/kick/KickManager.hpp"
#include "providers/twitch/TwitchCommon.hpp"
#include "util/LayoutCreator.hpp"
#include "widgets/dialogs/KickLoginDialog.hpp"
#include "widgets/dialogs/LoginDialog.hpp"
#include "widgets/helper/EditableModelView.hpp"

#include <QDialogButtonBox>
#include <QGroupBox>
#include <QHeaderView>
#include <QTableView>
#include <QVBoxLayout>


#include <algorithm>

namespace chatterino {

AccountsPage::AccountsPage()
{
    auto *app = getApp();

    LayoutCreator<AccountsPage> layoutCreator(this);
    auto layout = layoutCreator.emplace<QVBoxLayout>().withoutMargin();

    EditableModelView *view =
        layout
            .emplace<EditableModelView>(
                app->getAccounts()->createModel(nullptr), false)
            .getElement();

    view->getTableView()->horizontalHeader()->setVisible(false);
    view->getTableView()->horizontalHeader()->setStretchLastSection(true);

    // We can safely ignore this signal connection since we own the view
    std::ignore = view->addButtonPressed.connect([this] {
        LoginDialog d(this);
        d.exec();
    });

    view->getTableView()->setStyleSheet("background: #333");

    // Kick Account Section
    auto *kickGroup = new QGroupBox(QStringLiteral("Kick Account"), this);
    auto *kickLayout = new QVBoxLayout(kickGroup);
    kickLayout->setSpacing(8);

    auto *kickInfoLabel = new QLabel(kickGroup);
    kickInfoLabel->setWordWrap(true);
    kickLayout->addWidget(kickInfoLabel);

    auto *kickButtonLayout = new QHBoxLayout();
    auto *kickLoginButton = new QPushButton(QStringLiteral("Log in to Kick"), kickGroup);
    auto *kickLogoutButton = new QPushButton(QStringLiteral("Log out"), kickGroup);
    kickButtonLayout->addWidget(kickLoginButton);
    kickButtonLayout->addWidget(kickLogoutButton);
    kickButtonLayout->addStretch(1);
    kickLayout->addLayout(kickButtonLayout);

    layout->addWidget(kickGroup);

    const auto updateKickUi = [kickInfoLabel, kickLoginButton, kickLogoutButton]() {
        auto *kickMgr = getApp()->getKick();
        if (kickMgr && kickMgr->hasAccount())
        {
            kickInfoLabel->setText(
                QStringLiteral("Logged in as <b><font color='#53FC18'>%1</font></b>%2")
                    .arg(kickMgr->getCurrentUsername())
                    .arg(kickMgr->getUserId().isEmpty()
                             ? QString()
                             : QStringLiteral(" (User ID: %1)").arg(kickMgr->getUserId())));
            kickLoginButton->setText(QStringLiteral("Change Kick Account"));
            kickLogoutButton->setEnabled(true);
        }
        else
        {
            kickInfoLabel->setText(
                QStringLiteral("Not logged into Kick. Log in to send chat messages and moderate in Kick chat."));
            kickLoginButton->setText(QStringLiteral("Log in to Kick"));
            kickLogoutButton->setEnabled(false);
        }
    };

    updateKickUi();

    auto *kickMgr = getApp()->getKick();
    if (kickMgr)
    {
        QObject::connect(kickMgr, &KickManager::accountChanged, this, updateKickUi);
    }

    QObject::connect(kickLoginButton, &QPushButton::clicked, [this]() {
        KickLoginDialog dialog(this);
        dialog.exec();
    });

    QObject::connect(kickLogoutButton, &QPushButton::clicked, []() {
        if (auto *mgr = getApp()->getKick())
        {
            mgr->removeAccount();
        }
    });
}

}  // namespace chatterino

