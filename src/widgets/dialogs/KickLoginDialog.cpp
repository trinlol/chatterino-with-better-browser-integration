// SPDX-FileCopyrightText: 2026 Contributors to Chatterino <https://chatterino.com>
//
// SPDX-License-Identifier: MIT

#include "widgets/dialogs/KickLoginDialog.hpp"

#include "Application.hpp"
#include "providers/kick/KickManager.hpp"
#include "singletons/Settings.hpp"

#include <QCryptographicHash>
#include <QDesktopServices>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QHostAddress>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QRandomGenerator>
#include <QTcpSocket>
#include <QUrl>
#include <QUrlQuery>
#include <QVBoxLayout>

namespace chatterino {

KickLoginDialog::KickLoginDialog(QWidget *parent)
    : QDialog(parent)
{
    this->setWindowTitle(QStringLiteral("Log in to Kick"));
    this->setModal(true);
    this->resize(460, 360);

    this->setupUI();
}

KickLoginDialog::~KickLoginDialog()
{
    this->stopLocalServer();
}

void KickLoginDialog::setupUI()
{
    auto *mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(18, 18, 18, 18);
    mainLayout->setSpacing(14);

    auto *headerLabel = new QLabel(
        QStringLiteral("<b>Connect your Kick Account</b><br>"
                       "Sign in to Kick to chat and use channel moderation features directly in Chatterino."),
        this);
    headerLabel->setWordWrap(true);
    mainLayout->addWidget(headerLabel);

    // Primary OAuth login button
    this->oauthLoginButton_ =
        new QPushButton(QStringLiteral("Log in with Kick (Opens in browser)"), this);
    this->oauthLoginButton_->setFixedHeight(42);
    this->oauthLoginButton_->setStyleSheet(
        QStringLiteral("QPushButton {"
                       "  background-color: #53FC18; color: #000000;"
                       "  font-weight: bold; font-size: 14px;"
                       "  border-radius: 4px; padding: 6px 16px;"
                       "}"
                       "QPushButton:hover { background-color: #45D614; }"
                       "QPushButton:pressed { background-color: #38B010; }"
                       "QPushButton:disabled { background-color: #333; color: #777; }"));
    QObject::connect(this->oauthLoginButton_, &QPushButton::clicked, this,
                     &KickLoginDialog::startOAuthLogin);
    mainLayout->addWidget(this->oauthLoginButton_);

    this->statusLabel_ = new QLabel(this);
    this->statusLabel_->setWordWrap(true);
    this->statusLabel_->hide();
    mainLayout->addWidget(this->statusLabel_);

    // Collapsible manual token entry section
    auto *manualToggleBtn =
        new QPushButton(QStringLiteral("Manual / Developer Token Entry ▼"), this);
    manualToggleBtn->setFlat(true);
    manualToggleBtn->setStyleSheet(QStringLiteral("text-align: left; color: #888; font-size: 11px;"));
    mainLayout->addWidget(manualToggleBtn);

    this->manualContainer_ = new QWidget(this);
    auto *manualLayout = new QVBoxLayout(this->manualContainer_);
    manualLayout->setContentsMargins(0, 4, 0, 0);
    manualLayout->setSpacing(8);

    auto *form = new QFormLayout();
    form->setSpacing(8);

    this->usernameInput_ = new QLineEdit(this->manualContainer_);
    this->usernameInput_->setPlaceholderText(QStringLiteral("Kick username"));
    form->addRow(QStringLiteral("Username:"), this->usernameInput_);

    auto *tokenRow = new QHBoxLayout();
    this->tokenInput_ = new QLineEdit(this->manualContainer_);
    this->tokenInput_->setEchoMode(QLineEdit::Password);
    this->tokenInput_->setPlaceholderText(QStringLiteral("OAuth or Bearer token"));
    tokenRow->addWidget(this->tokenInput_);

    this->toggleTokenVisibility_ = new QPushButton(QStringLiteral("Show"), this->manualContainer_);
    this->toggleTokenVisibility_->setFixedWidth(50);
    QObject::connect(this->toggleTokenVisibility_, &QPushButton::clicked, [this]() {
        if (this->tokenInput_->echoMode() == QLineEdit::Password)
        {
            this->tokenInput_->setEchoMode(QLineEdit::Normal);
            this->toggleTokenVisibility_->setText(QStringLiteral("Hide"));
        }
        else
        {
            this->tokenInput_->setEchoMode(QLineEdit::Password);
            this->toggleTokenVisibility_->setText(QStringLiteral("Show"));
        }
    });
    tokenRow->addWidget(this->toggleTokenVisibility_);

    form->addRow(QStringLiteral("Token:"), tokenRow);
    manualLayout->addLayout(form);

    this->saveButton_ = new QPushButton(QStringLiteral("Verify & Save Manual Token"), this->manualContainer_);
    this->saveButton_->setStyleSheet(QStringLiteral("background-color: #333; color: #fff; padding: 5px 12px;"));
    QObject::connect(this->saveButton_, &QPushButton::clicked, this, &KickLoginDialog::verifyAndSaveManual);
    manualLayout->addWidget(this->saveButton_);

    this->manualContainer_->hide();
    QObject::connect(manualToggleBtn, &QPushButton::clicked, [this, manualToggleBtn]() {
        bool isHidden = this->manualContainer_->isHidden();
        this->manualContainer_->setVisible(isHidden);
        manualToggleBtn->setText(isHidden
                                     ? QStringLiteral("Manual / Developer Token Entry ▲")
                                     : QStringLiteral("Manual / Developer Token Entry ▼"));
        this->adjustSize();
    });

    mainLayout->addWidget(this->manualContainer_);
    mainLayout->addStretch(1);

    auto *buttonsLayout = new QHBoxLayout();
    buttonsLayout->addStretch(1);

    this->cancelButton_ = new QPushButton(QStringLiteral("Cancel"), this);
    QObject::connect(this->cancelButton_, &QPushButton::clicked, this, &QDialog::reject);
    buttonsLayout->addWidget(this->cancelButton_);

    mainLayout->addLayout(buttonsLayout);

    // Pre-fill existing credentials if available
    auto *kickMgr = getApp()->getKick();
    if (kickMgr && kickMgr->hasAccount())
    {
        this->usernameInput_->setText(kickMgr->getCurrentUsername());
        this->tokenInput_->setText(kickMgr->getAuthToken());
    }
}

void KickLoginDialog::startOAuthLogin()
{
    // Generate PKCE code verifier (RFC 7636)
    QByteArray randomBytes(32, Qt::Uninitialized);
    QRandomGenerator::system()->generate(
        reinterpret_cast<quint32 *>(randomBytes.data()),
        reinterpret_cast<quint32 *>(randomBytes.data() + randomBytes.size()));
    this->codeVerifier_ = randomBytes.toBase64(QByteArray::Base64UrlEncoding |
                                              QByteArray::OmitTrailingEquals);

    // Generate SHA-256 code challenge
    QByteArray hash = QCryptographicHash::hash(this->codeVerifier_.toLatin1(),
                                               QCryptographicHash::Sha256);
    QString codeChallenge = hash.toBase64(QByteArray::Base64UrlEncoding |
                                          QByteArray::OmitTrailingEquals);

    // Generate random state
    QByteArray stateBytes(16, Qt::Uninitialized);
    QRandomGenerator::system()->generate(
        reinterpret_cast<quint32 *>(stateBytes.data()),
        reinterpret_cast<quint32 *>(stateBytes.data() + stateBytes.size()));
    this->oauthState_ = stateBytes.toHex();

    // Start local loopback server on port 52153
    if (!this->tcpServer_)
    {
        this->tcpServer_ = new QTcpServer(this);
        QObject::connect(this->tcpServer_, &QTcpServer::newConnection, this,
                         &KickLoginDialog::handleIncomingConnection);
    }

    this->stopLocalServer();

    if (!this->tcpServer_->listen(QHostAddress::LocalHost, 52153))
    {
        this->statusLabel_->setText(
            QStringLiteral("<font color='#E91E63'>Failed to start local listener on port 52153: %1</font>")
                .arg(this->tcpServer_->errorString()));
        this->statusLabel_->show();
        return;
    }

    auto clientId = getSettings()->kickClientId.getValue().trimmed();
    QString redirectUri = QStringLiteral("http://localhost:52153/callback");
    QString scope = QStringLiteral("user:read chat:write channel:read moderation:ban moderation:chat_message:manage");

    QUrl authUrl(QStringLiteral("https://id.kick.com/oauth/authorize"));
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("client_id"), clientId);
    query.addQueryItem(QStringLiteral("response_type"), QStringLiteral("code"));
    query.addQueryItem(QStringLiteral("redirect_uri"), redirectUri);
    query.addQueryItem(QStringLiteral("scope"), scope);
    query.addQueryItem(QStringLiteral("code_challenge"), codeChallenge);
    query.addQueryItem(QStringLiteral("code_challenge_method"), QStringLiteral("S256"));
    query.addQueryItem(QStringLiteral("state"), this->oauthState_);
    authUrl.setQuery(query);

    this->oauthLoginButton_->setEnabled(false);
    this->statusLabel_->setText(
        QStringLiteral("<font color='#53FC18'>Opening browser...</font><br>"
                       "<small style='color: #AAA;'>Please approve the connection in your browser.</small>"));
    this->statusLabel_->show();

    if (!QDesktopServices::openUrl(authUrl))
    {
        this->statusLabel_->setText(
            QStringLiteral("<font color='#E91E63'>Could not open browser automatically. Please open this link:<br>"
                           "<a href='%1'>Authorize Chatterino on Kick</a></font>")
                .arg(authUrl.toString()));
        this->statusLabel_->show();
    }
}

void KickLoginDialog::handleIncomingConnection()
{
    if (!this->tcpServer_)
    {
        return;
    }

    auto *socket = this->tcpServer_->nextPendingConnection();
    if (!socket)
    {
        return;
    }

    QObject::connect(socket, &QTcpSocket::readyRead, [this, socket]() {
        QByteArray requestData = socket->readAll();
        QString request = QString::fromUtf8(requestData);

        int getIdx = request.indexOf("GET ");
        int httpIdx = request.indexOf(" HTTP/", getIdx);
        if (getIdx == -1 || httpIdx == -1)
        {
            return;
        }

        QString rawPath = request.mid(getIdx + 4, httpIdx - (getIdx + 4)).trimmed();
        QUrl parsedUrl(rawPath);
        QUrlQuery query(parsedUrl.query());

        QString code = query.queryItemValue(QStringLiteral("code"));
        QString state = query.queryItemValue(QStringLiteral("state"));
        QString error = query.queryItemValue(QStringLiteral("error"));

        bool ok = !code.isEmpty() && state == this->oauthState_;

        QByteArray body;
        if (ok)
        {
            body =
                "<!DOCTYPE html><html><head><meta charset='utf-8'><title>Kick Login - Chatterino</title></head>"
                "<body style='background:#18181b;color:#ffffff;font-family:Segoe UI,sans-serif;text-align:center;padding-top:60px;'>"
                "<h1 style='color:#53fc18;font-size:32px;margin-bottom:10px;'>Login Successful!</h1>"
                "<p style='color:#cccccc;font-size:16px;'>Chatterino has received your authorization. You can close this tab and return to Chatterino.</p>"
                "</body></html>";
        }
        else
        {
            QString reason = error.isEmpty() ? QStringLiteral("State mismatch or missing authorization code") : error;
            body =
                "<!DOCTYPE html><html><head><meta charset='utf-8'><title>Kick Login Error</title></head>"
                "<body style='background:#18181b;color:#ffffff;font-family:Segoe UI,sans-serif;text-align:center;padding-top:60px;'>"
                "<h1 style='color:#e91e63;font-size:32px;margin-bottom:10px;'>Authorization Failed</h1>"
                "<p style='color:#cccccc;font-size:16px;'>Could not authenticate with Kick: " +
                reason.toUtf8() +
                "</p></body></html>";
        }

        QByteArray response =
            "HTTP/1.1 200 OK\r\n"
            "Content-Type: text/html; charset=utf-8\r\n"
            "Content-Length: " + QByteArray::number(body.size()) + "\r\n"
            "Connection: close\r\n\r\n" + body;

        socket->write(response);
        socket->flush();
        socket->disconnectFromHost();

        this->stopLocalServer();

        if (!ok)
        {
            this->oauthLoginButton_->setEnabled(true);
            this->statusLabel_->setText(
                QStringLiteral("<font color='#E91E63'>Authorization cancelled or failed: %1</font>")
                    .arg(error.isEmpty() ? QStringLiteral("State mismatch") : error));
            this->statusLabel_->show();
            return;
        }

        this->statusLabel_->setText(
            QStringLiteral("<font color='#00BCD4'>Exchanging authorization code with Kick...</font>"));
        this->statusLabel_->show();

        getApp()->getKick()->exchangeOAuthCode(
            code, this->codeVerifier_,
            [this](bool success, QString exchangeError, QString accessToken, QString refreshToken) {
                if (!success)
                {
                    this->oauthLoginButton_->setEnabled(true);
                    this->statusLabel_->setText(
                        QStringLiteral("<font color='#E91E63'>%1</font>").arg(exchangeError));
                    this->statusLabel_->show();
                    return;
                }

                this->statusLabel_->setText(
                    QStringLiteral("<font color='#00BCD4'>Fetching Kick user profile...</font>"));
                this->statusLabel_->show();

                getApp()->getKick()->fetchCurrentUser(
                    accessToken,
                    [this, accessToken, refreshToken](bool fetchSuccess, QString fetchError,
                                                     QString username, QString userId, QString /*avatar*/) {
                        this->oauthLoginButton_->setEnabled(true);
                        if (!fetchSuccess)
                        {
                            this->statusLabel_->setText(
                                QStringLiteral("<font color='#E91E63'>%1</font>").arg(fetchError));
                            this->statusLabel_->show();
                            return;
                        }

                        getApp()->getKick()->setAccount(username, accessToken, userId, refreshToken);
                        this->statusLabel_->setText(
                            QStringLiteral("<font color='#53FC18'>Logged in as <b>%1</b>!</font>").arg(username));
                        this->statusLabel_->show();
                        this->accept();
                    });
            });
    });
}

void KickLoginDialog::stopLocalServer()
{
    if (this->tcpServer_ && this->tcpServer_->isListening())
    {
        this->tcpServer_->close();
    }
}

void KickLoginDialog::verifyAndSaveManual()
{
    QString username = this->usernameInput_->text().trimmed();
    QString token = this->tokenInput_->text().trimmed();

    if (token.isEmpty())
    {
        this->statusLabel_->setText(QStringLiteral("<font color='#E91E63'>Please enter a token.</font>"));
        this->statusLabel_->show();
        return;
    }

    this->saveButton_->setEnabled(false);
    this->saveButton_->setText(QStringLiteral("Verifying..."));
    this->statusLabel_->setText(QStringLiteral("<font color='#00BCD4'>Verifying account with Kick API...</font>"));
    this->statusLabel_->show();

    getApp()->getKick()->verifyAccount(
        username, token,
        [this, username, token](bool success, QString error, QString userId, QString /*avatarUrl*/) {
            this->saveButton_->setEnabled(true);
            this->saveButton_->setText(QStringLiteral("Verify & Save Manual Token"));

            if (success)
            {
                getApp()->getKick()->setAccount(username, token, userId);
                this->accept();
            }
            else
            {
                this->statusLabel_->setText(
                    QStringLiteral("<font color='#E91E63'>%1</font>").arg(error));
                this->statusLabel_->show();
            }
        });
}

}  // namespace chatterino
