// SPDX-FileCopyrightText: 2026 Contributors to Chatterino Better Browser
//
// SPDX-License-Identifier: MIT

#pragma once

#include <QApplication>
#include <QCoreApplication>
#include <QIcon>
#include <QWidget>

#ifdef USEWINSDK
#    include <Windows.h>
#endif

namespace chatterino {

inline QIcon applicationIcon()
{
#ifdef Q_OS_WIN
    const QIcon exeIcon(QCoreApplication::applicationFilePath());
    if (!exeIcon.isNull())
    {
        return exeIcon;
    }
#endif

    return QIcon(":/icon.ico");
}

inline void applyApplicationIcon(QWidget *widget)
{
    if (widget == nullptr)
    {
        return;
    }

    const QIcon icon = QApplication::windowIcon();
    if (icon.isNull())
    {
        return;
    }

    widget->setWindowIcon(icon);

#ifdef USEWINSDK
    const auto hwnd = HWND(widget->winId());
    if (!hwnd)
    {
        return;
    }

    const auto exePath = QCoreApplication::applicationFilePath();
    const auto *path = reinterpret_cast<LPCWSTR>(exePath.utf16());

    HICON bigIcon = static_cast<HICON>(LoadImageW(
        nullptr, path, IMAGE_ICON, GetSystemMetrics(SM_CXICON),
        GetSystemMetrics(SM_CYICON), LR_LOADFROMFILE));
    HICON smallIcon = static_cast<HICON>(LoadImageW(
        nullptr, path, IMAGE_ICON, GetSystemMetrics(SM_CXSMICON),
        GetSystemMetrics(SM_CYSMICON), LR_LOADFROMFILE));

    if (bigIcon != nullptr)
    {
        SendMessageW(hwnd, WM_SETICON, ICON_BIG,
                     reinterpret_cast<LPARAM>(bigIcon));
    }
    if (smallIcon != nullptr)
    {
        SendMessageW(hwnd, WM_SETICON, ICON_SMALL,
                     reinterpret_cast<LPARAM>(smallIcon));
    }
#endif
}

}  // namespace chatterino
