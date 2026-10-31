// SPDX-FileCopyrightText: 2023 Contributors to Chatterino <https://chatterino.com>
//
// SPDX-License-Identifier: MIT

#include "util/IpcQueue.hpp"

#include "common/QLogging.hpp"
#include "singletons/Paths.hpp"

#define BOOST_INTERPROCESS_SHARED_DIR_FUNC
#include <boost/date_time/posix_time/posix_time.hpp>
#include <boost/interprocess/ipc/message_queue.hpp>
#include <QByteArray>
#include <QString>
#include <QtGlobal>

#include <algorithm>
#include <chrono>
#include <thread>

namespace boost_ipc = boost::interprocess;

namespace {

static const chatterino::Paths *PATHS = nullptr;

// All queue operations are deadline-bounded. The Windows implementation of
// boost::interprocess message queues emulates interprocess mutexes with
// spinning on shared memory; a process that dies while holding that mutex
// (e.g. a force-killed browser or GUI) leaves it locked forever, and the
// kernel cannot recover an emulated mutex. An unbounded send or receive on
// such a poisoned queue would spin at 100% CPU and hang the caller for the
// lifetime of the process. Every operation therefore completes (success,
// timeout, or exception) within this bound.
constexpr std::chrono::milliseconds OPERATION_TIMEOUT{250};

// A broken queue makes every receive throw immediately. Callers loop on an
// empty result, so back off before reporting the error instead of letting
// them spin at 100% CPU. Successful receives never wait for this.
constexpr std::chrono::milliseconds ERROR_BACKOFF{100};

// How often tryReplaceOrCreate retries when remove() could not delete a queue
// that a peer still holds open and the reused queue is too small.
constexpr int REPLACE_ATTEMPTS = 3;

}  // namespace


namespace boost::interprocess::ipcdetail {

void get_shared_dir(std::string &shared_dir)
{
    if (!PATHS)
    {
        assert(false && "PATHS not set");
        qCCritical(chatterinoNativeMessage)
            << "PATHS not set for shared directory";
        return;
    }
    shared_dir = PATHS->ipcDirectory.toStdString();
}

#ifdef BOOST_INTERPROCESS_WINDOWS
void get_shared_dir(std::wstring &shared_dir)
{
    if (!PATHS)
    {
        assert(false && "PATHS not set");
        qCCritical(chatterinoNativeMessage)
            << "PATHS not set for shared directory";
        return;
    }
    shared_dir = PATHS->ipcDirectory.toStdWString();
}
#endif

}  // namespace boost::interprocess::ipcdetail

namespace chatterino::ipc {

void initPaths(const Paths *paths)
{
    PATHS = paths;
}

DeliveryStatus sendMessage(const char *name, const QByteArray &data)
{
    if (data.isEmpty())
    {
        return DeliveryStatus::InvalidMessage;
    }

    try
    {
        boost_ipc::message_queue messageQueue(boost_ipc::open_only, name);

        if (data.size() >
            static_cast<qsizetype>(messageQueue.get_max_msg_size()))
        {
            return DeliveryStatus::InvalidMessage;
        }
        const auto deadline = boost::posix_time::microsec_clock::universal_time() +
                              boost::posix_time::milliseconds(
                                  OPERATION_TIMEOUT.count());
        return messageQueue.timed_send(data.data(), size_t(data.size()), 1,
                                       deadline)
                   ? DeliveryStatus::Delivered
                   : DeliveryStatus::QueueFull;
    }
    catch (boost_ipc::interprocess_exception &ex)
    {
        qCDebug(chatterinoNativeMessage)
            << "Failed to send message:" << ex.what();
        return DeliveryStatus::QueueUnavailable;
    }
}

class IpcQueuePrivate
{
public:
    IpcQueuePrivate(const char *name, size_t maxMessages, size_t maxMessageSize)
        : queue(boost_ipc::open_or_create, name, maxMessages, maxMessageSize)
    {
    }

    /// Receives into a buffer allocated once, sized to the queue's maximum
    /// message, then copies out only the bytes that arrived. Without this,
    /// every receive would allocate a full max-size buffer.
    QByteArray receiveInto(const boost::posix_time::ptime &deadline)
    {
        const auto capacity =
            static_cast<qsizetype>(this->queue.get_max_msg_size());
        if (this->buffer.size() != capacity)
        {
            // The new storage is uninitialized
            this->buffer.resize(capacity);
        }

        size_t messageSize = 0;
        unsigned int priority = 0;
        if (!this->queue.timed_receive(this->buffer.data(),
                                       size_t(this->buffer.size()), messageSize,
                                       priority, deadline))
        {
            return {};
        }
        return {this->buffer.constData(), static_cast<qsizetype>(messageSize)};
    }

    boost_ipc::message_queue queue;
    QByteArray buffer;
};

IpcQueue::IpcQueue(IpcQueuePrivate *priv)
    : private_(priv) {};
IpcQueue::~IpcQueue() = default;

std::pair<std::unique_ptr<IpcQueue>, QString> IpcQueue::tryReplaceOrCreate(
    const char *name, size_t maxMessages, size_t maxMessageSize)
{
    try
    {
        std::unique_ptr<IpcQueuePrivate> priv;
        for (int attempt = 1;; ++attempt)
        {
            boost_ipc::message_queue::remove(name);
            priv = std::make_unique<IpcQueuePrivate>(name, maxMessages,
                                                     maxMessageSize);
            // remove() fails while a peer still has a file-backed queue open
            // (Windows). open_or_create then silently reuses that queue with
            // the geometry of whoever created it, e.g. an older version that
            // used a smaller message size.
            if (priv->queue.get_max_msg_size() >= maxMessageSize ||
                attempt >= REPLACE_ATTEMPTS)
            {
                break;
            }
            priv.reset();
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
        if (priv->queue.get_max_msg_size() < maxMessageSize)
        {
            qCWarning(chatterinoNativeMessage)
                << "Could not replace IPC queue" << name
                << "- reusing existing queue with max message size"
                << priv->queue.get_max_msg_size() << "instead of"
                << maxMessageSize;
        }
        return std::make_pair(
            std::unique_ptr<IpcQueue>(new IpcQueue(priv.release())), QString());
    }
    catch (boost_ipc::interprocess_exception &ex)
    {
        return {nullptr, QString::fromLatin1(ex.what())};
    }
}

std::pair<std::unique_ptr<IpcQueue>, QString> IpcQueue::tryOpenOrCreate(
    const char *name, size_t maxMessages, size_t maxMessageSize)
{
    try
    {
        return std::make_pair(
            std::unique_ptr<IpcQueue>(new IpcQueue(
                new IpcQueuePrivate(name, maxMessages, maxMessageSize))),
            QString());
    }
    catch (boost_ipc::interprocess_exception &ex)
    {
        return {nullptr, QString::fromLatin1(ex.what())};
    }
}

bool IpcQueue::remove(const char *name)
{
    return boost_ipc::message_queue::remove(name);
}

QByteArray IpcQueue::receive()
{
    // A single long blocking receive would never return on a queue whose
    // emulated mutex was poisoned by a force-killed peer, and the caller
    // could not observe shutdown requests. Wait in bounded slices instead.
    while (true)
    {
        try
        {
            const auto deadline =
                boost::posix_time::microsec_clock::universal_time() +
                boost::posix_time::milliseconds(OPERATION_TIMEOUT.count());
            auto buf = this->private_->receiveInto(deadline);
            if (buf.isEmpty())
            {
                // timed out (or a zero-length message); keep waiting
                continue;
            }
            return buf;
        }
        catch (boost_ipc::interprocess_exception &ex)
        {
            qCDebug(chatterinoNativeMessage)
                << "Failed to receive message:" << ex.what();
            std::this_thread::sleep_for(ERROR_BACKOFF);
            return {};
        }
    }
}

QByteArray IpcQueue::receiveFor(std::chrono::milliseconds timeout)
{
    try
    {
        const auto deadline =
            boost::posix_time::microsec_clock::universal_time() +
            boost::posix_time::milliseconds(timeout.count());
        return this->private_->receiveInto(deadline);
    }
    catch (boost_ipc::interprocess_exception &ex)
    {
        qCDebug(chatterinoNativeMessage)
            << "Failed to receive message:" << ex.what();
        std::this_thread::sleep_for(std::min(timeout, ERROR_BACKOFF));
    }
    return {};
}

}  // namespace chatterino::ipc
