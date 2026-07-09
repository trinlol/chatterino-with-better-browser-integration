// SPDX-FileCopyrightText: 2026 Contributors to Chatterino <https://chatterino.com>
//
// SPDX-License-Identifier: MIT

#include "controllers/spellcheck/SpellChecker.hpp"

#include "Application.hpp"
#include "common/QLogging.hpp"
#include "singletons/Paths.hpp"
#include "singletons/Settings.hpp"
#include "util/CombinePath.hpp"
#include "util/FilesystemHelpers.hpp"
#include "util/XDGDirectory.hpp"

#include <algorithm>
#include <QDir>
#include <QLocale>

#ifdef CHATTERINO_WITH_SPELLCHECK
#    include <hunspell/hunspell.hxx>
#endif

namespace chatterino {

#ifdef CHATTERINO_WITH_SPELLCHECK

namespace {

/// Returns a list of available dictionaries in the given directory
std::vector<DictionaryInfo> loadDictionariesFromDirectory(
    const QDir &searchDirectory, bool isSystem)
{
    std::vector<DictionaryInfo> dictionaries;

    if (!searchDirectory.exists())
    {
        return dictionaries;
    }

    for (const auto &affInfo : searchDirectory.entryInfoList(
             {"*.aff"}, QDir::Files | QDir::NoDotAndDotDot, QDir::Name))
    {
        if (!affInfo.isFile())
        {
            continue;
        }

        auto dictName = affInfo.baseName();

        auto dicInfo = QFileInfo(searchDirectory, dictName % ".dic");
        if (!dicInfo.isFile())
        {
            continue;
        }

        auto isSymbolicLink =
            affInfo.isSymbolicLink() || dicInfo.isSymbolicLink();

        QString path = [&] {
            if (isSystem)
            {
                return searchDirectory.absoluteFilePath(dictName);
            }
            return dictName;
        }();

        dictionaries.push_back(DictionaryInfo{
            .name = dictName,
            .path = path,
            .isSymbolicLink = isSymbolicLink,
            .isSystem = isSystem,
        });
    }

    return dictionaries;
}

QString resolveDictionaryPath(const QString &path)
{
    if (QDir::isAbsolutePath(path))
    {
        return path;
    }
    return combinePath(getApp()->getPaths().dictionariesDirectory, path);
}

QString dictionaryLocaleName(const QString &baseName)
{
    const auto parts = baseName.split('_');
    if (parts.size() >= 2)
    {
        return parts[0] + '_' + parts[1];
    }
    return baseName;
}

QString dictionaryDisplayName(const QString &baseName)
{
    const auto localeName = dictionaryLocaleName(baseName);
    const QLocale locale(localeName);
    const auto language = locale.nativeLanguageName();
    if (language.isEmpty())
    {
        return baseName;
    }

    const auto territory = locale.nativeTerritoryName();
    if (!territory.isEmpty())
    {
        return language + " (" + territory + ')';
    }

    return language;
}

void addLibreOfficeExtensionDirectories(
    std::vector<std::pair<QString, bool>> &searchDirectories,
    const QString &extensionsPath)
{
    QDir extensionsDir(extensionsPath);
    if (!extensionsDir.exists())
    {
        return;
    }

    for (const auto &entry :
         extensionsDir.entryList({"dict-*"}, QDir::Dirs | QDir::NoDotAndDotDot))
    {
        searchDirectories.emplace_back(
            extensionsDir.absoluteFilePath(entry), true);
    }
}

void addLibreOfficeInstallDirectories(
    std::vector<std::pair<QString, bool>> &searchDirectories,
    const QString &installRoot)
{
    if (installRoot.isEmpty())
    {
        return;
    }

    addLibreOfficeExtensionDirectories(
        searchDirectories, combinePath(installRoot, "share/extensions"));

    QDir installDir(installRoot);
    if (!installDir.exists())
    {
        return;
    }

    for (const auto &entry :
         installDir.entryList({"LibreOffice*"}, QDir::Dirs | QDir::NoDotAndDotDot))
    {
        addLibreOfficeExtensionDirectories(
            searchDirectories,
            combinePath(installDir.absoluteFilePath(entry), "share/extensions"));
    }
}

void addWindowsDictionarySearchDirectories(
    std::vector<std::pair<QString, bool>> &searchDirectories)
{
    QStringList installRoots;
    for (const auto *envVar :
         {"ProgramFiles", "ProgramFiles(x86)", "LocalAppData"})
    {
        const auto value = qEnvironmentVariable(envVar);
        if (!value.isEmpty())
        {
            installRoots << value;
        }
    }

    for (const auto &root : installRoots)
    {
        addLibreOfficeInstallDirectories(searchDirectories, root + "/LibreOffice");
        addLibreOfficeInstallDirectories(searchDirectories, root + "/Programs/LibreOffice");

        searchDirectories.emplace_back(root + "/Hunspell/Dictionaries", true);
        searchDirectories.emplace_back(root + "/Hunspell/dic", true);
        searchDirectories.emplace_back(root + "/OpenOffice 4/share/dict", true);
        searchDirectories.emplace_back(root + "/OpenOffice/share/dict", true);
    }
}

void addMacDictionarySearchDirectories(
    std::vector<std::pair<QString, bool>> &searchDirectories)
{
    const QStringList hunspellDirectories = {
        "/opt/homebrew/share/hunspell",
        "/usr/local/share/hunspell",
        "/opt/local/share/hunspell",
        "/Library/Spelling",
    };

    for (const auto &directory : hunspellDirectories)
    {
        searchDirectories.emplace_back(directory, true);
    }

    addLibreOfficeExtensionDirectories(
        searchDirectories,
        "/Applications/LibreOffice.app/Contents/Resources/extensions");
}

std::vector<std::pair<QString, bool>> getDictionarySearchDirectories()
{
    std::vector<std::pair<QString, bool>> searchDirectories{
        {getApp()->getPaths().dictionariesDirectory, false},
    };

#    if defined(Q_OS_UNIX) and !defined(Q_OS_DARWIN)
    auto dataDirs = getXDGBaseDirectories(XDGDirectoryType::Data);
    for (const auto &dataDir : dataDirs)
    {
        searchDirectories.emplace_back(combinePath(dataDir, "hunspell"), true);
        searchDirectories.emplace_back(combinePath(dataDir, "myspell"), true);
        searchDirectories.emplace_back(combinePath(dataDir, "myspell/dicts"),
                                       true);
    }
#    elif defined(Q_OS_WIN)
    addWindowsDictionarySearchDirectories(searchDirectories);
#    elif defined(Q_OS_DARWIN)
    addMacDictionarySearchDirectories(searchDirectories);
#    endif

    return searchDirectories;
}

QString dictionaryListLabel(const DictionaryInfo &dict)
{
    auto label = dictionaryDisplayName(dict.name);
    if (label != dict.name)
    {
        label += " [" + dict.name + ']';
    }
    if (dict.isSystem)
    {
        label += " (System)";
    }
    return label;
}

}  // namespace

class SpellCheckerPrivate
{
public:
    static std::unique_ptr<SpellCheckerPrivate> tryLoad(
        const QString &path = {});

    /// NOTE: To support multiple dictionaries at the same time, it seems like we need to store a list of Hunspell instances, each supporting a single dictionary, and then during the spell checking process check each hunspell instance.
    Hunspell hunspell;

private:
    SpellCheckerPrivate(const char *affpath, const char *dpath);
};

std::unique_ptr<SpellCheckerPrivate> SpellCheckerPrivate::tryLoad(
    const QString &path)
{
    if (path.isEmpty())
    {
        qCDebug(chatterinoSpellcheck) << "No path specified";
        return nullptr;
    }

    auto resolvedPath = resolveDictionaryPath(path);
    auto aff = qStringToStdPath(resolvedPath % ".aff");
    auto dic = qStringToStdPath(resolvedPath % ".dic");

    if (!std::filesystem::exists(aff) || !std::filesystem::exists(dic))
    {
        qCInfo(chatterinoSpellcheck).nospace().noquote()
            << "Failed to find " << resolvedPath << ".{aff,dic}";
        return nullptr;
    }
    std::error_code ec;
    auto affCanonical = std::filesystem::weakly_canonical(aff, ec);
    if (ec)
    {
        qCInfo(chatterinoSpellcheck)
            << "Failed to canonicalize" << stdPathToQString(aff)
            << "error:" << QUtf8StringView(ec.message());
        return nullptr;
    }
    auto dicCanonical = std::filesystem::weakly_canonical(dic, ec);
    if (ec)
    {
        qCInfo(chatterinoSpellcheck)
            << "Failed to canonicalize" << stdPathToQString(dic)
            << "error:" << QUtf8StringView(ec.message());
        return nullptr;
    }

    return std::unique_ptr<SpellCheckerPrivate>{new SpellCheckerPrivate(
        affCanonical.string().c_str(), dicCanonical.string().c_str())};
}

SpellCheckerPrivate::SpellCheckerPrivate(const char *affpath, const char *dpath)
    : hunspell(affpath, dpath)
{
}

SpellChecker::SpellChecker()
{
    this->loadDictionary(getSettings()->spellCheckingDefaultDictionary);
}
#else
class SpellCheckerPrivate
{
};
SpellChecker::SpellChecker() = default;
#endif

SpellChecker::~SpellChecker() = default;

bool SpellChecker::loadDictionary(const QString &path)
{
#ifdef CHATTERINO_WITH_SPELLCHECK
    this->private_ = SpellCheckerPrivate::tryLoad(path);
    this->loadedDictionaryPath_ = this->private_ ? path : QString{};
    return this->private_ != nullptr || path.isEmpty();
#else
    (void)path;
    return false;
#endif
}

const QString &SpellChecker::loadedDictionaryPath() const
{
    return this->loadedDictionaryPath_;
}

bool SpellChecker::isLoaded() const
{
    return this->private_ != nullptr;
}

// NOLINTNEXTLINE(readability-convert-member-functions-to-static)
bool SpellChecker::check(const QString &word)
{
#ifdef CHATTERINO_WITH_SPELLCHECK
    if (!this->private_)
    {
        return true;
    }

    return this->private_->hunspell.spell(word.toStdString());
#else
    (void)word;
    return true;
#endif
}

// NOLINTNEXTLINE(readability-convert-member-functions-to-static)
std::vector<std::string> SpellChecker::suggestions(const QString &word)
{
#ifdef CHATTERINO_WITH_SPELLCHECK
    if (!this->private_)
    {
        return {};
    }

    auto stdWord = word.toStdString();
    if (this->private_->hunspell.spell(stdWord))
    {
        return {};
    }

    return this->private_->hunspell.suggest(stdWord);
#else
    (void)word;
    return {};
#endif
}

// NOLINTNEXTLINE(readability-convert-member-functions-to-static)
std::vector<DictionaryInfo> SpellChecker::getAvailableDictionaries() const
{
#ifdef CHATTERINO_WITH_SPELLCHECK
    std::vector<DictionaryInfo> dictionaries;

    for (const auto &[searchDirectory, isSystem] :
         getDictionarySearchDirectories())
    {
        qCDebug(chatterinoSpellcheck)
            << "Looking for dictionaries in" << searchDirectory
            << "isSystem:" << isSystem;
        for (const auto &dict :
             loadDictionariesFromDirectory(QDir(searchDirectory), isSystem))
        {
            if (dict.isSymbolicLink && dict.isSystem)
            {
                continue;
            }

            dictionaries.push_back(DictionaryInfo{
                .name = dictionaryListLabel(dict),
                .path = dict.path,
                .isSymbolicLink = dict.isSymbolicLink,
                .isSystem = dict.isSystem,
            });
        }
    }

    std::ranges::sort(dictionaries,
                      [](const DictionaryInfo &lhs, const DictionaryInfo &rhs) {
                          return std::tie(lhs.isSystem, lhs.name, lhs.path) <
                                 std::tie(rhs.isSystem, rhs.name, rhs.path);
                      });

    const auto uniqueEnd = std::ranges::unique(
                               dictionaries, {}, &DictionaryInfo::path)
                               .begin();
    dictionaries.erase(uniqueEnd, dictionaries.end());

    return dictionaries;
#else
    return {};
#endif
}

}  // namespace chatterino
