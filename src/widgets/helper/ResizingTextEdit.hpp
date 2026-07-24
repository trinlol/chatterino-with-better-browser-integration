// SPDX-FileCopyrightText: 2017 Contributors to Chatterino <https://chatterino.com>
//
// SPDX-License-Identifier: MIT

#pragma once

#include <pajlada/signals/scoped-connection.hpp>
#include <pajlada/signals/signal.hpp>
#include <QCompleter>
#include <QKeyEvent>
#include <QTextEdit>

class QTimer;

namespace chatterino {

class Channel;
struct Emote;
using EmotePtr = std::shared_ptr<const Emote>;

EmotePtr findEmoteByName(const QString &name, const Channel *channel);
EmotePtr findSeventvEmoteByName(const QString &name, const Channel *channel);

class ResizingTextEdit : public QTextEdit
{
public:
    ResizingTextEdit();

    QSize sizeHint() const override;

    bool hasHeightForWidth() const override;
    bool isFirstWord() const;

    pajlada::Signals::Signal<QKeyEvent *> keyPressed;
    pajlada::Signals::NoArgSignal focused;
    pajlada::Signals::NoArgSignal focusLost;
    pajlada::Signals::Signal<const QMimeData *> imagePasted;
    pajlada::Signals::Signal<QMenu *, QPoint> contextMenuRequested;

    void setCompleter(QCompleter *c);
    /**
     * Resets a completion for this text if one was is progress.
     * See `completionInProgress_`.
     */
    void resetCompletion();

    void insertEmote(const EmotePtr &emote);
    void setScale(float scale);
    QString toPlainText() const;
    QString textUpToPosition(int pos) const;
    std::function<std::shared_ptr<Channel>()> getChannel;
    QString textUnderCursor(bool *hadSpace = nullptr) const;

protected:
    int heightForWidth(int) const override;
    void keyPressEvent(QKeyEvent *event) override;

    void focusInEvent(QFocusEvent *event) override;
    void focusOutEvent(QFocusEvent *event) override;

    bool canInsertFromMimeData(const QMimeData *source) const override;
    void insertFromMimeData(const QMimeData *source) override;

    void contextMenuEvent(QContextMenuEvent *event) override;

private:
    QCompleter *completer_ = nullptr;
    /**
     * This is true if a completion was done but the user didn't type yet,
     * and might want to press `Tab` again to get the next completion
     * on the original text.
     *
     * For example:
     *
     * input: "pog"
     * `Tab` pressed:
     *   - complete to "PogBones"
     *   - retain "pog" for next completion
     *   - set `completionInProgress_ = true`
     * `Tab` pressed again:
     *   - complete ["pog"] to "PogChamp"
     *
     * [other key] pressed or cursor moved - updating the input text:
     *   - set `completionInProgress_ = false`
     */
    bool completionInProgress_ = false;

    bool eventFilter(QObject *obj, QEvent *event) override;

    QTimer *loadingTimer_ = nullptr;
    QList<std::pair<QString, std::shared_ptr<class Image>>> loadingEmotes_;
    pajlada::Signals::ScopedConnection gifTimerConnection_;
    float scale_ = 1.F;

private Q_SLOTS:
    void insertCompletion(const QString &completion);
    void checkLoadingEmotes();
};

}  // namespace chatterino
