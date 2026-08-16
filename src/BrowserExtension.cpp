// SPDX-FileCopyrightText: 2018 Contributors to Chatterino <https://chatterino.com>
//
// SPDX-License-Identifier: MIT

#include "BrowserExtension.hpp"

#include "singletons/NativeMessaging.hpp"
#include "util/IpcQueue.hpp"
#include "util/RenameThread.hpp"

#include <QHash>
#include <QJsonDocument>
#include <QJsonObject>

#include <iostream>
#include <thread>

#ifdef Q_OS_WIN
#    include <fcntl.h>
#    include <io.h>

#    include <cstdio>

#    include <Windows.h>

#endif

namespace {

using namespace chatterino;

void initFileMode()
{
#ifdef Q_OS_WIN
    _setmode(_fileno(stdin), _O_BINARY);
    _setmode(_fileno(stdout), _O_BINARY);
#endif
}

// TODO(Qt6): Use QUtf8String
void sendToBrowser(QLatin1String str)
{
    auto len = static_cast<uint32_t>(str.size());
    std::cout.write(reinterpret_cast<const char *>(&len), sizeof(len));
    std::cout.write(str.data(), str.size());
    std::cout.flush();
}

QByteArray receiveFromBrowser()
{
    uint32_t size = 0;
    std::cin.read(reinterpret_cast<char *>(&size), sizeof(size));

    if (std::cin.eof())
    {
        return {};
    }

    QByteArray buffer{static_cast<QByteArray::size_type>(size),
                      Qt::Uninitialized};
    std::cin.read(buffer.data(), size);

    return buffer;
}

#ifdef Q_OS_WIN
bool isSupportedBrowserWindow(HWND window)
{
    DWORD processId = 0;
    ::GetWindowThreadProcessId(window, &processId);
    if (processId == 0)
    {
        return false;
    }

    auto process = ::OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, false,
                                 processId);
    if (process == nullptr)
    {
        return false;
    }

    wchar_t path[1024]{};
    DWORD pathLength = 1024;
    const bool havePath =
        ::QueryFullProcessImageNameW(process, 0, path, &pathLength) != 0;
    ::CloseHandle(process);
    if (!havePath)
    {
        return false;
    }

    const auto executable =
        QString::fromWCharArray(path, static_cast<qsizetype>(pathLength));
    return executable.endsWith("chrome.exe", Qt::CaseInsensitive) ||
           executable.endsWith("firefox.exe", Qt::CaseInsensitive) ||
           executable.endsWith("vivaldi.exe", Qt::CaseInsensitive) ||
           executable.endsWith("opera.exe", Qt::CaseInsensitive) ||
           executable.endsWith("msedge.exe", Qt::CaseInsensitive) ||
           executable.endsWith("brave.exe", Qt::CaseInsensitive);
}
#endif

QByteArray rememberBrowserWindow(
    QByteArray message, QHash<QString, quintptr> &browserWindows,
    quintptr startupBrowserWindow)
{
#ifdef Q_OS_WIN
    auto document = QJsonDocument::fromJson(message);
    if (!document.isObject())
    {
        return message;
    }

    auto root = document.object();
    if (root.value("action").toString() != QStringLiteral("select"))
    {
        return message;
    }

    const auto winId = root.value("winId").toString();
    if (winId.isEmpty())
    {
        return message;
    }

    // The extension only sets this after Edge confirms that its window owns
    // OS focus. Preserve that HWND so retries remain targeted after launching
    // Chatterino moves focus away from the browser.
    if (root.value("browserWindowFocused").toBool())
    {
        if (auto foreground = ::GetForegroundWindow();
            foreground != nullptr && isSupportedBrowserWindow(foreground))
        {
            browserWindows.insert(winId,
                                  reinterpret_cast<quintptr>(foreground));
        }
    }

    auto target = browserWindows.value(winId, 0);

    // Edge can launch the native host before the Twitch content script has
    // produced its first geometry measurement. In that case there is no
    // focused select message from which to learn the HWND before Chatterino
    // takes focus. The host itself was launched while Edge owned focus, so use
    // that one validated startup target for the first replay only.
    if (target == 0 && root.value("startupReplay").toBool() &&
        startupBrowserWindow != 0 &&
        ::IsWindow(reinterpret_cast<HWND>(startupBrowserWindow)))
    {
        target = startupBrowserWindow;
        browserWindows.insert(winId, target);
    }

    if (target == 0 || !::IsWindow(reinterpret_cast<HWND>(target)))
    {
        browserWindows.remove(winId);
        return message;
    }

    root.insert("browserHwnd", QString::number(target));
    return QJsonDocument(root).toJson(QJsonDocument::Compact);
#else
    (void)browserWindows;
    return message;
#endif
}

void runBrowserOutboundLoop()
{
    auto [messageQueue, error] =
        ipc::IpcQueue::tryReplaceOrCreate("chatterino_browser", 100, 1024);

    if (!error.isEmpty() || !messageQueue)
    {
        return;
    }

    // The desktop process can start before this outbound queue exists. Tell
    // the extension when the host is ready so it can replay the active chat
    // geometry after that startup ordering.
    sendToBrowser(QLatin1String{
        R"({"type":"status","status":"native-host-ready"})"});

    while (true)
    {
        auto buf = messageQueue->receive();
        if (buf.isEmpty())
        {
            continue;
        }
        sendToBrowser(QLatin1String(buf.constData(), buf.size()));
    }
}

void runLoop()
{
    QHash<QString, quintptr> browserWindows;
#ifdef Q_OS_WIN
    quintptr startupBrowserWindow = 0;
    if (auto foreground = ::GetForegroundWindow();
        foreground != nullptr && isSupportedBrowserWindow(foreground))
    {
        startupBrowserWindow = reinterpret_cast<quintptr>(foreground);
    }
#endif
    auto outboundThread = std::thread([] {
        runBrowserOutboundLoop();
    });
    renameThread(outboundThread, "BrowserOutbound");

    while (true)
    {
        auto buffer = receiveFromBrowser();
        if (buffer.isNull())
        {
            break;
        }

        nm::client::sendMessage(rememberBrowserWindow(
            std::move(buffer), browserWindows,
#ifdef Q_OS_WIN
            startupBrowserWindow
#else
            0
#endif
            ));
    }

    sendToBrowser(QLatin1String{
        R"({"type":"status","status":"exiting-host","reason":"received EOF"})"});
    _Exit(0);
}
}  // namespace

namespace chatterino {

void runBrowserExtensionHost()
{
    initFileMode();

    runLoop();
}

}  // namespace chatterino
