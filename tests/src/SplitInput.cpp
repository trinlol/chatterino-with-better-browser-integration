// SPDX-FileCopyrightText: 2024 Contributors to Chatterino <https://chatterino.com>
//
// SPDX-License-Identifier: MIT

#include "widgets/splits/SplitInput.hpp"

#include "common/Literals.hpp"
#include "controllers/accounts/AccountController.hpp"
#include "controllers/commands/Command.hpp"
#include "controllers/commands/CommandController.hpp"
#include "controllers/hotkeys/HotkeyController.hpp"
#include "mocks/BaseApplication.hpp"
#include "mocks/EmoteController.hpp"
#include "singletons/Fonts.hpp"
#include "singletons/Paths.hpp"
#include "singletons/Settings.hpp"
#include "singletons/Theme.hpp"
#include "singletons/WindowManager.hpp"
#include "Test.hpp"
#include "widgets/Notebook.hpp"
#include "common/WindowDescriptors.hpp"
#include "widgets/splits/Split.hpp"

#include <QDebug>
#include <QString>
#include <QTextEdit>

using namespace chatterino;
using ::testing::Exactly;

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

class SplitInputTest
    : public ::testing::TestWithParam<std::tuple<QString, QString>>
{
public:
    SplitInputTest()
        : split(new Split(nullptr))
        , input(this->split)
    {
    }

    MockApplication mockApplication;
    Split *split;
    SplitInput input;
};

}  // namespace

TEST_P(SplitInputTest, Reply)
{
    std::tuple<QString, QString> params = this->GetParam();
    auto [inputText, expected] = params;
    ASSERT_EQ("", this->input.getInputText());
    this->input.setInputText(inputText);
    ASSERT_EQ(inputText, this->input.getInputText());

    auto *message = new Message();
    message->displayName = "forsen";
    auto reply = MessagePtr(message);
    this->input.setReply(reply);
    QString actual = this->input.getInputText();
    ASSERT_EQ(expected, actual) << "Input text after setReply should be '"
                                << expected << "', but got '" << actual << "'";
}

INSTANTIATE_TEST_SUITE_P(
    SplitInput, SplitInputTest,
    testing::Values(
        // Ensure message is retained
        std::make_tuple<QString, QString>(
            // Pre-existing text in the input
            "Test message",
            // Expected text after replying to forsen
            "@forsen Test message "),

        // Ensure mention is stripped, no message
        std::make_tuple<QString, QString>(
            // Pre-existing text in the input
            "@forsen",
            // Expected text after replying to forsen
            "@forsen "),

        // Ensure mention with space is stripped, no message
        std::make_tuple<QString, QString>(
            // Pre-existing text in the input
            "@forsen ",
            // Expected text after replying to forsen
            "@forsen "),

        // Ensure mention is stripped, retain message
        std::make_tuple<QString, QString>(
            // Pre-existing text in the input
            "@forsen Test message",
            // Expected text after replying to forsen
            "@forsen Test message "),

        // Ensure mention with comma is stripped, no message
        std::make_tuple<QString, QString>(
            // Pre-existing text in the input
            "@forsen,",
            // Expected text after replying to forsen
            "@forsen "),

        // Ensure mention with comma is stripped, retain message
        std::make_tuple<QString, QString>(
            // Pre-existing text in the input
            "@forsen Test message",
            // Expected text after replying to forsen
            "@forsen Test message "),

        // Ensure mention with comma and space is stripped, no message
        std::make_tuple<QString, QString>(
            // Pre-existing text in the input
            "@forsen, ",
            // Expected text after replying to forsen
            "@forsen "),

        // Ensure it works with no message
        std::make_tuple<QString, QString>(
            // Pre-existing text in the input
            "",
            // Expected text after replying to forsen
            "@forsen ")));

TEST(SplitInput, SelectionHighlightColor)
{
    MockApplication mockApp;
    Split split(nullptr);
    SplitInput input(&split);

    auto *textEdit = input.findChild<QTextEdit *>();
    ASSERT_NE(textEdit, nullptr);

    auto palette = textEdit->palette();
    // Verify highlight is blue and does NOT match the base/background color
    EXPECT_NE(palette.color(QPalette::Active, QPalette::Highlight),
              palette.color(QPalette::Active, QPalette::Base));
    EXPECT_EQ(palette.color(QPalette::Active, QPalette::Highlight),
              QColor(42, 130, 218));
    EXPECT_EQ(palette.color(QPalette::Inactive, QPalette::Highlight),
              QColor(42, 130, 218));
    EXPECT_EQ(palette.color(QPalette::Active, QPalette::HighlightedText),
              Qt::white);
    EXPECT_EQ(palette.color(QPalette::Inactive, QPalette::HighlightedText),
              Qt::white);
}

TEST(SplitDescriptor, BuildDescriptorKickAndCombined)
{
    MockApplication mockApp;

    // Test Kick channel serialization and round-trip
    {
        Split split(nullptr);
        auto kickChan =
            std::make_shared<Channel>("testkickuser", Channel::Type::Kick);
        split.setChannel(kickChan);

        auto desc = split.buildDescriptor();
        EXPECT_EQ(desc.type_, "kick");
        EXPECT_EQ(desc.channelName_, "testkickuser");

        auto json = desc.toJson();
        EXPECT_EQ(json["type"].toString(), "split");
        auto data = json["data"].toObject();
        EXPECT_EQ(data["type"].toString(), "kick");
        EXPECT_EQ(data["name"].toString(), "testkickuser");

        auto loadedDesc = SplitDescriptor::loadFromJSON(json);
        EXPECT_EQ(loadedDesc.type_, "kick");
        EXPECT_EQ(loadedDesc.channelName_, "testkickuser");
    }

    // Test Combined channel serialization and round-trip
    {
        Split split(nullptr);
        auto combinedChan = std::make_shared<Channel>(
            "twitch_user+kick_user", Channel::Type::Combined);
        split.setChannel(combinedChan);

        auto desc = split.buildDescriptor();
        EXPECT_EQ(desc.type_, "combined");
        EXPECT_EQ(desc.channelName_, "twitch_user+kick_user");

        auto json = desc.toJson();
        EXPECT_EQ(json["type"].toString(), "split");
        auto data = json["data"].toObject();
        EXPECT_EQ(data["type"].toString(), "combined");
        EXPECT_EQ(data["name"].toString(), "twitch_user+kick_user");

        auto loadedDesc = SplitDescriptor::loadFromJSON(json);
        EXPECT_EQ(loadedDesc.type_, "combined");
        EXPECT_EQ(loadedDesc.channelName_, "twitch_user+kick_user");
    }
}

