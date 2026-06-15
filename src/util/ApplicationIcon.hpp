// SPDX-FileCopyrightText: 2026 Contributors to Chatterino Better Browser
//
// SPDX-License-Identifier: MIT

#pragma once

#include <QApplication>
#include <QCoreApplication>
#include <QIcon>
#include <QTimer>
#include <QWidget>

#ifdef USEWINSDK
#    include <Windows.h>
#endif

namespace chatterino {

inline QIcon applicationIcon()
{
    const auto tryIcon = [](const QString &path) -> QIcon {
        const QIcon icon(path);
        if (!icon.isNull() && !icon.availableSizes().isEmpty())
        {
            return icon;
        }
        return {};
    };

    if (auto icon = tryIcon(QStringLiteral(":/icon.ico")); !icon.isNull())
    {
        return icon;
    }

    if (auto icon = tryIcon(QStringLiteral(":/icon.png")); !icon.isNull())
    {
        return icon;
    }

#ifdef Q_OS_WIN
    if (auto icon = tryIcon(QCoreApplication::applicationFilePath());
        !icon.isNull())
    {
        return icon;
    }
#endif

    return QIcon(QStringLiteral(":/icon.ico"));
}

inline void applyApplicationIcon(QWidget *widget)
{
    if (widget == nullptr)
    {
        return;
    }

    const QIcon icon = applicationIcon();
    if (!icon.isNull())
    {
        widget->setWindowIcon(icon);
    }

#ifdef USEWINSDK
    const auto applyWin32Icons = [](HWND hwnd) {
        if (!hwnd)
        {
            return;
        }

        const HINSTANCE module = GetModuleHandleW(nullptr);
        const LPCWSTR iconId = MAKEINTRESOURCEW(1);

        auto loadSizedIcon = [&](int width, int height) -> HICON {
            HICON icon = static_cast<HICON>(LoadImageW(
                module, iconId, IMAGE_ICON, width, height, LR_DEFAULTCOLOR));
            if (icon == nullptr)
            {
                icon = LoadIconW(module, iconId);
            }
            return icon;
        };

        HICON bigIcon =
            loadSizedIcon(GetSystemMetrics(SM_CXICON),
                          GetSystemMetrics(SM_CYICON));
        HICON smallIcon =
            loadSizedIcon(GetSystemMetrics(SM_CXSMICON),
                          GetSystemMetrics(SM_CYSMICON));

        if (bigIcon != nullptr)
        {
            SendMessageW(hwnd, WM_SETICON, ICON_BIG,
                         reinterpret_cast<LPARAM>(bigIcon));
            SetClassLongPtrW(hwnd, GCLP_HICON,
                             reinterpret_cast<LONG_PTR>(bigIcon));
        }
        if (smallIcon != nullptr)
        {
            SendMessageW(hwnd, WM_SETICON, ICON_SMALL,
                         reinterpret_cast<LPARAM>(smallIcon));
            SetClassLongPtrW(hwnd, GCLP_HICONSM,
                             reinterpret_cast<LONG_PTR>(smallIcon));
        }
    };

    applyWin32Icons(HWND(widget->winId()));

    QTimer::singleShot(0, widget, [widget, applyWin32Icons] {
        applyWin32Icons(HWND(widget->winId()));
    });
    QTimer::singleShot(100, widget, [widget, applyWin32Icons] {
        applyWin32Icons(HWND(widget->winId()));
    });
#endif
}

}  // namespace chatterino
