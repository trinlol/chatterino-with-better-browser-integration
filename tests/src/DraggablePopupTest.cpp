#include "widgets/DraggablePopup.hpp"
#include "widgets/BaseWindow.hpp"
#include "Test.hpp"
#include <QWindow>


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

}  // namespace chatterino

