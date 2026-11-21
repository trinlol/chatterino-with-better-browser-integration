// SPDX-FileCopyrightText: 2018 Contributors to Chatterino <https://chatterino.com>
//
// SPDX-License-Identifier: MIT

#include "widgets/settingspages/AboutPage.hpp"

#include "Application.hpp"
#include "common/Common.hpp"
#include "common/QLogging.hpp"
#include "common/Version.hpp"
#include "singletons/Paths.hpp"
#include "util/Expected.hpp"  // IWYU pragma: keep - this is being used to see if we're using the expected_lite library
#include "util/LayoutCreator.hpp"
#include "util/RemoveScrollAreaBackground.hpp"
#include "widgets/BasePopup.hpp"
#include "widgets/layout/FlowLayout.hpp"

#include <QFile>
#include <QFormLayout>
#include <QGroupBox>
#include <QLabel>
#include <QStringBuilder>
#include <QTextEdit>
#include <QTextStream>
#include <QVBoxLayout>
#include <twitch-eventsub-ws/chrono.hpp>  // IWYU pragma: keep - this is being used to see if we're using Howard Hinnant's date library

namespace chatterino {

constexpr int PIXMAP_WIDTH = 500;

constexpr QStringView LINK_CHATTERINO_FEATURES =
    u"https://chatterino.com/#features";
constexpr QStringView LINK_FORK_GITHUB =
    u"https://github.com/trinlol/chatterino-with-better-browser-integration";
constexpr QStringView LINK_FORK_RELEASES =
    u"https://github.com/trinlol/chatterino-with-better-browser-integration/releases";
constexpr QStringView LINK_FORK_ISSUES =
    u"https://github.com/trinlol/chatterino-with-better-browser-integration/issues";

AboutPage::AboutPage()
{
    LayoutCreator<AboutPage> layoutCreator(this);

    auto scroll = layoutCreator.emplace<QScrollArea>();
    auto widget = scroll.emplaceScrollAreaWidget();
    removeScrollAreaBackground(scroll.getElement(), widget.getElement());

    auto layout = widget.setLayoutType<QVBoxLayout>();
    {
        QPixmap pixmap;
        pixmap.load(":/settings/aboutlogo.png");

        auto logo = layout.emplace<QLabel>().assign(&this->logo_);
        logo->setPixmap(pixmap);
        if (pixmap.width() != 0)
        {
            logo->setFixedSize(PIXMAP_WIDTH,
                               PIXMAP_WIDTH * pixmap.height() / pixmap.width());
        }
        logo->setScaledContents(true);

        // Version
        auto versionInfo = layout.emplace<QGroupBox>("Version");
        {
            auto vbox = versionInfo.emplace<QVBoxLayout>();
            const auto &version = Version::instance();

            QString string =
                version.buildString() % "<br>" % version.runningString();

            if (!version.extraString().isEmpty())
            {
                string += "<br>" % version.extraString();
            }

            string +=
                "<br><br>Your settings directory is located at <a href=\"";
            string +=
                QUrl::fromLocalFile(getApp()->getPaths().settingsDirectory)
                    .toString(QUrl::FullyEncoded);
            string += "\">";
            string += getApp()->getPaths().settingsDirectory.toHtmlEscaped();
            string += "</a>.";

            auto label = vbox.emplace<QLabel>(string);
            label->setWordWrap(true);
            label->setOpenExternalLinks(true);
            label->setTextInteractionFlags(Qt::TextBrowserInteraction);
        }

        // About Chatterino Better Browser
        auto aboutFork =
            layout.emplace<QGroupBox>("About Chatterino Better Browser...");
        {
            auto l = aboutFork.emplace<QVBoxLayout>();

            // clang-format off
            l.emplace<QLabel>("Chatterino Better Browser on <a href=\"" % LINK_FORK_GITHUB % "\">GitHub</a>")->setOpenExternalLinks(true);
            l.emplace<QLabel>("Latest releases and changelog can be found <a href=\"" % LINK_FORK_RELEASES % "\">here</a>")->setOpenExternalLinks(true);
            l.emplace<QLabel>("Report bugs or request features on <a href=\"" % LINK_FORK_ISSUES % "\">GitHub Issues</a>")->setOpenExternalLinks(true);
            // clang-format on
        }

        // Original Chatterino
        auto aboutChatterino =
            layout.emplace<QGroupBox>("Original Chatterino...");
        {
            auto l = aboutChatterino.emplace<QVBoxLayout>();

            // clang-format off
            l.emplace<QLabel>("Chatterino Wiki can be found <a href=\"" % LINK_CHATTERINO_WIKI % "\">here</a>")->setOpenExternalLinks(true);
            l.emplace<QLabel>("All about Chatterino's <a href=\"" % LINK_CHATTERINO_FEATURES % "\">features</a>")->setOpenExternalLinks(true);
            l.emplace<QLabel>("Join the official Chatterino <a href=\"" % LINK_CHATTERINO_DISCORD % "\">Discord</a>")->setOpenExternalLinks(true);
            // clang-format on
        }

        // Licenses
        auto licenses =
            layout.emplace<QGroupBox>("Open source software used...");
        {
            auto form = licenses.emplace<QFormLayout>();

            addLicense(form.getElement(), "Qt Framework", "https://www.qt.io",
                       ":/licenses/qt_lgpl-3.0.txt");
            addLicense(form.getElement(), "Boost", "https://www.boost.org/",
                       ":/licenses/boost_boost.txt");
            addLicense(form.getElement(), "LibCommuni",
                       "https://github.com/communi/libcommuni",
                       ":/licenses/libcommuni_BSD3.txt");
            addLicense(form.getElement(), "OpenSSL", "https://www.openssl.org/",
                       ":/licenses/openssl.txt");
            addLicense(form.getElement(), "RapidJson", "https://rapidjson.org/",
                       ":/licenses/rapidjson.txt");
            addLicense(form.getElement(), "Pajlada/Settings",
                       "https://github.com/pajlada/settings",
                       ":/licenses/pajlada_settings.txt");
            addLicense(form.getElement(), "Pajlada/Signals",
                       "https://github.com/pajlada/signals",
                       ":/licenses/pajlada_signals.txt");
            addLicense(form.getElement(), "Pajlada/Serialize",
                       "https://github.com/pajlada/serialize",
                       ":/licenses/pajlada_serialize.txt");
            addLicense(form.getElement(), "lrucache",
                       "https://github.com/lamerman/cpp-lru-cache",
                       ":/licenses/lrucache.txt");
            addLicense(form.getElement(), "magic_enum",
                       "https://github.com/Neargye/magic_enum",
                       ":/licenses/magic_enum.txt");
            addLicense(form.getElement(), "semver",
                       "https://github.com/Neargye/semver",
                       ":/licenses/semver.txt");
            addLicense(form.getElement(), "miniaudio",
                       "https://github.com/mackron/miniaudio",
                       ":/licenses/miniaudio.txt");
#ifdef CHATTERINO_HAVE_PLUGINS
            addLicense(form.getElement(), "lua", "https://lua.org",
                       ":/licenses/lua.txt");
#endif
#ifdef CHATTERINO_WITH_CRASHPAD
            addLicense(form.getElement(), "sentry-crashpad",
                       "https://github.com/getsentry/crashpad",
                       ":/licenses/crashpad.txt");
#endif
            addLicense(form.getElement(), "Fluent icons",
                       "https://github.com/microsoft/fluentui-system-icons",
                       ":/licenses/fluenticons.txt");
#ifdef CHATTERINO_USING_NONSTD_EXPECTED
            addLicense(form.getElement(), "expected-lite",
                       "https://github.com/martinmoene/expected-lite",
                       ":/licenses/expected-lite.txt");
#endif
            addLicense(form.getElement(), "certify",
                       "https://github.com/djarek/certify",
                       ":/licenses/certify.txt");
#ifdef CHATTERINO_USING_HOWARD_HINNANTS_DATE
            addLicense(form.getElement(), "Howard Hinnant's date.h",
                       "https://github.com/HowardHinnant/date",
                       ":/licenses/howard-hinnant-date.txt");
#endif
            addLicense(form.getElement(), "{fmt}", "https://fmt.dev",
                       ":/licenses/fmtlib.txt");
            addLicense(form.getElement(), "Unicode",
                       "https://www.unicode.org/copyright.html",
                       ":/licenses/unicode.txt");
#ifdef CHATTERINO_WITH_SPELLCHECK
            addLicense(form.getElement(), "Hunspell",
                       "https://hunspell.github.io", ":/licenses/hunspell.txt");
#endif
        }

        // Attributions
        auto attributions = layout.emplace<QGroupBox>("Attributions...");
        {
            auto l = attributions.emplace<QVBoxLayout>();

            // clang-format off
            l.emplace<QLabel>("Twemoji emojis provided by <a href=\"https://github.com/twitter/twemoji\">Twitter's Twemoji</a>")->setOpenExternalLinks(true);
            l.emplace<QLabel>("Facebook emojis provided by <a href=\"https://facebook.com\">Facebook</a>")->setOpenExternalLinks(true);
            l.emplace<QLabel>("Apple emojis provided by <a href=\"https://apple.com\">Apple</a>")->setOpenExternalLinks(true);
            l.emplace<QLabel>("Google emojis provided by <a href=\"https://google.com\">Google</a>")->setOpenExternalLinks(true);
            l.emplace<QLabel>("Emoji datasource provided by <a href=\"https://www.iamcal.com/\">Cal Henderson</a> "
                              "(<a href=\"https://github.com/iamcal/emoji-data/blob/master/LICENSE\">show license</a>)")->setOpenExternalLinks(true);
            // clang-format on
        }

        // Original Chatterino maintainers & contributors
        auto contributors = layout.emplace<QGroupBox>(
            "People (Original Chatterino Maintainers & Contributors)");
        {
            auto outer = contributors.emplace<QVBoxLayout>();
            auto *note = new QLabel(
                "Chatterino Better Browser is a fork of Chatterino 2. "
                "The people listed below are the original Chatterino "
                "maintainers, collaborators, and contributors.");
            note->setWordWrap(true);
            outer->addWidget(note);

            auto l = outer.emplace<FlowLayout>();

            QFile contributorsFile(":/contributors.txt");
            if (!contributorsFile.open(QFile::ReadOnly))
            {
                assert(false && "Resources not loaded");
                qCWarning(chatterinoWidget) << "Resources not loaded";
            }

            QTextStream stream(&contributorsFile);

            QString line;

            while (stream.readLineInto(&line))
            {
                if (line.isEmpty() || line.startsWith('#'))
                {
                    continue;
                }

                if (line.startsWith(u"@header"))
                {
                    if (l->count() != 0)
                    {
                        l->addLinebreak(16);
                    }
                    auto *label = new QLabel(QStringLiteral("<h1>%1</h1>")
                                                 .arg(line.mid(8).trimmed()));
                    l->addWidget(label);
                    l->addLinebreak(6);
                    continue;
                }

                QStringList contributorParts = line.split("|");

                if (contributorParts.size() != 3)
                {
                    qCWarning(chatterinoWidget)
                        << "Missing parts in line" << line;
                    continue;
                }

                QString username = contributorParts[0].trimmed();
                QString url = contributorParts[1].trimmed();

                auto *usernameLabel =
                    new QLabel("<a href=\"" + url + "\">" + username + "</a>");
                usernameLabel->setOpenExternalLinks(true);
                usernameLabel->setToolTip(url);
                usernameLabel->setContentsMargins(0, 2, 10, 2);
                l->addWidget(usernameLabel);
            }
        }
    }

    layout->addStretch(1);
}

void AboutPage::addLicense(QFormLayout *form, const QString &name,
                           const QString &website, const QString &licenseLink)
{
    auto *a = new QLabel("<a href=\"" + website + "\">" + name + "</a>");
    a->setOpenExternalLinks(true);
    auto *b = new QLabel("<a href=\"" + licenseLink + "\">show license</a>");
    QObject::connect(
        b, &QLabel::linkActivated, [parent = this, name, licenseLink] {
            auto *window = new BasePopup(
                {
                    BaseWindow::EnableCustomFrame,
                    BaseWindow::DisableLayoutSave,
                    BaseWindow::BoundsCheckOnShow,
                },
                parent);
            window->setWindowTitle("Chatterino - License for " + name);
            window->setAttribute(Qt::WA_DeleteOnClose);
            auto *layout = new QVBoxLayout();
            auto *edit = new QTextEdit;

            QFile file(licenseLink);
            if (!file.open(QIODevice::ReadOnly))
            {
                assert(false && "License not found");
                qCWarning(chatterinoWidget)
                    << "License not found" << licenseLink;
            }
            edit->setText(file.readAll());
            edit->setReadOnly(true);

            layout->addWidget(edit);

            window->getLayoutContainer()->setLayout(layout);
            window->show();
        });

    form->addRow(a, b);
}

}  // namespace chatterino
