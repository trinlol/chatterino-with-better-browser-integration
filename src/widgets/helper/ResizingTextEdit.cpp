// SPDX-FileCopyrightText: 2017 Contributors to Chatterino <https://chatterino.com>
//
// SPDX-License-Identifier: MIT

#include "widgets/helper/ResizingTextEdit.hpp"

#include "common/Common.hpp"
#include "singletons/helper/GifTimer.hpp"
#include "common/QLogging.hpp"
#include "controllers/completion/TabCompletionModel.hpp"
#include "singletons/Settings.hpp"
#include "Application.hpp"
#include "common/Channel.hpp"
#include "controllers/accounts/AccountController.hpp"
#include "controllers/emotes/EmoteController.hpp"
#include "messages/Emote.hpp"
#include "messages/Image.hpp"
#include "providers/bttv/BttvEmotes.hpp"
#include "providers/ffz/FfzEmotes.hpp"
#include "providers/seventv/SeventvEmotes.hpp"
#include "providers/twitch/TwitchAccount.hpp"
#include "providers/twitch/TwitchChannel.hpp"

#include <QMenu>
#include <QMimeData>
#include <QMimeDatabase>
#include <QObject>
#include <QSizeF>
#include <QTextBlock>
#include <QAbstractTextDocumentLayout>
#include <QTextDocument>
#include <QTextFragment>
#include <QTimer>

namespace chatterino {

EmotePtr findEmoteByName(const QString &name, const Channel *channel)
{
    if (name.isEmpty())
    {
        return nullptr;
    }

    const auto *tc = dynamic_cast<const TwitchChannel *>(channel);
    if (channel && channel->isTwitchChannel() && tc)
    {
        // 1. Local twitch emotes
        if (auto twitch = tc->localTwitchEmotes())
        {
            auto it = twitch->find(EmoteName{name});
            if (it != twitch->end())
            {
                return it->second;
            }
        }

        // 2. Access emotes (subscriber emotes for current account)
        auto user = getApp()->getAccounts()->twitch.getCurrent();
        if (user)
        {
            auto access = user->accessEmotes();
            if (*access)
            {
                auto it = (*access)->find(EmoteName{name});
                if (it != (*access)->end())
                {
                    return it->second;
                }
            }
        }

        // 3. Channel BetterTTV
        if (auto bttv = tc->bttvEmotes())
        {
            auto it = bttv->find(EmoteName{name});
            if (it != bttv->end())
            {
                return it->second;
            }
        }

        // 4. Channel FrankerFaceZ
        if (auto ffz = tc->ffzEmotes())
        {
            auto it = ffz->find(EmoteName{name});
            if (it != ffz->end())
            {
                return it->second;
            }
        }

        // 5. Channel 7TV
        if (auto seventv = tc->seventvEmotes())
        {
            auto it = seventv->find(EmoteName{name});
            if (it != seventv->end())
            {
                return it->second;
            }
        }
    }

    // 6. Global BetterTTV
    if (auto bttvG = getApp()->getBttvEmotes()->emotes())
    {
        auto it = bttvG->find(EmoteName{name});
        if (it != bttvG->end())
        {
            return it->second;
        }
    }

    // 7. Global FrankerFaceZ
    if (auto ffzG = getApp()->getFfzEmotes()->emotes())
    {
        auto it = ffzG->find(EmoteName{name});
        if (it != ffzG->end())
        {
            return it->second;
        }
    }

    // 8. Global 7TV
    if (auto seventvG = getApp()->getSeventvEmotes()->globalEmotes())
    {
        auto it = seventvG->find(EmoteName{name});
        if (it != seventvG->end())
        {
            return it->second;
        }
    }

    return nullptr;
}

ResizingTextEdit::ResizingTextEdit()
{
    auto sizePolicy = this->sizePolicy();
    sizePolicy.setHeightForWidth(true);
    sizePolicy.setVerticalPolicy(QSizePolicy::Preferred);
    this->setSizePolicy(sizePolicy);
    this->setAcceptRichText(false);

    QObject::connect(this, &QTextEdit::textChanged, this,
                     &QWidget::updateGeometry);

    QObject::connect(this, &QTextEdit::cursorPositionChanged, [this]() {
        // If tab was pressed and we're completing/replacing the current word,
        // this code will not even be called, see ResizingTextEdit::keyPressEvent

        if (!this->completionInProgress_)
        {
            return;
        }
        qCDebug(chatterinoCommon)
            << "Finishing completion because cursor moved";
        this->completionInProgress_ = false;
    });

    // Whenever the setting for emote completion changes, force a
    // refresh on the completion model the next time "Tab" is pressed
    getSettings()->prefixOnlyEmoteCompletion.connect([this] {
        this->completionInProgress_ = false;
    });

    this->setFocusPolicy(Qt::ClickFocus);
    this->installEventFilter(this);

    this->gifTimerConnection_ =
        getApp()->getEmotes()->getGIFTimer()->signal.connect([this] {
            bool anyAnimated = false;
            for (QTextBlock block = this->document()->begin(); block.isValid(); block = block.next())
            {
                for (QTextBlock::iterator it = block.begin(); !it.atEnd(); ++it)
                {
                    QTextFragment fragment = it.fragment();
                    if (fragment.isValid() && fragment.charFormat().isImageFormat())
                    {
                        QTextImageFormat imageFormat = fragment.charFormat().toImageFormat();
                        QString url = imageFormat.name();
                        if (url.startsWith("emote://"))
                        {
                            QString emoteName = url.mid(8);
                            auto emote = findEmoteByName(emoteName, this->getChannel ? this->getChannel().get() : nullptr);
                            if (emote && emote->images.getImage1()->animated())
                            {
                                if (auto pixmap = emote->images.getImage1()->pixmapOrLoad())
                                {
                                    this->document()->addResource(QTextDocument::ImageResource, QUrl(url), *pixmap);
                                    anyAnimated = true;
                                }
                            }
                        }
                    }
                }
            }

            if (anyAnimated)
            {
                this->document()->documentLayout()->update();
                this->viewport()->update();
            }
        });
}

QSize ResizingTextEdit::sizeHint() const
{
    return QSize(this->width(), this->heightForWidth(this->width()));
}

bool ResizingTextEdit::hasHeightForWidth() const
{
    return true;
}

bool ResizingTextEdit::isFirstWord() const
{
    QString portionBeforeCursor = this->textUpToPosition(this->textCursor().position());
    return !portionBeforeCursor.contains(' ');
};

int ResizingTextEdit::heightForWidth(int) const
{
    auto margins = this->contentsMargins();

    return margins.top() + this->document()->size().height() +
           margins.bottom() + 5;
}

QString ResizingTextEdit::textUnderCursor(bool *hadSpace) const
{
    auto textUpToCursor = this->textUpToPosition(this->textCursor().selectionStart());

    auto words = QStringView{textUpToCursor}.split(' ');
    if (words.size() == 0)
    {
        return QString();
    }

    bool first = true;
    QString lastWord;
    for (auto it = words.crbegin(); it != words.crend(); ++it)
    {
        auto word = *it;

        if (first && word.isEmpty())
        {
            first = false;
            if (hadSpace != nullptr)
            {
                *hadSpace = true;
            }
            continue;
        }

        lastWord = word.toString();
        break;
    }

    if (lastWord.isEmpty())
    {
        return QString();
    }

    return lastWord;
}

QString ResizingTextEdit::toPlainText() const
{
    QString result;
    QTextDocument *doc = this->document();
    for (QTextBlock block = doc->begin(); block != doc->end(); block = block.next())
    {
        if (!result.isEmpty())
        {
            result += "\n";
        }
        for (QTextBlock::iterator it = block.begin(); !it.atEnd(); ++it)
        {
            QTextFragment fragment = it.fragment();
            if (fragment.isValid())
            {
                QTextCharFormat format = fragment.charFormat();
                if (format.isImageFormat())
                {
                    QTextImageFormat imgFormat = format.toImageFormat();
                    QString name = imgFormat.name();
                    if (name.startsWith("emote://"))
                    {
                        result += name.mid(8);
                    }
                    else
                    {
                        result += fragment.text();
                    }
                }
                else
                {
                    result += fragment.text();
                }
            }
        }
    }
    return result;
}

QString ResizingTextEdit::textUpToPosition(int pos) const
{
    QString result;
    QTextDocument *doc = this->document();
    int currentPos = 0;
    for (QTextBlock block = doc->begin(); block != doc->end(); block = block.next())
    {
        if (currentPos >= pos)
        {
            break;
        }
        if (currentPos > 0)
        {
            result += "\n";
            currentPos++; // for the newline character
        }
        for (QTextBlock::iterator it = block.begin(); !it.atEnd(); ++it)
        {
            if (currentPos >= pos)
            {
                break;
            }
            QTextFragment fragment = it.fragment();
            if (fragment.isValid())
            {
                int fragmentLength = fragment.length();
                if (currentPos + fragmentLength > pos)
                {
                    int subLength = pos - currentPos;
                    QTextCharFormat format = fragment.charFormat();
                    if (format.isImageFormat())
                    {
                        QTextImageFormat imgFormat = format.toImageFormat();
                        QString name = imgFormat.name();
                        if (name.startsWith("emote://"))
                        {
                            result += name.mid(8);
                        }
                        else
                        {
                            result += fragment.text().left(subLength);
                        }
                    }
                    else
                    {
                        result += fragment.text().left(subLength);
                    }
                    currentPos = pos;
                    break;
                }
                else
                {
                    QTextCharFormat format = fragment.charFormat();
                    if (format.isImageFormat())
                    {
                        QTextImageFormat imgFormat = format.toImageFormat();
                        QString name = imgFormat.name();
                        if (name.startsWith("emote://"))
                        {
                            result += name.mid(8);
                        }
                        else
                        {
                            result += fragment.text();
                        }
                    }
                    else
                    {
                        result += fragment.text();
                    }
                    currentPos += fragmentLength;
                }
            }
        }
    }
    return result;
}

void ResizingTextEdit::insertEmote(const EmotePtr &emote)
{
    if (!emote)
    {
        return;
    }

    auto image = emote->images.getImage(1);
    if (!image)
    {
        return;
    }

    image->load();

    QString emoteName = emote->name.string;
    QString resourceUrl = "emote://" + emoteName;

    auto pixmapOpt = image->pixmapOrLoad();
    if (pixmapOpt.has_value())
    {
        this->document()->addResource(QTextDocument::ImageResource, QUrl(resourceUrl), *pixmapOpt);
    }
    else
    {
        QSize size = image->size().toSize();
        if (size.isEmpty())
        {
            size = QSize(16, 16);
        }
        QPixmap placeholder(size);
        placeholder.fill(Qt::transparent);
        this->document()->addResource(QTextDocument::ImageResource, QUrl(resourceUrl), placeholder);

        this->loadingEmotes_.append({emoteName, image});

        if (!this->loadingTimer_)
        {
            this->loadingTimer_ = new QTimer(this);
            QObject::connect(this->loadingTimer_, &QTimer::timeout, this, &ResizingTextEdit::checkLoadingEmotes);
            this->loadingTimer_->setInterval(100);
        }
        if (!this->loadingTimer_->isActive())
        {
            this->loadingTimer_->start();
        }
    }

    QTextCursor cursor = this->textCursor();
    QTextImageFormat format;
    format.setName(resourceUrl);

    auto emoteScale = getSettings()->emoteScale.getValue();
    auto emoteSize = image->size() * this->scale_ * emoteScale;
    if (emoteSize.isEmpty())
    {
        emoteSize = QSizeF(16, 16) * this->scale_ * emoteScale;
    }

    format.setHeight(emoteSize.height());
    format.setWidth(emoteSize.width());

    cursor.insertImage(format);
    cursor.insertText(" ");
    this->setTextCursor(cursor);
    this->updateGeometry();
}

void ResizingTextEdit::checkLoadingEmotes()
{
    bool anyLoaded = false;
    for (auto it = this->loadingEmotes_.begin(); it != this->loadingEmotes_.end(); )
    {
        if (it->second->loaded())
        {
            if (auto pixmap = it->second->pixmapOrLoad())
            {
                this->document()->addResource(QTextDocument::ImageResource, QUrl("emote://" + it->first), *pixmap);
                anyLoaded = true;
            }
            it = this->loadingEmotes_.erase(it);
        }
        else
        {
            ++it;
        }
    }

    if (this->loadingEmotes_.isEmpty())
    {
        this->loadingTimer_->stop();
    }

    if (anyLoaded)
    {
        this->document()->documentLayout()->update();
        this->viewport()->update();
    }
}

void ResizingTextEdit::setScale(float scale)
{
    this->scale_ = scale;
}

bool ResizingTextEdit::eventFilter(QObject *obj, QEvent *event)
{
    (void)obj;  // unused

    // makes QShortcuts work in the ResizingTextEdit
    if (event->type() != QEvent::ShortcutOverride)
    {
        return false;
    }
    auto *ev = static_cast<QKeyEvent *>(event);
    ev->ignore();
    if ((ev->key() == Qt::Key_C || ev->key() == Qt::Key_Insert) &&
        ev->modifiers() == Qt::ControlModifier)
    {
        return false;
    }
    return true;
}
void ResizingTextEdit::keyPressEvent(QKeyEvent *event)
{
    event->ignore();

    this->keyPressed.invoke(event);

    if (event->key() == Qt::Key_Space && (event->modifiers() & Qt::ControlModifier) == Qt::NoModifier)
    {
        QString word = this->textUnderCursor();
        if (!word.isEmpty())
        {
            EmotePtr emote = nullptr;
            if (this->getChannel)
            {
                auto channel = this->getChannel();
                const auto *tc = dynamic_cast<const TwitchChannel *>(channel.get());
                if (tc)
                {
                    if (auto seventv = tc->seventvEmotes())
                    {
                        auto it = seventv->find(EmoteName{word});
                        if (it != seventv->end())
                        {
                            emote = it->second;
                        }
                    }
                }
            }

            if (emote)
            {
                QTextCursor tc = this->textCursor();
                tc.setPosition(tc.position() - word.size(), QTextCursor::KeepAnchor);
                tc.removeSelectedText();
                this->setTextCursor(tc);
                this->insertEmote(emote);
                event->accept();
                return;
            }
        }
    }

    bool doComplete =
        (event->key() == Qt::Key_Tab || event->key() == Qt::Key_Backtab) &&
        (event->modifiers() & Qt::ControlModifier) == Qt::NoModifier &&
        !event->isAccepted();

    if (doComplete)
    {
        // check if there is a completer
        if (!this->completer_)
        {
            return;
        }

        QString currentCompletion = this->textUnderCursor();

        // check if there is something to complete
        if (currentCompletion.size() <= 1)
        {
            return;
        }

        // always expected to be TabCompletionModel
        auto *completionModel =
            dynamic_cast<TabCompletionModel *>(this->completer_->model());
        assert(completionModel != nullptr);

        if (!this->completionInProgress_)
        {
            // First type pressing tab after modifying a message, we refresh our
            // completion model
            this->completer_->setModel(completionModel);
            completionModel->updateResults(
                currentCompletion, this->toPlainText(),
                this->textCursor().position(), this->isFirstWord());
            this->completionInProgress_ = true;
            {
                // this blocks cursor movement events from resetting tab completion
                QSignalBlocker dontTriggerCursorMovement(this);
                this->completer_->complete();
            }
            this->textChanged();
            return;
        }

        // scrolling through selections
        if (event->key() == Qt::Key_Tab)
        {
            if (!this->completer_->setCurrentRow(
                    this->completer_->currentRow() + 1))
            {
                // wrap over and start again
                this->completer_->setCurrentRow(0);
            }
        }
        else
        {
            if (!this->completer_->setCurrentRow(
                    this->completer_->currentRow() - 1))
            {
                // wrap over and start again
                this->completer_->setCurrentRow(
                    this->completer_->completionCount() - 1);
            }
        }

        {
            // this blocks cursor movement events from updating tab completion
            QSignalBlocker dontTriggerCursorMovement(this);
            this->completer_->complete();
        }
        this->textChanged();
        return;
    }

    if (!event->text().isEmpty())
    {
        this->completionInProgress_ = false;
    }

    if (!event->isAccepted())
    {
        QTextEdit::keyPressEvent(event);
    }
}

void ResizingTextEdit::focusInEvent(QFocusEvent *event)
{
    QTextEdit::focusInEvent(event);

    if (event->gotFocus())
    {
        this->focused.invoke();
    }
}

void ResizingTextEdit::focusOutEvent(QFocusEvent *event)
{
    QTextEdit::focusOutEvent(event);

    if (event->lostFocus())
    {
        this->focusLost.invoke();
    }
}

void ResizingTextEdit::setCompleter(QCompleter *c)
{
    if (this->completer_)
    {
        QObject::disconnect(this->completer_, nullptr, this, nullptr);
    }

    this->completer_ = c;

    if (!this->completer_)
    {
        return;
    }

    this->completer_->setWidget(this);
    this->completer_->setCompletionMode(QCompleter::InlineCompletion);
    this->completer_->setCaseSensitivity(Qt::CaseInsensitive);

    QObject::connect(this->completer_,
                     static_cast<void (QCompleter::*)(const QString &)>(
                         &QCompleter::highlighted),
                     this, &ResizingTextEdit::insertCompletion);
}

void ResizingTextEdit::resetCompletion()
{
    this->completionInProgress_ = false;
}

void ResizingTextEdit::insertCompletion(const QString &completion)
{
    if (this->completer_->widget() != this)
    {
        return;
    }

    bool hadSpace = false;
    auto prefix = this->textUnderCursor(&hadSpace);

    int prefixSize = prefix.size();

    if (hadSpace)
    {
        ++prefixSize;
    }

    QTextCursor tc = this->textCursor();
    int completionStart = tc.position() - prefixSize;
    tc.setPosition(completionStart, QTextCursor::KeepAnchor);

    QString emoteName = completion.trimmed();
    EmotePtr emote = nullptr;
    if (this->getChannel)
    {
        auto channel = this->getChannel();
        if (channel)
        {
            emote = findEmoteByName(emoteName, channel.get());
        }
    }

    if (emote && (emote->homePage.string.contains("7tv") || (emote->images.getImage1() && emote->images.getImage1()->url().string.contains("7tv"))))
    {
        tc.removeSelectedText();
        this->setTextCursor(tc);
        this->insertEmote(emote);
    }
    else
    {
        tc.insertText(completion);
        this->setTextCursor(tc);
    }
    this->updateGeometry();
}

bool ResizingTextEdit::canInsertFromMimeData(const QMimeData *source) const
{
    if (source->hasImage() || source->hasFormat("text/plain"))
    {
        return true;
    }
    return QTextEdit::canInsertFromMimeData(source);
}

void ResizingTextEdit::insertFromMimeData(const QMimeData *source)
{
    if (getSettings()->imageUploaderEnabled)
    {
        if (source->hasImage())
        {
            this->imagePasted.invoke(source);
            return;
        }

        if (source->hasUrls())
        {
            bool hasUploadable = false;
            auto mimeDb = QMimeDatabase();
            for (const QUrl &url : source->urls())
            {
                QMimeType mime = mimeDb.mimeTypeForUrl(url);
                if (mime.name().startsWith("image"))
                {
                    hasUploadable = true;
                    break;
                }
            }

            if (hasUploadable)
            {
                this->imagePasted.invoke(source);
                return;
            }
        }
    }

    insertPlainText(source->text());
}

void ResizingTextEdit::contextMenuEvent(QContextMenuEvent *event)
{
    QObjectPtr<QMenu> menu{this->createStandardContextMenu(event->pos())};
    this->contextMenuRequested.invoke(menu.get(), event->pos());
    menu->exec(event->globalPos());
}

}  // namespace chatterino
