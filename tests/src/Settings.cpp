// SPDX-FileCopyrightText: 2026 Contributors to Chatterino <https://chatterino.com>
//
// SPDX-License-Identifier: MIT

#include "singletons/Settings.hpp"

#include "common/Args.hpp"
#include "common/Modes.hpp"
#include "Test.hpp"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>

using namespace chatterino;
using namespace Qt::Literals;

namespace {

QJsonObject readSettingsJson(const QString &dirPath)
{
    QFile file(dirPath + u"/settings.json"_s);
    if (!file.open(QIODevice::ReadOnly))
    {
        return {};
    }
    return QJsonDocument::fromJson(file.readAll()).object();
}

QStringList readStringArray(const QJsonObject &root, const QString &section,
                            const QString &key)
{
    QStringList out;
    const auto arr = root.value(section).toObject().value(key).toArray();
    for (const auto &v : arr)
    {
        out.push_back(v.toString());
    }
    return out;
}

class FavouriteSettingsTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        ASSERT_TRUE(this->tempDir_.isValid());
    }

    void TearDown() override
    {
        // Leave the global SettingManager pointing at a valid directory after
        // this test's QTemporaryDir is cleaned up.
        static QTemporaryDir fallbackDir;
        pajlada::Settings::SettingManager::getInstance()->setPath(
            qPrintable(fallbackDir.path() + u"/settings.json"_s));
        getSettings()->favouriteEmotes = QStringList{};
        getSettings()->favouriteEmojis = QStringList{};
    }

    QTemporaryDir tempDir_;
};

}  // namespace

TEST_F(FavouriteSettingsTest, FavouriteEmotesPersistImmediatelyToDisk)
{
    Args args;
    Settings settings(Modes(args), args, this->tempDir_.path(),
                      SettingsArgs{.isTest = true, .runMigrations = false});

    settings.favouriteEmotes = QStringList{u"Kappa"_s, u"PogChamp"_s};

    auto onDisk = readStringArray(readSettingsJson(this->tempDir_.path()),
                                  u"emotes"_s, u"favouriteEmotes"_s);
    EXPECT_EQ(onDisk, (QStringList{u"Kappa"_s, u"PogChamp"_s}));

    settings.favouriteEmotes = QStringList{u"PogChamp"_s};

    onDisk = readStringArray(readSettingsJson(this->tempDir_.path()),
                             u"emotes"_s, u"favouriteEmotes"_s);
    EXPECT_EQ(onDisk, (QStringList{u"PogChamp"_s}));
}

TEST_F(FavouriteSettingsTest, FavouriteEmojisPersistImmediatelyToDisk)
{
    Args args;
    Settings settings(Modes(args), args, this->tempDir_.path(),
                      SettingsArgs{.isTest = true, .runMigrations = false});

    settings.favouriteEmojis = QStringList{u"joy"_s, u"fire"_s};

    auto onDisk = readStringArray(readSettingsJson(this->tempDir_.path()),
                                  u"emotes"_s, u"favouriteEmojis"_s);
    EXPECT_EQ(onDisk, (QStringList{u"joy"_s, u"fire"_s}));
}

TEST_F(FavouriteSettingsTest, DontSaveSettingsSkipsFavouriteDiskWrite)
{
    Args args;
    args.dontSaveSettings = true;
    Settings settings(Modes(args), args, this->tempDir_.path(),
                      SettingsArgs{.isTest = true, .runMigrations = false});

    settings.favouriteEmotes = QStringList{u"LUL"_s};

    EXPECT_FALSE(QFile::exists(this->tempDir_.path() + u"/settings.json"_s));
}
