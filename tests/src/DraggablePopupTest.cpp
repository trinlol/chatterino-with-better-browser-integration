#include "widgets/DraggablePopup.hpp"
#include "widgets/BasePopup.hpp"
#include "widgets/BaseWindow.hpp"
#include "Test.hpp"
#include <QWindow>

#ifdef USEWINSDK
#    include <Windows.h>
#endif


#include "controllers/accounts/AccountController.hpp"
#include "controllers/commands/Command.hpp"
#include "controllers/commands/CommandController.hpp"
#include "controllers/hotkeys/HotkeyController.hpp"
#include "mocks/BaseApplication.hpp"
#include "mocks/EmoteController.hpp"
#include "singletons/WindowManager.hpp"

namespace chatterino {
namespace {

class MockApplication : public mock::BaseApplication
{
public:
    MockApplication()
        : windowManager(this->args_, this->paths_, this->settings, this->theme,
                        this->fonts)
        , commands(this->paths_)
    {
    }

    HotkeyController *getHotkeys() override
    {
        return &this->hotkeys;
    }

    WindowManager *getWindows() override
    {
        return &this->windowManager;
    }

    AccountController *getAccounts() override
    {
        return &this->accounts;
    }

    CommandController *getCommands() override
    {
        return &this->commands;
    }

    EmoteController *getEmotes() override
    {
        return &this->emotes;
    }

    HotkeyController hotkeys;
    WindowManager windowManager;
    AccountController accounts;
    CommandController commands;
    mock::EmoteController emotes;
};

}  // namespace

TEST(DraggablePopupTest, CreateWithParent)
{
    MockApplication mockApp;

    QWidget parentWidget;
    std::cout << "1: parentWidget created" << std::endl;
    parentWidget.show();
    std::cout << "2: parentWidget shown" << std::endl;

    std::cout << "3: creating DraggablePopup" << std::endl;
    auto *popup = new DraggablePopup(true, &parentWidget);
    std::cout << "4: DraggablePopup created" << std::endl;

    std::cout << "5: showing popup" << std::endl;
    popup->show();
    std::cout << "6: popup shown, windowHandle = " << popup->windowHandle() << std::endl;
    if (popup->windowHandle())
    {
        std::cout << "7: windowHandle->parent() = " << popup->windowHandle()->parent() << std::endl;
    }


    popup->deleteLater();
}

TEST(DraggablePopupTest, ParentWindowChangeEventBeforeNativeWindow)
{
    MockApplication mockApp;

    QWidget parentWidget;
    auto *popup = new DraggablePopup(true, &parentWidget);
    EXPECT_EQ(popup->windowHandle(), nullptr);

    QEvent ev(QEvent::ParentWindowChange);
    QCoreApplication::sendEvent(popup, &ev);

    popup->deleteLater();
}

TEST(DraggablePopupTest, NullParentLifecycleAndSignalCleanup)
{
    MockApplication mockApp;

    auto *parent = new QWidget();
    auto *popup = new DraggablePopup(true, nullptr);
    QPointer<DraggablePopup> weakPopup(popup);

    QObject::connect(parent, &QObject::destroyed, popup, &QWidget::deleteLater);

    popup->show();
    EXPECT_TRUE(weakPopup != nullptr);

    // Destroy the parent widget (simulating Split being closed)
    delete parent;
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);

    EXPECT_TRUE(weakPopup.isNull());
}

#ifdef USEWINSDK
TEST(DraggablePopupTest, DialogPopupInheritsTopMostFromTopMostHost)
{
    MockApplication mockApp;
    mockApp.settings.windowTopMost = false;

    QWidget hostWindow(nullptr, Qt::FramelessWindowHint | Qt::Window);
    hostWindow.show();
    auto hostHwnd = reinterpret_cast<HWND>(hostWindow.winId());
    ::SetWindowPos(hostHwnd, HWND_TOPMOST, 0, 0, 0, 0,
                   SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    ASSERT_NE(::GetWindowLongPtr(hostHwnd, GWL_EXSTYLE) & WS_EX_TOPMOST, 0);

    auto *popup = new BasePopup(
        {BaseWindow::EnableCustomFrame, BaseWindow::DisableLayoutSave},
        &hostWindow);
    popup->show();

    // Allow BaseWindow::showEvent's deferred tryApplyTopMost() timer to fire.
    QEventLoop loop;
    QTimer::singleShot(50, &loop, &QEventLoop::quit);
    loop.exec();

    auto popupHwnd = reinterpret_cast<HWND>(popup->winId());
    EXPECT_NE(::GetWindowLongPtr(popupHwnd, GWL_EXSTYLE) & WS_EX_TOPMOST, 0);

    popup->close();
    popup->deleteLater();
}

TEST(DraggablePopupTest, DialogPopupDoesNotPromoteNonTopMostHost)
{
    MockApplication mockApp;
    mockApp.settings.windowTopMost = false;

    QWidget hostWindow(nullptr, Qt::Window);
    hostWindow.show();
    auto hostHwnd = reinterpret_cast<HWND>(hostWindow.winId());
    ASSERT_EQ(::GetWindowLongPtr(hostHwnd, GWL_EXSTYLE) & WS_EX_TOPMOST, 0);

    auto *popup = new BasePopup(
        {BaseWindow::EnableCustomFrame, BaseWindow::DisableLayoutSave},
        &hostWindow);
    popup->show();

    QEventLoop loop;
    QTimer::singleShot(50, &loop, &QEventLoop::quit);
    loop.exec();

    auto popupHwnd = reinterpret_cast<HWND>(popup->winId());
    EXPECT_EQ(::GetWindowLongPtr(popupHwnd, GWL_EXSTYLE) & WS_EX_TOPMOST, 0);
    EXPECT_EQ(::GetWindowLongPtr(hostHwnd, GWL_EXSTYLE) & WS_EX_TOPMOST, 0);

    popup->close();
    popup->deleteLater();
}
#endif

}  // namespace chatterino

