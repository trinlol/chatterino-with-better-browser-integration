// SPDX-FileCopyrightText: 2019 Contributors to Chatterino <https://chatterino.com>
//
// SPDX-License-Identifier: MIT

#include "util/WindowsHelper.hpp"

#include "Application.hpp"
#include "common/Literals.hpp"

#include <QApplication>
#include <QClipboard>
#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QSettings>
#include <QStandardPaths>

#ifdef USEWINSDK

#    include <Ole2.h>
#    include <ShellScalingApi.h>
#    include <Shlwapi.h>
#    include <VersionHelpers.h>
#    include <propkey.h>
#    include <propvarutil.h>
#    include <shobjidl.h>

#    pragma comment(lib, "shell32.lib")
#    pragma comment(lib, "propsys.lib")

namespace chatterino {

namespace {

bool setShortcutAppUserModelId(IShellLinkW *shellLink,
                               const QString &appUserModelId)
{
    if (shellLink == nullptr)
    {
        return false;
    }

    IPropertyStore *propertyStore = nullptr;
    if (FAILED(shellLink->QueryInterface(IID_PPV_ARGS(&propertyStore))))
    {
        return false;
    }

    PROPVARIANT value;
    const auto appId = appUserModelId.toStdWString();
    const HRESULT initResult =
        InitPropVariantFromString(appId.c_str(), &value);
    if (FAILED(initResult))
    {
        propertyStore->Release();
        return false;
    }

    const HRESULT setResult =
        propertyStore->SetValue(PKEY_AppUserModel_ID, value);
    PropVariantClear(&value);

    const HRESULT commitResult = propertyStore->Commit();
    propertyStore->Release();

    return SUCCEEDED(setResult) && SUCCEEDED(commitResult);
}

bool writeShellShortcut(const QString &shortcutPath, const QString &exePath,
                        const QString &workingDirectory,
                        const QString &appUserModelId)
{
    IShellLinkW *shellLink = nullptr;
    if (FAILED(CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER,
                                IID_PPV_ARGS(&shellLink))))
    {
        return false;
    }

    const auto exe = exePath.toStdWString();
    const auto workDir = workingDirectory.toStdWString();

    shellLink->SetPath(exe.c_str());
    shellLink->SetWorkingDirectory(workDir.c_str());
    shellLink->SetIconLocation(exe.c_str(), 0);

    if (!setShortcutAppUserModelId(shellLink, appUserModelId))
    {
        shellLink->Release();
        return false;
    }

    IPersistFile *persistFile = nullptr;
    if (FAILED(shellLink->QueryInterface(IID_PPV_ARGS(&persistFile))))
    {
        shellLink->Release();
        return false;
    }

    const auto shortcut = shortcutPath.toStdWString();
    const HRESULT saveResult =
        persistFile->Save(shortcut.c_str(), TRUE);

    persistFile->Release();
    shellLink->Release();

    return SUCCEEDED(saveResult);
}

bool shortcutTargetMatches(IShellLinkW *shellLink, const QString &exePath)
{
    if (shellLink == nullptr)
    {
        return false;
    }

    wchar_t target[MAX_PATH]{};
    WIN32_FIND_DATAW findData{};
    if (FAILED(shellLink->GetPath(target, MAX_PATH, &findData, SLGP_RAWPATH)))
    {
        return false;
    }

    const QString shortcutTarget =
        QFileInfo(QString::fromWCharArray(target)).absoluteFilePath();
    const QString expectedTarget = QFileInfo(exePath).absoluteFilePath();

    return shortcutTarget.compare(expectedTarget, Qt::CaseInsensitive) == 0;
}

void syncShortcutDirectory(const QString &directoryPath,
                           const QString &exePath,
                           const QString &appUserModelId)
{
    QDir directory(directoryPath);
    if (!directory.exists())
    {
        return;
    }

    const auto shortcutFiles =
        directory.entryList({QStringLiteral("*.lnk")}, QDir::Files);

    for (const auto &shortcutFile : shortcutFiles)
    {
        const auto shortcutPath = directory.filePath(shortcutFile);

        IShellLinkW *shellLink = nullptr;
        if (FAILED(CoCreateInstance(CLSID_ShellLink, nullptr,
                                    CLSCTX_INPROC_SERVER,
                                    IID_PPV_ARGS(&shellLink))))
        {
            continue;
        }

        IPersistFile *persistFile = nullptr;
        if (FAILED(shellLink->QueryInterface(IID_PPV_ARGS(&persistFile))))
        {
            shellLink->Release();
            continue;
        }

        const auto shortcut = shortcutPath.toStdWString();
        if (FAILED(persistFile->Load(shortcut.c_str(), STGM_READWRITE)))
        {
            persistFile->Release();
            shellLink->Release();
            continue;
        }

        if (!shortcutTargetMatches(shellLink, exePath))
        {
            persistFile->Release();
            shellLink->Release();
            continue;
        }

        if (setShortcutAppUserModelId(shellLink, appUserModelId))
        {
            persistFile->Save(shortcut.c_str(), TRUE);
        }

        persistFile->Release();
        shellLink->Release();
    }
}

}  // namespace

using namespace literals;

using GetDpiForMonitor_ = HRESULT(CALLBACK *)(HMONITOR, MONITOR_DPI_TYPE,
                                              UINT *, UINT *);

// TODO: This should be changed to `GetDpiForWindow`.
std::optional<UINT> getWindowDpi(HWND hwnd)
{
    static HINSTANCE shcore = LoadLibrary(L"Shcore.dll");
    if (shcore != nullptr)
    {
        if (auto getDpiForMonitor =
                GetDpiForMonitor_(GetProcAddress(shcore, "GetDpiForMonitor")))
        {
            HMONITOR monitor =
                MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST);

            UINT xScale = 96;
            UINT yScale = 96;
            getDpiForMonitor(monitor, MDT_DEFAULT, &xScale, &yScale);

            return xScale;
        }
    }

    return std::nullopt;
}

void flushClipboard()
{
    if (QApplication::clipboard()->ownsClipboard())
    {
        OleFlushClipboard();
    }
}

const QString RUN_KEY =
    uR"(HKEY_CURRENT_USER\Software\Microsoft\Windows\CurrentVersion\Run)"_s;

bool isRegisteredForStartup()
{
    QSettings settings(RUN_KEY, QSettings::NativeFormat);

    return !settings.value("Chatterino").toString().isEmpty();
}

void setRegisteredForStartup(bool isRegistered)
{
    auto *app = tryGetApp();
    if (app && app->isTest())
    {
        return;
    }

    QSettings settings(RUN_KEY, QSettings::NativeFormat);

    if (isRegistered)
    {
        auto exePath = QFileInfo(QCoreApplication::applicationFilePath())
                           .absoluteFilePath()
                           .replace('/', '\\');

        settings.setValue("Chatterino", "\"" + exePath + "\" --autorun");
    }
    else
    {
        settings.remove("Chatterino");
    }
}

QString getAssociatedExecutable(AssociationQueryType queryType, LPCWSTR query)
{
    // always error out instead of returning a truncated string when the
    // buffer is too small - avoids race condition when the user changes their
    // default browser between calls to AssocQueryString
    ASSOCF flags = ASSOCF_NOTRUNCATE;

    if (queryType == AssociationQueryType::Protocol)
    {
        // ASSOCF_IS_PROTOCOL was introduced in Windows 8
        if (IsWindows8OrGreater())
        {
            flags |= ASSOCF_IS_PROTOCOL;
        }
        else
        {
            return {};
        }
    }

    DWORD resultSize = 0;
    if (FAILED(AssocQueryStringW(flags, ASSOCSTR_EXECUTABLE, query, nullptr,
                                 nullptr, &resultSize)))
    {
        return {};
    }

    if (resultSize <= 1)
    {
        // resultSize includes the null terminator. if resultSize is 1, the
        // returned value would be the empty string.
        return {};
    }

    QString result;
    auto *buf = new wchar_t[resultSize];
    if (SUCCEEDED(AssocQueryStringW(flags, ASSOCSTR_EXECUTABLE, query, nullptr,
                                    buf, &resultSize)))
    {
        // QString::fromWCharArray expects the length in characters *not
        // including* the null terminator, but AssocQueryStringW calculates
        // length including the null terminator
        result = QString::fromWCharArray(buf, resultSize - 1);
    }
    delete[] buf;
    return result;
}

void ensureWindowsShellShortcuts(const QString &appUserModelId)
{
    if (appUserModelId.isEmpty())
    {
        return;
    }

    const HRESULT comInit =
        CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    const bool needsComUninit =
        comInit == S_OK || comInit == S_FALSE;

    const auto exeInfo =
        QFileInfo(QCoreApplication::applicationFilePath());
    const auto exePath = exeInfo.absoluteFilePath();
    const auto workDir = exeInfo.absolutePath();

    const auto startMenuPrograms = QStandardPaths::writableLocation(
        QStandardPaths::ApplicationsLocation);
    if (!startMenuPrograms.isEmpty())
    {
        writeShellShortcut(
            QDir(startMenuPrograms).filePath(
                QStringLiteral("Chatterino Better Browser.lnk")),
            exePath, workDir, appUserModelId);
    }

    const auto desktopPath =
        QStandardPaths::writableLocation(QStandardPaths::DesktopLocation);
    if (!desktopPath.isEmpty())
    {
        writeShellShortcut(
            QDir(desktopPath).filePath(
                QStringLiteral("Chatterino Better Browser.lnk")),
            exePath, workDir, appUserModelId);
    }

    const auto roamingAppData = QStandardPaths::writableLocation(
        QStandardPaths::GenericConfigLocation);
    if (!roamingAppData.isEmpty())
    {
        const auto pinnedTaskbar = QDir(roamingAppData).filePath(
            QStringLiteral("Microsoft/Internet Explorer/Quick Launch/User "
                           "Pinned/TaskBar"));
        syncShortcutDirectory(pinnedTaskbar, exePath, appUserModelId);
    }

    if (needsComUninit)
    {
        CoUninitialize();
    }
}

}  // namespace chatterino

#endif
