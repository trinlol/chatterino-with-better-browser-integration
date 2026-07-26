// SPDX-FileCopyrightText: 2018 Contributors to Chatterino <https://chatterino.com>
//
// SPDX-License-Identifier: MIT

#include "widgets/AttachedWindow.hpp"

#include "Application.hpp"
#include "common/QLogging.hpp"
#include "singletons/Settings.hpp"
#include "util/ApplicationIcon.hpp"
#include "util/DebugCount.hpp"
#include "widgets/splits/Split.hpp"

#include <QTimer>
#include <QVBoxLayout>
#include <QWindow>

#include <memory>
#include <unordered_map>

#ifdef USEWINSDK
#    include "util/WindowsHelper.hpp"

// clang-format off
// don't even think about reordering these
#    include "Windows.h"
#    include "Psapi.h"
// clang-format on
#    pragma comment(lib, "Dwmapi.lib")
#endif

namespace chatterino {

#ifdef USEWINSDK
static thread_local std::vector<HWND> taskbarHwnds;

namespace {

std::unordered_map<HWND, AttachedWindow *> attachedWindows;
HWINEVENTHOOK targetWindowHook = nullptr;

void CALLBACK handleTargetWindowEvent(HWINEVENTHOOK /*hook*/, DWORD event,
                                      HWND hwnd, LONG objectId, LONG childId,
                                      DWORD /*eventThread*/,
                                      DWORD /*eventTime*/)
{
    if ((event != EVENT_OBJECT_STATECHANGE &&
         event != EVENT_OBJECT_LOCATIONCHANGE) ||
        objectId != OBJID_WINDOW || childId != CHILDID_SELF)
    {
        return;
    }

    auto attachedWindow = attachedWindows.find(hwnd);
    if (attachedWindow != attachedWindows.end())
    {
        attachedWindow->second->syncToTargetWindow();
    }
}

void registerTargetWindowHook(HWND target, AttachedWindow *window)
{
    attachedWindows.insert_or_assign(target, window);

    if (!targetWindowHook)
    {
        targetWindowHook = ::SetWinEventHook(
            EVENT_OBJECT_STATECHANGE, EVENT_OBJECT_LOCATIONCHANGE, nullptr,
            handleTargetWindowEvent, 0, 0,
            WINEVENT_OUTOFCONTEXT | WINEVENT_SKIPOWNPROCESS);
    }
}

void unregisterTargetWindowHook(HWND target, AttachedWindow *window)
{
    auto attachedWindow = attachedWindows.find(target);
    if (attachedWindow != attachedWindows.end() &&
        attachedWindow->second == window)
    {
        attachedWindows.erase(attachedWindow);
    }

    if (attachedWindows.empty() && targetWindowHook)
    {
        ::UnhookWinEvent(targetWindowHook);
        targetWindowHook = nullptr;
    }
}

void moveOverlayWindow(HWND hwnd, int x, int y, int width, int height)
{
    RECT current{};
    const bool haveCurrentRect = ::GetWindowRect(hwnd, &current) != 0;
    if (haveCurrentRect && current.left == x && current.top == y &&
        current.right - current.left == width &&
        current.bottom - current.top == height)
    {
        return;
    }

    UINT flags = SWP_NOACTIVATE | SWP_NOZORDER | SWP_NOOWNERZORDER;
    if (haveCurrentRect && current.right - current.left == width &&
        current.bottom - current.top == height)
    {
        // During a browser drag only the position changes. Keeping the
        // existing composited surface avoids a synchronous full repaint.
        flags |= SWP_NOSIZE;
    }

    ::SetWindowPos(hwnd, nullptr, x, y, width, height, flags);
}

}  // namespace

BOOL CALLBACK enumWindows(HWND hwnd, LPARAM)
{
    constexpr int length = 16;

    auto className = std::make_unique<WCHAR[]>(length);
    GetClassName(hwnd, className.get(), length);

    if (lstrcmp(className.get(), L"Shell_TrayWnd") == 0 ||
        lstrcmp(className.get(), L"Shell_Secondary") == 0)
    {
        taskbarHwnds.push_back(hwnd);
    }

    return true;
}
#endif

AttachedWindow::AttachedWindow(void *_target)
    : QWidget(nullptr, Qt::FramelessWindowHint | Qt::Window)
    , target_(_target)
{
    this->setAttribute(Qt::WA_QuitOnClose, false);

    QLayout *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    this->setLayout(layout);

    auto *split = new Split(this);
    this->ui_.split = split;
    split->setSizePolicy(QSizePolicy::Maximum, QSizePolicy::MinimumExpanding);
    layout->addWidget(split);

#ifdef USEWINSDK
    this->targetWindowSettleTimer_.setInterval(75);
    this->targetWindowSettleTimer_.setSingleShot(true);
    QObject::connect(&this->targetWindowSettleTimer_, &QTimer::timeout, [this] {
        if (!this->targetWindowTransitioning_)
        {
            return;
        }

        auto target = HWND(this->target_);
        if (!::IsWindow(target))
        {
            this->targetWindowTransitioning_ = false;
            this->updateWindowRect(this->target_);
            return;
        }

        // An owned overlay follows its browser through minimize/restore.
        // If a zoom transition was interrupted by minimizing, defer the final
        // placement until the browser is restored.
        if (::IsIconic(target))
        {
            return;
        }

        const bool zoomed = ::IsZoomed(target) != 0;
        if (zoomed != this->targetWindowZoomed_)
        {
            this->targetWindowZoomed_ = zoomed;
            this->targetWindowSettleTimer_.start();
            return;
        }

        this->targetWindowTransitioning_ = false;
        this->updateWindowRect(this->target_);
        if (this->requestedVisible_)
        {
            this->show();
        }
    });
#endif

    DebugCount::increase(DebugObject::AttachedWindow);
}

AttachedWindow::~AttachedWindow()
{
#ifdef USEWINSDK
    unregisterTargetWindowHook(HWND(this->target_), this);
#endif

    for (auto it = items.begin(); it != items.end(); it++)
    {
        if (it->window == this)
        {
            items.erase(it);
            break;
        }
    }

    DebugCount::decrease(DebugObject::AttachedWindow);
}

AttachedWindow *AttachedWindow::get(void *target, const GetArgs &args)
{
    AttachedWindow *window = [&]() {
        for (Item &item : items)
        {
            if (item.hwnd == target)
            {
                item.winId = args.winId;
                return item.window;
            }
        }

        auto *window = new AttachedWindow(target);
        items.push_back(Item{target, window, args.winId});
        return window;
    }();

    bool show = true;
    QSize size = window->size();

    window->fullscreen_ = args.fullscreen;

    window->x_ = args.x;
    window->pixelRatio_ = args.pixelRatio;

    if (args.height != -1)
    {
        if (args.height == 0)
        {
            window->hide();
            show = false;
        }
        else
        {
            window->height_ = args.height;
            size.setHeight(args.height);
        }
    }
    if (args.width != -1)
    {
        if (args.width == 0)
        {
            window->hide();
            show = false;
        }
        else
        {
            window->width_ = args.width;
            size.setWidth(args.width);
        }
    }

    window->requestedVisible_ = show;
    if (show)
    {
#ifdef USEWINSDK
        if (window->targetWindowTransitioning_)
        {
            return window;
        }
#endif
        window->updateWindowRect(window->target_);
        window->show();
    }

    return window;
}

#ifdef USEWINSDK
AttachedWindow *AttachedWindow::getForeground(const GetArgs &args)
{
    return AttachedWindow::get(::GetForegroundWindow(), args);
}
#endif

void AttachedWindow::detach(const QString &winId)
{
    for (auto it = items.begin(); it != items.end();)
    {
        if (it->winId == winId)
        {
            it->window->deleteLater();
            it = items.erase(it);
        }
        else
        {
            ++it;
        }
    }
}

void AttachedWindow::setChannel(ChannelPtr channel)
{
    this->ui_.split->setChannel(std::move(channel));
}

void AttachedWindow::showEvent(QShowEvent *event)
{
    QWidget::showEvent(event);
    applyApplicationIcon(this);
    this->attachToHwnd(this->target_);
}

void AttachedWindow::attachToHwnd(void *_attachedPtr)
{
#ifdef USEWINSDK
    if (this->attached_)
    {
        return;
    }

    auto hwnd = HWND(this->winId());
    auto attached = HWND(_attachedPtr);

    this->attached_ = true;
    this->targetWindowZoomed_ = ::IsZoomed(attached) != 0;
    this->targetWindowStateInitialized_ = true;

    // Set the browser window as the owner of this window to prevent Z-order flickering
    ::SetWindowLongPtr(hwnd, GWLP_HWNDPARENT,
                       reinterpret_cast<LONG_PTR>(attached));
    ::SetWindowPos(hwnd, nullptr, 0, 0, 0, 0,
                   SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_NOZORDER |
                       SWP_FRAMECHANGED);

    // Location changes are delivered immediately by the WinEvent hook. Keep
    // the timer as a fallback and for foreground/Z-order maintenance.
    registerTargetWindowHook(attached, this);
    this->timer_.setInterval(16);
    QObject::connect(&this->timer_, &QTimer::timeout, [this, attached] {
        // check process id
        if (!this->validProcessName_)
        {
            DWORD processId;
            ::GetWindowThreadProcessId(attached, &processId);

            HANDLE process = ::OpenProcess(
                PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, false, processId);

            std::unique_ptr<TCHAR[]> filename(new TCHAR[512]);
            DWORD filenameLength =
                ::GetModuleFileNameEx(process, nullptr, filename.get(), 512);
            QString qfilename =
                QString::fromWCharArray(filename.get(), int(filenameLength));
            if (process)
            {
                ::CloseHandle(process);
            }

            if (!getSettings()->attachExtensionToAnyProcess)
            {
                // We don't attach to non-browser processes by default.
                if (!qfilename.endsWith("chrome.exe") &&
                    !qfilename.endsWith("firefox.exe") &&
                    !qfilename.endsWith("vivaldi.exe") &&
                    !qfilename.endsWith("opera.exe") &&
                    !qfilename.endsWith("msedge.exe") &&
                    !qfilename.endsWith("brave.exe"))

                {
                    qCWarning(chatterinoWidget)
                        << "NM Illegal caller" << qfilename;
                    this->timer_.stop();
                    this->deleteLater();
                    return;
                }
            }
            this->validProcessName_ = true;
        }

        if (this->targetWindowTransitioning_)
        {
            if (!::IsIconic(attached) &&
                !this->targetWindowSettleTimer_.isActive())
            {
                this->targetWindowSettleTimer_.start();
            }
            return;
        }

        this->updateWindowRect(attached);
    });

    this->timer_.start();

    // SLOW TIMER - used to hide taskbar behind fullscreen window
    this->slowTimer_.setInterval(2000);
    QObject::connect(&this->slowTimer_, &QTimer::timeout, [this, attached] {
        if (this->fullscreen_)
        {
            taskbarHwnds.clear();
            ::EnumWindows(&enumWindows, 0);

            for (auto taskbarHwnd : taskbarHwnds)
            {
                ::SetWindowPos(taskbarHwnd,
                               GetNextWindow(attached, GW_HWNDNEXT), 0, 0, 0, 0,
                               SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
            }
        }
    });
    this->slowTimer_.start();
#endif
}

#ifdef USEWINSDK
void AttachedWindow::syncToTargetWindow()
{
    auto target = HWND(this->target_);
    if (::IsIconic(target))
    {
        return;
    }

    const bool zoomed = ::IsZoomed(target) != 0;
    if (!this->targetWindowStateInitialized_)
    {
        this->targetWindowZoomed_ = zoomed;
        this->targetWindowStateInitialized_ = true;
    }
    else if (zoomed != this->targetWindowZoomed_)
    {
        this->targetWindowZoomed_ = zoomed;
        this->targetWindowTransitioning_ = true;
        this->hide();
    }

    if (this->targetWindowTransitioning_)
    {
        // Coalesce the animated maximize/restore rectangle burst. Applying
        // every queued intermediate rectangle makes the overlay visibly trail
        // the browser even though ordinary dragging is synchronized directly.
        this->targetWindowSettleTimer_.start();
        return;
    }

    this->updateWindowRect(this->target_);
}
#endif

void AttachedWindow::updateWindowRect(void *_attachedPtr)
{
#ifdef USEWINSDK
    auto hwnd = HWND(this->winId());
    auto attached = HWND(_attachedPtr);

    // We get the window rect first so we can close this window when it returns
    // an error. If we query the process first and check the filename then it
    // will return and empty string that doens't match.
    ::SetLastError(0);
    RECT rect;
    ::GetWindowRect(attached, &rect);

    if (::GetLastError() != 0)
    {
        qCWarning(chatterinoWidget) << "NM GetLastError()" << ::GetLastError();

        this->timer_.stop();
        this->deleteLater();
        return;
    }

    // Update topmost state based on foreground window
    HWND foreground = ::GetForegroundWindow();
    HWND root = ::GetAncestor(foreground, GA_ROOTOWNER);
    bool isBrowserActive = (root == attached || root == hwnd);

    if (isBrowserActive != this->wasBrowserActive_)
    {
        this->wasBrowserActive_ = isBrowserActive;
        if (isBrowserActive)
        {
            ::SetWindowPos(hwnd, HWND_TOPMOST, 0, 0, 0, 0,
                           SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
        }
        else
        {
            ::SetWindowPos(hwnd, HWND_NOTOPMOST, 0, 0, 0, 0,
                           SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
            HWND prev = ::GetNextWindow(attached, GW_HWNDPREV);
            if (prev && prev != hwnd)
            {
                ::SetWindowPos(hwnd, prev, 0, 0, 0, 0,
                               SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
            }
            else if (!prev)
            {
                ::SetWindowPos(hwnd, HWND_TOP, 0, 0, 0, 0,
                               SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
            }
        }
    }
    float scale = 1.f;
    float ourScale = 1.F;
    if (auto dpi = getWindowDpi(attached))
    {
        scale = *dpi / 96.f;
        ourScale = scale / this->devicePixelRatio();

        for (auto w : this->ui_.split->findChildren<BaseWidget *>())
        {
            w->setOverrideScale(ourScale);
        }
        this->ui_.split->setOverrideScale(ourScale);
    }

    if (this->height_ != -1)
    {
        const auto splitWidth = int(this->width_ * ourScale);
        if (this->ui_.split->minimumWidth() != splitWidth ||
            this->ui_.split->maximumWidth() != splitWidth)
        {
            this->ui_.split->setFixedWidth(splitWidth);
        }

        // offset
        int o = this->fullscreen_ ? 0 : 8;

        if (this->pixelRatio_ != -1.0)
        {
            moveOverlayWindow(
                hwnd,
                int(rect.left + this->x_ * scale * this->pixelRatio_ + o - 2),
                int(rect.bottom - this->height_ * scale - o),
                int(this->width_ * scale), int(this->height_ * scale));
        }
        //support for old extension version 1.3
        else if (this->x_ != -1.0)
        {
            moveOverlayWindow(hwnd, int(rect.left + this->x_ * scale + o),
                              int(rect.bottom - this->height_ * scale - o),
                              int(this->width_ * scale),
                              int(this->height_ * scale));
        }
        //support for old extension version 1.2
        else
        {
            moveOverlayWindow(hwnd, int(rect.right - this->width_ * scale - o),
                              int(rect.bottom - this->height_ * scale - o),
                              int(this->width_ * scale),
                              int(this->height_ * scale));
        }
    }

//    if (this->fullscreen_)
//    {
//        ::BringWindowToTop(attached);
//    }

//        ::MoveWindow(hwnd, rect.right - 360, rect.top + 82, 360 - 8,
//        rect.bottom - rect.top - 82 - 8, false);
#endif
}

// void AttachedWindow::nativeEvent(const QByteArray &eventType, void *message,
// long *result)
//{
//    MSG *msg = reinterpret_cast

//    case WM_NCCALCSIZE: {
//    }
//}

std::vector<AttachedWindow::Item> AttachedWindow::items;

}  // namespace chatterino
