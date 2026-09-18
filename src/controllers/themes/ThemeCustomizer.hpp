// SPDX-FileCopyrightText: 2026 Contributors to Chatterino <https://chatterino.com>
//
// SPDX-License-Identifier: MIT

#pragma once

#include "singletons/Theme.hpp"

#include <QColor>
#include <QJsonObject>
#include <QObject>
#include <QString>
#include <vector>

namespace chatterino {

struct ThemeTokenGroup {
    QString name;
    struct Token {
        QString path;
        QString title;
        QString description;
    };
    std::vector<Token> tokens;
};

class ThemeCustomizer : public QObject
{
    Q_OBJECT

public:
    explicit ThemeCustomizer(const QString &customThemesDir = QString(),
                             QObject *parent = nullptr);

    bool loadFromPath(const QString &path);
    bool loadFromDescriptor(const ThemeDescriptor &descriptor,
                            QString *errorMessage = nullptr);
    bool load(const ThemeDescriptor &descriptor,
              QString *errorMessage = nullptr)
    {
        return this->loadFromDescriptor(descriptor, errorMessage);
    }
    void loadDefaultFallback(bool isLight = false);

    QColor color(const QString &tokenPath) const;
    QColor getColor(const QString &tokenPath) const
    {
        return this->color(tokenPath);
    }
    void setColor(const QString &tokenPath, const QColor &color);

    bool isLight() const;
    void setIsLight(bool light);

    const ThemeDescriptor &currentDescriptor() const
    {
        return this->currentDescriptor_;
    }
    bool isBuiltIn() const
    {
        return !this->currentDescriptor_.custom;
    }
    bool isModified() const
    {
        return this->isModified_;
    }

    QJsonObject toJson() const;

    bool saveAsCustom(const QString &name, QString *errorMessage = nullptr);
    bool exportToFile(const QString &filePath,
                      QString *errorMessage = nullptr) const;
    bool importFromFile(const QString &filePath,
                        QString *errorMessage = nullptr);
    bool deleteCustomTheme(QString *errorMessage = nullptr);
    static bool deleteCustomTheme(const QString &key,
                                  const QString &customThemesDir = QString(),
                                  QString *errorMessage = nullptr);

    static std::vector<ThemeTokenGroup> tokenGroups();

Q_SIGNALS:
    void tokenChanged(const QString &tokenPath, const QColor &color);
    void themeLoaded();

private:
    QString getThemesDirectory() const;

    ThemeDescriptor currentDescriptor_{};
    bool isModified_ = false;
    QString customThemesDir_;

    QJsonObject currentJson_;
    QJsonObject fallbackJson_;
};

}  // namespace chatterino
