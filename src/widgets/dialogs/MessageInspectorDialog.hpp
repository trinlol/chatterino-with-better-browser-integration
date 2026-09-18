// SPDX-FileCopyrightText: 2026 Contributors to Chatterino <https://chatterino.com>
//
// SPDX-License-Identifier: MIT

#pragma once

#include "messages/Message.hpp"
#include "widgets/BaseWindow.hpp"

#include <memory>

class QLabel;
class QPushButton;
class QTextEdit;

namespace chatterino {

class MessageInspectorDialog : public BaseWindow
{
    Q_OBJECT

public:
    explicit MessageInspectorDialog(MessagePtr message, QWidget *parent = nullptr);

private:
    void setupUi();
    QString buildJsonRepresentation() const;

    MessagePtr message_;
};

}  // namespace chatterino
