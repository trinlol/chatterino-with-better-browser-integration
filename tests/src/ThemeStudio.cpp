// SPDX-FileCopyrightText: 2026 Contributors to Chatterino <https://chatterino.com>
//
// SPDX-License-Identifier: MIT

#include "controllers/themes/ThemeCustomizer.hpp"
#include "singletons/Theme.hpp"
#include "Test.hpp"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>

#include "common/Args.hpp"
#include "common/Modes.hpp"
#include "singletons/Paths.hpp"

using namespace chatterino;

TEST(ThemeStudio, TestPathsInit)
{
    Args args;
    Modes modes(args);
    try
    {
        Paths paths(args, modes);
        EXPECT_FALSE(paths.rootAppDataDirectory.isEmpty());
        std::cout << "rootAppDataDirectory: " << paths.rootAppDataDirectory.toStdString() << std::endl;
    }
    catch (const std::exception &e)
    {
        FAIL() << "Paths threw exception: " << e.what();
    }
}

TEST(ThemeStudio, PresetsValidJsonAndColors)
{
    const QStringList presets = {
        "Dracula",
        "CatppuccinMocha",
        "CatppuccinMacchiato",
        "CatppuccinFrappe",
        "CatppuccinLatte",
        "Nord",
        "TokyoNight",
        "GruvboxDark",
        "OneDark",
        "SolarizedDark",
        "SolarizedLight",
        "Synthwave84",
    };

    for (const auto &preset : presets)
    {
        QString path = QString(":/themes/%1.json").arg(preset);
        QFile file(path);
        ASSERT_TRUE(file.open(QIODevice::ReadOnly))
            << "Failed to open " << path.toStdString();

        QJsonParseError parseError;
        auto doc = QJsonDocument::fromJson(file.readAll(), &parseError);
        ASSERT_FALSE(doc.isNull())
            << "JSON parse error in " << preset.toStdString() << ": "
            << parseError.errorString().toStdString();
        ASSERT_TRUE(doc.isObject())
            << "Root is not an object in " << preset.toStdString();

        auto root = doc.object();
        EXPECT_TRUE(root.contains("colors"))
            << "Missing 'colors' in " << preset.toStdString();
        auto colors = root["colors"].toObject();
        EXPECT_FALSE(colors.isEmpty())
            << "'colors' is empty in " << preset.toStdString();
        EXPECT_TRUE(colors.contains("accent"))
            << "Missing 'accent' in " << preset.toStdString();
        EXPECT_TRUE(colors.contains("messages"))
            << "Missing 'messages' in " << preset.toStdString();
        EXPECT_TRUE(colors.contains("tabs"))
            << "Missing 'tabs' in " << preset.toStdString();
        EXPECT_TRUE(colors.contains("splits"))
            << "Missing 'splits' in " << preset.toStdString();
    }
}

TEST(ThemeStudio, CustomizerTokenManipulation)
{
    ThemeCustomizer customizer;
    ThemeDescriptor desc{
        .key = "Nord",
        .path = ":/themes/Nord.json",
        .name = "Nord",
        .custom = false,
    };

    QString error;
    bool loaded = customizer.load(desc, &error);
    ASSERT_TRUE(loaded) << error.toStdString();
    EXPECT_TRUE(customizer.isBuiltIn());
    EXPECT_EQ(customizer.currentDescriptor().name, "Nord");

    // Check accent color
    QColor originalAccent = customizer.getColor("accent");
    EXPECT_TRUE(originalAccent.isValid());

    // Check live signal on token modification
    bool signalReceived = false;
    QString changedPath;
    QColor changedColor;
    QObject::connect(&customizer, &ThemeCustomizer::tokenChanged,
                     [&](const QString &path, const QColor &color) {
                         signalReceived = true;
                         changedPath = path;
                         changedColor = color;
                     });

    QColor newAccent("#FF00FF");
    customizer.setColor("accent", newAccent);

    EXPECT_TRUE(signalReceived);
    EXPECT_EQ(changedPath, "accent");
    EXPECT_EQ(changedColor, newAccent);
    EXPECT_EQ(customizer.getColor("accent"), newAccent);
    EXPECT_TRUE(customizer.isModified());
}

TEST(ThemeStudio, CustomizerSaveExportImport)
{
    QTemporaryDir tempDir;
    ASSERT_TRUE(tempDir.isValid());

    ThemeCustomizer customizer(tempDir.path());
    ThemeDescriptor desc{
        .key = "Dracula",
        .path = ":/themes/Dracula.json",
        .name = "Dracula",
        .custom = false,
    };

    QString error;
    ASSERT_TRUE(customizer.load(desc, &error)) << error.toStdString();

    QColor testColor("#123456");
    customizer.setColor("accent", testColor);

    // Save as custom
    QString customName = "DraculaCustom";
    ASSERT_TRUE(customizer.saveAsCustom(customName, &error))
        << error.toStdString();
    EXPECT_FALSE(customizer.isBuiltIn());
    EXPECT_EQ(customizer.currentDescriptor().name, customName);

    // Export to file
    QString exportPath = tempDir.filePath("ExportedTestTheme.json");
    ASSERT_TRUE(customizer.exportToFile(exportPath, &error))
        << error.toStdString();
    EXPECT_TRUE(QFile::exists(exportPath));

    // Import into a fresh customizer
    ThemeCustomizer importer(tempDir.path());
    ASSERT_TRUE(importer.importFromFile(exportPath, &error))
        << error.toStdString();
    EXPECT_EQ(importer.getColor("accent"), testColor);

    // Delete custom theme
    ASSERT_TRUE(importer.deleteCustomTheme(&error)) << error.toStdString();
}

TEST(ThemeStudio, TokenGroupsCoverage)
{
    const auto &groups = ThemeCustomizer::tokenGroups();
    EXPECT_FALSE(groups.empty());

    ThemeCustomizer customizer;
    ThemeDescriptor desc{
        .key = "TokyoNight",
        .path = ":/themes/TokyoNight.json",
        .name = "TokyoNight",
        .custom = false,
    };

    QString error;
    ASSERT_TRUE(customizer.load(desc, &error)) << error.toStdString();

    for (const auto &group : groups)
    {
        EXPECT_FALSE(group.name.isEmpty());
        EXPECT_FALSE(group.tokens.empty());
        for (const auto &token : group.tokens)
        {
            EXPECT_FALSE(token.path.isEmpty());
            EXPECT_FALSE(token.title.isEmpty());
            QColor color = customizer.getColor(token.path);
            EXPECT_TRUE(color.isValid())
                << "Token color invalid for: " << token.path.toStdString();
        }
    }
}
