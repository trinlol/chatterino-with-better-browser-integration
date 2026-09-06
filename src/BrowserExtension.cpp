// SPDX-FileCopyrightText: 2018 Contributors to Chatterino <https://chatterino.com>
//
// SPDX-License-Identifier: MIT

#include "BrowserExtension.hpp"

#include "singletons/NativeMessaging.hpp"
#include "singletons/NativeMessagingProtocol.hpp"
#include "util/IpcQueue.hpp"
#include "util/RenameThread.hpp"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QHash>
#include <QJsonDocument>
#include <QJsonObject>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <mutex>
#include <syncstream>
#include <thread>

#ifdef Q_OS_WIN
#    include <fcntl.h>
#    include <io.h>
#    include <Windows.h>
// clang-format off
// TlHelp32.h must follow Windows.h.
#    include <TlHelp32.h>
// clang-format on

#    include <cstdio>

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
    // EOF is sent by the input thread while replies come from the output
    // thread. Keep each length prefix and payload together on the wire.
    static std::mutex outputMutex;
    const std::lock_guard lock(outputMutex);
    auto len = static_cast<uint32_t>(str.size());
    std::osyncstream(std::cerr) << "[BrowserExtension] sendToBrowser: length=" << len
              << " content=" << str.data() << std::endl;
    std::cout.write(reinterpret_cast<const char *>(&len), sizeof(len));
    std::cout.write(str.data(), str.size());
    std::cout.flush();
}

QByteArray receiveFromBrowser()
{
    uint32_t size = 0;
    std::cin.read(reinterpret_cast<char *>(&size), sizeof(size));

    std::osyncstream(std::cerr) << "[BrowserExtension] receiveFromBrowser: length prefix=" << size
              << std::endl;

    if (std::cin.gcount() != sizeof(size) ||
        !nm::isValidNativeMessageSize(size))
    {
        std::osyncstream(std::cerr) << "[BrowserExtension] receiveFromBrowser: invalid length "
                  << "(gcount=" << std::cin.gcount() << ")" << std::endl;
        return {};
    }

    QByteArray buffer{static_cast<QByteArray::size_type>(size),
                      Qt::Uninitialized};
    std::cin.read(buffer.data(), size);

    if (std::cin.gcount() != static_cast<std::streamsize>(size))
    {
        std::osyncstream(std::cerr) << "[BrowserExtension] receiveFromBrowser: incomplete read "
                  << "(expected=" << size << " got=" << std::cin.gcount() << ")"
                  << std::endl;
        return {};
    }

    return buffer;
}

#ifdef Q_OS_WIN
bool isSupportedBrowserExecutable(const QString &executable)
{
    return executable.compare("chrome.exe", Qt::CaseInsensitive) == 0 ||
           executable.compare("firefox.exe", Qt::CaseInsensitive) == 0 ||
           executable.compare("vivaldi.exe", Qt::CaseInsensitive) == 0 ||
           executable.compare("opera.exe", Qt::CaseInsensitive) == 0 ||
           executable.compare("msedge.exe", Qt::CaseInsensitive) == 0 ||
           executable.compare("brave.exe", Qt::CaseInsensitive) == 0;
}

bool isSupportedBrowserWindow(HWND window)
{
    if (window == nullptr || ::IsWindow(window) == 0)
    {
        return false;
    }
    DWORD processId = 0;
    ::GetWindowThreadProcessId(window, &processId);
    if (processId == 0)
    {
        return false;
    }

    auto process =
        ::OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, false, processId);
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

    return isSupportedBrowserExecutable(
        QFileInfo(
            QString::fromWCharArray(path, static_cast<qsizetype>(pathLength)))
            .fileName());
}

/// Whether a process (by PID) is a supported browser. Used to find the owning
/// browser through the launcher chain when the host's direct parent is not the
/// browser itself.
bool isSupportedBrowserProcess(DWORD processId)
{
    if (processId == 0)
    {
        return false;
    }

    auto process =
        ::OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, false, processId);
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

    return isSupportedBrowserExecutable(
        QFileInfo(
            QString::fromWCharArray(path, static_cast<qsizetype>(pathLength)))
            .fileName());
}

/// Returns the parent process id of `processId`, or 0 when it cannot be
/// determined.
DWORD parentProcessIdOf(DWORD processId)
{
    auto snapshot = ::CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE)
    {
        return 0;
    }

    DWORD parent = 0;
    PROCESSENTRY32W entry{};
    entry.dwSize = sizeof(entry);
    if (::Process32FirstW(snapshot, &entry) != 0)
    {
        do
        {
            if (entry.th32ProcessID == processId)
            {
                parent = entry.th32ParentProcessID;
                break;
            }
        } while (::Process32NextW(snapshot, &entry) != 0);
    }
    ::CloseHandle(snapshot);
    return parent;
}

struct BrowserWindowSearch {
    DWORD processId = 0;
    HWND result = nullptr;
};

BOOL CALLBACK findBrowserWindowForProcess(HWND window, LPARAM parameter)
{
    auto *search = reinterpret_cast<BrowserWindowSearch *>(parameter);

    DWORD processId = 0;
    ::GetWindowThreadProcessId(window, &processId);
    if (processId != search->processId || ::IsWindowVisible(window) == 0)
    {
        return TRUE;
    }
    // Browsers keep several invisible/zero-sized helper windows around; only a
    // real top-level frame can host the overlay.
    if (::GetWindow(window, GW_OWNER) != nullptr)
    {
        return TRUE;
    }
    RECT rect{};
    if (::GetWindowRect(window, &rect) == 0 || rect.right - rect.left <= 0 ||
        rect.bottom - rect.top <= 0)
    {
        return TRUE;
    }

    search->result = window;
    return FALSE;
}

/// Resolves a browser top-level window without relying on OS focus.
///
/// The extension can only report `browserWindowFocused` while the browser owns
/// focus, but a v2 select requires a browser HWND unconditionally. Launching
/// Chatterino moves focus away from the browser, so focus-based capture alone
/// leaves the very first attach with no HWND and it gets rejected.
///
/// The browser does not always spawn the host directly: observed on Windows is
/// `msedge.exe -> cmd.exe -> host`, where the browser launches the host through
/// a command line with pipe redirection. Walk the process ancestry until a
/// supported browser process is found, then scan its top-level windows, rather
/// than assuming the immediate parent is the browser.
HWND browserWindowFromHostProcess()
{
    static HWND cached = nullptr;
    if (cached != nullptr && isSupportedBrowserWindow(cached))
    {
        return cached;
    }
    cached = nullptr;

    DWORD current = ::GetCurrentProcessId();
    for (int depth = 0; depth < 32; ++depth)
    {
        const auto parent = parentProcessIdOf(current);
        if (parent == 0 || parent == current)
        {
            break;
        }

        if (isSupportedBrowserProcess(parent))
        {
            BrowserWindowSearch search{.processId = parent, .result = nullptr};
            ::EnumWindows(&findBrowserWindowForProcess,
                          reinterpret_cast<LPARAM>(&search));
            if (search.result != nullptr &&
                isSupportedBrowserWindow(search.result))
            {
                cached = search.result;
                break;
            }
        }

        current = parent;
    }
    return cached;
}
#endif

QByteArray rememberBrowserWindow(QByteArray message,
                                 QHash<QString, quintptr> &browserWindows,
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
        isSupportedBrowserWindow(reinterpret_cast<HWND>(startupBrowserWindow)))
    {
        target = startupBrowserWindow;
        browserWindows.insert(winId, target);
    }

    // Focus-based capture cannot help when the browser never owned focus while
    // a select was in flight (launching Chatterino steals it). This host is a
    // child of the browser process, so derive the window from the process tree
    // instead. If this lookup still cannot find a window, the desktop attach
    // handler uses Chatterino's original foreground-window fallback.
    if (target == 0)
    {
        if (auto owned = browserWindowFromHostProcess(); owned != nullptr)
        {
            target = reinterpret_cast<quintptr>(owned);
            browserWindows.insert(winId, target);
        }
    }

    if (target == 0 ||
        !isSupportedBrowserWindow(reinterpret_cast<HWND>(target)))
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
    std::osyncstream(std::cerr) << "[BrowserExtension] runBrowserOutboundLoop: starting"
              << std::endl;

    // This native host is the sole creator and drainer of the outbound queue;
    // the GUI only ever writes to it with open_only. A previous host that was
    // force-killed (e.g. the browser closing) leaves a stale, file-backed queue
    // behind on Windows. If that stale queue still holds its 100-message
    // backlog, every GUI try_send returns QueueFull and responses are dropped,
    // which breaks the desktop->browser path permanently. Replace (remove then
    // create) so each host launch starts from a clean, drained queue.
    auto [messageQueue, error] =
        ipc::IpcQueue::tryReplaceOrCreate("chatterino_browser", 100, 1024);

    if (!error.isEmpty() || !messageQueue)
    {
        std::osyncstream(std::cerr) << "[BrowserExtension] runBrowserOutboundLoop: failed to "
                  << "open/create IPC queue 'chatterino_browser' - error: "
                  << error.toStdString() << std::endl;
        return;
    }

    std::osyncstream(std::cerr) << "[BrowserExtension] runBrowserOutboundLoop: IPC queue "
              << "'chatterino_browser' opened successfully" << std::endl;

    // The desktop process can start before this outbound queue exists. Tell
    // the extension when the host is ready so it can replay the active chat
    // geometry after that startup ordering.
    sendToBrowser(QLatin1String{
        R"({"type":"status","status":"native-host-ready","protocolVersion":2,"capabilities":["sessions"]})"});

    while (true)
    {
        auto buf = messageQueue->receiveFor(std::chrono::milliseconds(250));
        if (buf.isEmpty())
        {
            continue;
        }
        std::osyncstream(std::cerr) << "[BrowserExtension] runBrowserOutboundLoop: received "
                  << "message from IPC queue 'chatterino_browser' - length="
                  << buf.size() << " content=" << buf.constData() << std::endl;
        sendToBrowser(QLatin1String(buf.constData(), buf.size()));
    }
}

void runLoop()
{
    std::osyncstream(std::cerr) << "[BrowserExtension] runLoop: starting" << std::endl;

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
            std::osyncstream(std::cerr) << "[BrowserExtension] runLoop: receiveFromBrowser "
                      << "returned null, breaking loop" << std::endl;
            break;
        }

        std::osyncstream(std::cerr) << "[BrowserExtension] runLoop: received message from "
                  << "browser stdin - length=" << buffer.size()
                  << " content=" << buffer.constData() << std::endl;

        auto processedMessage = rememberBrowserWindow(std::move(buffer),
                                                      browserWindows,
#ifdef Q_OS_WIN
                                                      startupBrowserWindow
#else
                                                      0
#endif
                                                      );

        std::osyncstream(std::cerr) << "[BrowserExtension] runLoop: sending message to IPC "
                  << "queue 'chatterino_gui' - length=" << processedMessage.size()
                  << " content=" << processedMessage.constData() << std::endl;

        nm::client::sendMessage(std::move(processedMessage));
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

    // Each browser connection needs its own log. freopen_s closes stderr even
    // when opening the destination fails (for example, another host holds the
    // old shared log). Writing diagnostics to that closed CRT stream aborts
    // the host before it can send its readiness message. Redirect the C++
    // stream only after opening a separate log successfully.
    {
        const auto logPath =
            QDir::tempPath() + QStringLiteral("/chatterino-native-host-%1.log")
                                  .arg(QCoreApplication::applicationPid());
#ifdef Q_OS_WIN
        const std::filesystem::path nativeLogPath(logPath.toStdWString());
#else
        const std::filesystem::path nativeLogPath(logPath.toStdString());
#endif
        static std::ofstream stderrLog(nativeLogPath, std::ios::app);
        if (stderrLog.is_open())
        {
            std::cerr.rdbuf(stderrLog.rdbuf());
            std::osyncstream(std::cerr) << "\n=== native host started, pid "
                      << QCoreApplication::applicationPid() << " ===\n"
                      << std::flush;
        }
    }

    runLoop();
}

}  // namespace chatterino
