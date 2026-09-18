// SPDX-FileCopyrightText: 2026 Contributors to Chatterino <https://chatterino.com>
//
// SPDX-License-Identifier: MIT

#include "controllers/themes/ThemeCustomizer.hpp"

#include "Application.hpp"
#include "singletons/Paths.hpp"
#include "singletons/Theme.hpp"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QRegularExpression>

namespace {

using namespace chatterino;

std::optional<QJsonObject> readJsonFile(const QString &path)
{
    QFile file(path);
    if (!file.open(QFile::ReadOnly))
    {
        return std::nullopt;
    }

    QJsonParseError err{};
    auto doc = QJsonDocument::fromJson(file.readAll(), &err);
    if (err.error != QJsonParseError::NoError || !doc.isObject())
    {
        return std::nullopt;
    }
    return doc.object();
}

QString formatColor(const QColor &color)
{
    if (color.alpha() < 255)
    {
        return color.name(QColor::HexArgb);
    }
    return color.name(QColor::HexRgb);
}

}  // namespace

namespace chatterino {

ThemeCustomizer::ThemeCustomizer(const QString &customThemesDir, QObject *parent)
    : QObject(parent)
    , customThemesDir_(customThemesDir)
{
    this->loadDefaultFallback(false);
}

QString ThemeCustomizer::getThemesDirectory() const
{
    if (!this->customThemesDir_.isEmpty())
    {
        return this->customThemesDir_;
    }
    if (getApp())
    {
        return getApp()->getPaths().themesDirectory;
    }
    return QString();
}

void ThemeCustomizer::loadDefaultFallback(bool isLight)
{
    auto path = isLight ? QStringLiteral(":/themes/Light.json")
                        : QStringLiteral(":/themes/Dark.json");
    if (auto obj = readJsonFile(path))
    {
        this->fallbackJson_ = *obj;
        this->currentJson_ = *obj;
    }
}

bool ThemeCustomizer::loadFromPath(const QString &path)
{
    auto obj = readJsonFile(path);
    if (!obj)
    {
        return false;
    }

    this->currentJson_ = *obj;
    bool light = this->isLight();
    auto fallbackPath = light ? QStringLiteral(":/themes/Light.json")
                              : QStringLiteral(":/themes/Dark.json");
    if (auto fb = readJsonFile(fallbackPath))
    {
        this->fallbackJson_ = *fb;
    }

    Q_EMIT this->themeLoaded();
    return true;
}

bool ThemeCustomizer::loadFromDescriptor(const ThemeDescriptor &descriptor,
                                         QString *errorMessage)
{
    this->currentDescriptor_ = descriptor;
    this->isModified_ = false;

    if (!this->loadFromPath(descriptor.path))
    {
        if (errorMessage)
        {
            *errorMessage = "Failed to load theme file from " + descriptor.path;
        }
        return false;
    }
    return true;
}

bool ThemeCustomizer::isLight() const
{
    return this->currentJson_[QLatin1String("metadata")][QLatin1String("iconTheme")].toString() ==
           QLatin1String("dark");
}

void ThemeCustomizer::setIsLight(bool light)
{
    auto meta = this->currentJson_[QLatin1String("metadata")].toObject();
    meta[QLatin1String("iconTheme")] = light ? QStringLiteral("dark") : QStringLiteral("light");
    meta[QLatin1String("fallbackTheme")] = light ? QStringLiteral("Light") : QStringLiteral("Dark");
    this->currentJson_[QLatin1String("metadata")] = meta;
}

QColor ThemeCustomizer::color(const QString &tokenPath) const
{
    auto lookup = [](const QJsonObject &root, const QString &path) -> std::optional<QColor> {
        auto parts = path.split(QLatin1Char('.'));
        QJsonObject current = root[QLatin1String("colors")].toObject();
        for (int i = 0; i < parts.size() - 1; ++i)
        {
            if (!current.contains(parts[i]) || !current[parts[i]].isObject())
            {
                return std::nullopt;
            }
            current = current[parts[i]].toObject();
        }
        if (parts.isEmpty())
        {
            return std::nullopt;
        }
        auto leaf = current[parts.last()];
        if (!leaf.isString())
        {
            return std::nullopt;
        }
        QColor col(leaf.toString());
        if (col.isValid())
        {
            return col;
        }
        return std::nullopt;
    };

    if (auto c = lookup(this->currentJson_, tokenPath))
    {
        return *c;
    }
    if (auto c = lookup(this->fallbackJson_, tokenPath))
    {
        return *c;
    }
    return QColor(Qt::magenta);
}

void ThemeCustomizer::setColor(const QString &tokenPath, const QColor &color)
{
    auto parts = tokenPath.split(QLatin1Char('.'));
    if (parts.isEmpty())
    {
        return;
    }

    std::function<QJsonObject(QJsonObject, int)> updateNested = [&](QJsonObject cur, int depth) -> QJsonObject {
        if (depth == parts.size() - 1)
        {
            cur[parts[depth]] = formatColor(color);
            return cur;
        }
        QJsonObject child = cur[parts[depth]].toObject();
        cur[parts[depth]] = updateNested(child, depth + 1);
        return cur;
    };

    QJsonObject colors = this->currentJson_[QLatin1String("colors")].toObject();
    this->currentJson_[QLatin1String("colors")] = updateNested(colors, 0);
    this->isModified_ = true;

    Q_EMIT this->tokenChanged(tokenPath, color);
}

QJsonObject ThemeCustomizer::toJson() const
{
    return this->currentJson_;
}

bool ThemeCustomizer::saveAsCustom(const QString &name, QString *errorMessage)
{
    QString trimmed = name.trimmed();
    if (trimmed.isEmpty())
    {
        if (errorMessage)
        {
            *errorMessage = "Theme name cannot be empty.";
        }
        return false;
    }

    static const QRegularExpression invalidChars(R"([<>:"/\\|?*])");
    if (trimmed.contains(invalidChars))
    {
        if (errorMessage)
        {
            *errorMessage = "Theme name contains invalid characters.";
        }
        return false;
    }

    auto themesDir = this->getThemesDirectory();
    if (themesDir.isEmpty())
    {
        if (errorMessage)
        {
            *errorMessage = "Themes directory is not configured.";
        }
        return false;
    }

    QDir dir(themesDir);
    if (!dir.exists() && !dir.mkpath(QStringLiteral(".")))
    {
        if (errorMessage)
        {
            *errorMessage = "Failed to create Custom Themes directory.";
        }
        return false;
    }

    auto filePath = dir.filePath(trimmed + QStringLiteral(".json"));
    if (!this->exportToFile(filePath, errorMessage))
    {
        return false;
    }

    this->currentDescriptor_.key = trimmed;
    this->currentDescriptor_.name = trimmed;
    this->currentDescriptor_.path = filePath;
    this->currentDescriptor_.custom = true;
    this->isModified_ = false;

    if (getApp() && getApp()->getThemes())
    {
        getApp()->getThemes()->reloadAvailableThemes();
    }
    return true;
}

bool ThemeCustomizer::exportToFile(const QString &filePath, QString *errorMessage) const
{
    QFile file(filePath);
    if (!file.open(QFile::WriteOnly | QFile::Truncate))
    {
        if (errorMessage)
        {
            *errorMessage = file.errorString();
        }
        return false;
    }

    QJsonDocument doc(this->currentJson_);
    if (file.write(doc.toJson(QJsonDocument::Indented)) == -1)
    {
        if (errorMessage)
        {
            *errorMessage = file.errorString();
        }
        return false;
    }
    return true;
}

bool ThemeCustomizer::importFromFile(const QString &filePath, QString *errorMessage)
{
    auto obj = readJsonFile(filePath);
    if (!obj)
    {
        if (errorMessage)
        {
            *errorMessage = "Selected file is not valid JSON.";
        }
        return false;
    }

    if (!obj->contains(QLatin1String("colors")) || !obj->contains(QLatin1String("metadata")))
    {
        if (errorMessage)
        {
            *errorMessage = "Selected JSON does not match ChatterinoTheme schema.";
        }
        return false;
    }

    QFileInfo fi(filePath);
    auto themesDir = this->getThemesDirectory();
    if (themesDir.isEmpty())
    {
        if (errorMessage)
        {
            *errorMessage = "Themes directory is not configured.";
        }
        return false;
    }

    QDir dir(themesDir);
    if (!dir.exists())
    {
        dir.mkpath(QStringLiteral("."));
    }

    auto targetPath = dir.filePath(fi.fileName());
    if (!this->loadFromPath(filePath))
    {
        if (errorMessage)
        {
            *errorMessage = "Failed to load imported theme.";
        }
        return false;
    }

    if (!this->exportToFile(targetPath, errorMessage))
    {
        return false;
    }

    QString baseName = fi.completeBaseName();
    this->currentDescriptor_.key = baseName;
    this->currentDescriptor_.name = baseName;
    this->currentDescriptor_.path = targetPath;
    this->currentDescriptor_.custom = true;
    this->isModified_ = false;

    if (getApp() && getApp()->getThemes())
    {
        getApp()->getThemes()->reloadAvailableThemes();
    }
    return true;
}

bool ThemeCustomizer::deleteCustomTheme(QString *errorMessage)
{
    return deleteCustomTheme(this->currentDescriptor_.key,
                             this->getThemesDirectory(), errorMessage);
}

bool ThemeCustomizer::deleteCustomTheme(const QString &key,
                                        const QString &customThemesDir,
                                        QString *errorMessage)
{
    QString dirPath = customThemesDir;
    if (dirPath.isEmpty() && getApp())
    {
        dirPath = getApp()->getPaths().themesDirectory;
    }

    if (dirPath.isEmpty())
    {
        if (errorMessage)
        {
            *errorMessage = "Themes directory is not configured.";
        }
        return false;
    }

    QDir dir(dirPath);
    QString filename = key;
    if (!filename.endsWith(QStringLiteral(".json"), Qt::CaseInsensitive))
    {
        filename += QStringLiteral(".json");
    }

    if (!dir.exists(filename))
    {
        if (errorMessage)
        {
            *errorMessage = "Theme file not found in custom themes directory.";
        }
        return false;
    }

    if (!dir.remove(filename))
    {
        if (errorMessage)
        {
            *errorMessage = "Failed to remove theme file.";
        }
        return false;
    }

    if (getApp() && getApp()->getThemes())
    {
        getApp()->getThemes()->reloadAvailableThemes();
    }
    return true;
}

std::vector<ThemeTokenGroup> ThemeCustomizer::tokenGroups()
{
    return {
        {
            "Window & Accent",
            {
                {"accent", "Accent Color", "Primary highlight color for tabs, active indicators, and focus outlines"},
                {"window.background", "Window Background", "Main application background color"},
                {"window.border", "Window Border", "Outer borders of windows and dialogs"},
                {"window.text", "Window Text", "General UI text color"},
            },
        },
        {
            "Chat Messages",
            {
                {"messages.textColors.regular", "Regular Text", "Standard chat message text color"},
                {"messages.backgrounds.regular", "Message Background", "Default background color for chat lines"},
                {"messages.backgrounds.alternate", "Alternating Background", "Background color for alternating chat lines"},
                {"messages.textColors.system", "System Messages", "Notice and system info text color"},
                {"messages.textColors.link", "Links", "Clickable URL link color"},
                {"messages.textColors.chatPlaceholder", "Placeholder Text", "Text color for placeholder messages"},
            },
        },
        {
            "Selection & Highlights",
            {
                {"messages.selection.background", "Selection Background", "Background color of selected text in chat"},
                {"messages.selection.text", "Selection Text", "Color of highlighted/selected text"},
                {"messages.highlightBackgrounds.regular", "Mention Highlight", "Background color when user is mentioned"},
                {"messages.highlightBackgrounds.selfMessage", "Self Message Highlight", "Background color for own sent messages"},
                {"messages.highlightBackgrounds.redeemedHighlight", "Redeemed Highlight", "Background for channel point highlighted messages"},
            },
        },
        {
            "Tabs",
            {
                {"tabs.regular.background", "Tab Background", "Inactive notebook tab background"},
                {"tabs.regular.text", "Tab Text", "Inactive notebook tab text color"},
                {"tabs.selected.background", "Active Tab Background", "Currently selected notebook tab background"},
                {"tabs.selected.text", "Active Tab Text", "Currently selected notebook tab text color"},
                {"tabs.highlighted.background", "Highlighted Tab Background", "Tab background when receiving highlights/mentions"},
                {"tabs.highlighted.text", "Highlighted Tab Text", "Tab text color when receiving highlights/mentions"},
                {"tabs.newMessage.background", "New Message Tab Background", "Tab background when unread messages arrive"},
                {"tabs.newMessage.text", "New Message Tab Text", "Tab text color when unread messages arrive"},
                {"tabs.liveIndicator", "Channel Live Dot", "Indicator dot color for live Twitch channels"},
                {"tabs.dividerLine", "Tab Divider Line", "Separator line between channel tabs"},
            },
        },
        {
            "Splits & Headers",
            {
                {"splits.header.background", "Header Background", "Split header bar background color"},
                {"splits.header.text", "Header Text", "Channel title and viewer count text color"},
                {"splits.header.border", "Header Border", "Border line separating split header from chat"},
                {"splits.background", "Split Background", "Base background color of empty split panes"},
                {"splits.dropPreview", "Split Drop Preview", "Overlay color when dragging/dropping splits"},
                {"splits.resizeMargin", "Split Resize Margin", "Color of resizable divider lines between splits"},
            },
        },
        {
            "Input Box & Scrollbars",
            {
                {"splits.input.background", "Input Box Background", "Chat input text area background"},
                {"splits.input.text", "Input Box Text", "Text color while typing in chat"},
                {"splits.input.border", "Input Box Border", "Border around chat input field"},
                {"scrollbars.background", "Scrollbar Track", "Chat scrollbar track background"},
                {"scrollbars.thumb", "Scrollbar Thumb", "Draggable scrollbar handle"},
                {"scrollbars.thumbSelected", "Scrollbar Thumb Hovered", "Scrollbar handle when hovered or dragged"},
            },
        },
    };
}

}  // namespace chatterino
