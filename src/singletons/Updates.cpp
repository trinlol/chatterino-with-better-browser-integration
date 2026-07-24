// SPDX-FileCopyrightText: 2018 Contributors to Chatterino <https://chatterino.com>
//
// SPDX-License-Identifier: MIT

#include "singletons/Updates.hpp"

#include "common/Literals.hpp"
#include "common/network/NetworkRequest.hpp"
#include "common/network/NetworkResult.hpp"
#include "common/QLogging.hpp"
#include "common/Version.hpp"
#include "singletons/Paths.hpp"
#include "singletons/Settings.hpp"
#include "util/CombinePath.hpp"
#include "util/PostToThread.hpp"

#include <QCoreApplication>
#include <QDesktopServices>
#include <QFile>
#include <QJsonArray>
#include <QJsonObject>
#include <QStringBuilder>
#include <QtConcurrent>
#include <semver/semver.hpp>

#include <optional>
#include <utility>

namespace {

using namespace chatterino;
using namespace literals;

const QString RELEASES_API_URL =
    u"https://api.github.com/repos/trinlol/"
    u"chatterino-with-better-browser-integration/releases?per_page=20"_s;
const QString RELEASES_URL =
    u"https://github.com/trinlol/"
    u"chatterino-with-better-browser-integration/releases"_s;
const QString RELEASE_TAG_URL_PREFIX = RELEASES_URL + u"/tag/"_s;

struct GithubRelease {
    QString version;
    QString pageUrl;
    semver::version semanticVersion;
};

std::optional<GithubRelease> latestGithubRelease(const QJsonArray &releases,
                                                 bool includePrereleases)
{
    std::optional<GithubRelease> latest;

    for (const auto &value : releases)
    {
        const auto object = value.toObject();
        if (object.isEmpty() || object.value("draft").toBool() ||
            (!includePrereleases && object.value("prerelease").toBool()))
        {
            continue;
        }

        auto tag = object.value("tag_name").toString();
        const auto pageUrl = object.value("html_url").toString();
        if (tag.startsWith(u'v'))
        {
            tag.remove(0, 1);
        }
        if (!pageUrl.startsWith(RELEASE_TAG_URL_PREFIX))
        {
            continue;
        }

        semver::version semanticVersion;
        if (!semanticVersion.from_string_noexcept(tag.toStdString()))
        {
            continue;
        }
        if (!latest || semanticVersion > latest->semanticVersion)
        {
            latest = GithubRelease{
                .version = std::move(tag),
                .pageUrl = pageUrl,
                .semanticVersion = semanticVersion,
            };
        }
    }

    return latest;
}

}  // namespace

namespace chatterino {

Updates::Updates(const Paths &paths_, Settings &settings)
    : paths(paths_)
    , currentVersion_(CHATTERINO_VERSION)
    , updateGuideLink_(RELEASES_URL)
{
    qCDebug(chatterinoUpdate) << "init UpdateManager";

    settings.betaUpdates.connect(
        [this] {
            this->checkForUpdates();
        },
        this->managedConnections, false);
}

/// Checks if the online version is newer or older than the current version.
bool Updates::isDowngradeOf(const QString &online, const QString &current)
{
    semver::version onlineVersion;
    if (!onlineVersion.from_string_noexcept(online.toStdString()))
    {
        qCWarning(chatterinoUpdate) << "Unable to parse online version"
                                    << online << "into a proper semver string";
        return false;
    }

    semver::version currentVersion;
    if (!currentVersion.from_string_noexcept(current.toStdString()))
    {
        qCWarning(chatterinoUpdate) << "Unable to parse current version"
                                    << current << "into a proper semver string";
        return false;
    }

    return onlineVersion < currentVersion;
}

bool Updates::isNewerThan(const QString &online, const QString &current)
{
    semver::version onlineVersion;
    semver::version currentVersion;
    if (!onlineVersion.from_string_noexcept(online.toStdString()) ||
        !currentVersion.from_string_noexcept(current.toStdString()))
    {
        return false;
    }

    return onlineVersion > currentVersion;
}

void Updates::deleteOldFiles()
{
    std::ignore = QtConcurrent::run([dir{this->paths.miscDirectory}] {
        {
            auto path = combinePath(dir, "Update.exe");
            if (QFile::exists(path))
            {
                QFile::remove(path);
            }
        }
        {
            auto path = combinePath(dir, "update.zip");
            if (QFile::exists(path))
            {
                QFile::remove(path);
            }
        }
    });
}

const QString &Updates::getCurrentVersion() const
{
    return this->currentVersion_;
}

const QString &Updates::getOnlineVersion() const
{
    return this->onlineVersion_;
}

void Updates::installUpdates()
{
    if (this->status_ != UpdateAvailable)
    {
        assert(false);
        return;
    }

    QDesktopServices::openUrl(this->updateGuideLink_);
}

void Updates::checkForUpdates()
{
#ifndef CHATTERINO_DISABLE_UPDATER
    auto version = Version::instance();

    if (!version.isSupportedOS())
    {
        qCDebug(chatterinoUpdate)
            << "Update checking disabled because OS doesn't appear to be one "
               "of Windows, GNU/Linux or macOS.";
        return;
    }

    // Disable updates on Flatpak
    if (version.isFlatpak())
    {
        return;
    }

    NetworkRequest(RELEASES_API_URL)
        .timeout(60000)
        .header("Accept", "application/vnd.github+json")
        .header("X-GitHub-Api-Version", "2022-11-28")
        .onError([this](NetworkResult result) {
            qCDebug(chatterinoUpdate)
                << "error checking GitHub releases" << result.formatError();
            this->setStatus_(SearchFailed);
        })
        .onSuccess([this](auto result) {
            const auto release = latestGithubRelease(
                result.parseJsonArray(), getSettings()->betaUpdates);
            if (!release)
            {
                this->setStatus_(SearchFailed);
                qCDebug(chatterinoUpdate)
                    << "GitHub returned no valid Chatterino Better Browser "
                       "release";
                return;
            }

            this->onlineVersion_ = release->version;
            this->updateGuideLink_ = release->pageUrl;
            this->isDowngrade_ = Updates::isDowngradeOf(this->onlineVersion_,
                                                        this->currentVersion_);
            if (Updates::isNewerThan(this->onlineVersion_,
                                     this->currentVersion_))
            {
                this->setStatus_(UpdateAvailable);
            }
            else
            {
                this->setStatus_(NoUpdateAvailable);
            }
        })
        .execute();
    this->setStatus_(Searching);
#endif
}

Updates::Status Updates::getStatus() const
{
    return this->status_;
}

QString Updates::portableUpdaterPath()
{
    return combinePath(QCoreApplication::applicationDirPath(),
                       "updater.1/ChatterinoUpdater.exe");
}

bool Updates::shouldShowUpdateButton() const
{
    switch (this->getStatus())
    {
        case UpdateAvailable:
        case SearchFailed:
        case Downloading:
        case DownloadFailed:
        case WriteFileFailed:
            return true;

        default:
            return false;
    }
}

bool Updates::isError() const
{
    switch (this->getStatus())
    {
        case SearchFailed:
        case DownloadFailed:
        case WriteFileFailed:
        case MissingPortableUpdater:
        case RunUpdaterFailed:
            return true;

        default:
            return false;
    }
}

bool Updates::isDowngrade() const
{
    return this->isDowngrade_;
}

QString Updates::buildUpdateAvailableText() const
{
    return QString("A Chatterino Better Browser update (%1) is available "
                   "from our GitHub.\n\nDo you want to open the release page?")
        .arg(this->getOnlineVersion());
}

void Updates::setStatus_(Status status)
{
    if (this->status_ != status)
    {
        this->status_ = status;
        postToThread([this, status] {
            this->statusUpdated.invoke(status);
        });
    }
}

}  // namespace chatterino
