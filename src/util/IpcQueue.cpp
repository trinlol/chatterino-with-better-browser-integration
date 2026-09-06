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

#include <chrono>

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

    boost_ipc::message_queue queue;
};

IpcQueue::IpcQueue(IpcQueuePrivate *priv)
    : private_(priv) {};
IpcQueue::~IpcQueue() = default;

std::pair<std::unique_ptr<IpcQueue>, QString> IpcQueue::tryReplaceOrCreate(
    const char *name, size_t maxMessages, size_t maxMessageSize)
{
    try
    {
        boost_ipc::message_queue::remove(name);
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
            auto *d = this->private_.get();

            QByteArray buf;
            // The new storage is uninitialized
            buf.resize(static_cast<qsizetype>(d->queue.get_max_msg_size()));

            size_t messageSize = 0;
            unsigned int priority = 0;
            const auto deadline =
                boost::posix_time::microsec_clock::universal_time() +
                boost::posix_time::milliseconds(OPERATION_TIMEOUT.count());
            if (!d->queue.timed_receive(buf.data(), buf.size(), messageSize,
                                        priority, deadline))
            {
                continue;
            }

            // truncate to the initialized storage
            buf.truncate(static_cast<qsizetype>(messageSize));
            return buf;
        }
        catch (boost_ipc::interprocess_exception &ex)
        {
            qCDebug(chatterinoNativeMessage)
                << "Failed to receive message:" << ex.what();
            return {};
        }
    }
}

QByteArray IpcQueue::receiveFor(std::chrono::milliseconds timeout)
{
    try
    {
        auto *d = this->private_.get();
        QByteArray buf;
        buf.resize(static_cast<qsizetype>(d->queue.get_max_msg_size()));

        size_t messageSize = 0;
        unsigned int priority = 0;
        const auto deadline =
            boost::posix_time::microsec_clock::universal_time() +
            boost::posix_time::milliseconds(timeout.count());
        if (!d->queue.timed_receive(buf.data(), buf.size(), messageSize,
                                    priority, deadline))
        {
            return {};
        }
        buf.truncate(static_cast<qsizetype>(messageSize));
        return buf;
    }
    catch (boost_ipc::interprocess_exception &ex)
    {
        qCDebug(chatterinoNativeMessage)
            << "Failed to receive message:" << ex.what();
    }
    return {};
}

}  // namespace chatterino::ipc
