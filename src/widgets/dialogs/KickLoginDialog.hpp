// SPDX-FileCopyrightText: 2026 Contributors to Chatterino <https://chatterino.com>
//
// SPDX-License-Identifier: MIT

#pragma once

#include <QDialog>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QTcpServer>
#include <QVBoxLayout>

namespace chatterino {

class KickLoginDialog final : public QDialog
{
    Q_OBJECT

public:
    explicit KickLoginDialog(QWidget *parent = nullptr);
    ~KickLoginDialog() override;

private:
    void setupUI();
    void startOAuthLogin();
    void handleIncomingConnection();
    void stopLocalServer();
    void verifyAndSaveManual();

    QTcpServer *tcpServer_{nullptr};
    QString codeVerifier_;
    QString oauthState_;

    QPushButton *oauthLoginButton_{nullptr};
    QLabel *statusLabel_{nullptr};

    // Manual login controls
    QWidget *manualContainer_{nullptr};
    QLineEdit *usernameInput_{nullptr};
    QLineEdit *tokenInput_{nullptr};
    QPushButton *toggleTokenVisibility_{nullptr};
    QPushButton *saveButton_{nullptr};
    QPushButton *cancelButton_{nullptr};
};

}  // namespace chatterino
