#include "stdafx.h"

#include "YetAnotherThreadPool.h"

YetAnotherThreadPool::YetAnotherThreadPool(int thread_count)
{
    if (thread_count <= 0)
        thread_count = std::thread::hardware_concurrency();

    if (thread_count < 1)
        thread_count = 1;

    workers_.reserve(thread_count);

    for (auto i = 0; i < thread_count; ++i)
        workers_.push_back(std::make_unique<WorkerThread>(*this));
}

YetAnotherThreadPool::~YetAnotherThreadPool()
{
    for (auto& w : workers_)
        w->stop();
}

YetAnotherThreadPool::WorkerThread::WorkerThread(YetAnotherThreadPool& pool): pool_(pool)
{
    thread_ = std::thread{&WorkerThread::run, this};
}

YetAnotherThreadPool::WorkerThread::~WorkerThread()
{
    stop();

    if (thread_.joinable())
        thread_.join();
}

thread_local seeded_random YetAnotherThreadPool::rng_;

void YetAnotherThreadPool::WorkerThread::enqueue_work(std::function<void()>&& work)
{
    bool was_empty;

    {
        // Scope
        std::unique_lock<std::mutex> lock(mutex_);

        if (done_)
            throw std::runtime_error("enqueue_work after done");

        was_empty = work_queue_.empty();

        work_queue_.push(move(work));
    }

    if (was_empty)
        have_work_cv_.notify_one();
}

void YetAnotherThreadPool::WorkerThread::stop()
{
    std::unique_lock<std::mutex> lock(mutex_);

    done_ = true;

    have_work_cv_.notify_all();
}

void YetAnotherThreadPool::WorkerThread::run()
{
    try
    {
        std::unique_lock<std::mutex> lock{ mutex_ };

        for (;;)
        {
            if (done_)
            {
                decltype(work_queue_) temp;

                temp.swap(work_queue_);

                lock.unlock();

                return;
            }

            if (!work_queue_.empty())
            {
                const auto v{ std::move(work_queue_.front()) };

                work_queue_.pop();

                lock.unlock();

                v();

                lock.lock();

                continue;
            }

            {
                //scope
                lock.unlock();

                auto found_work = false;

                for (auto i = 0; i < pool_.workers_.size() / 2; ++i)
                {
                    auto& worker = pool_.random_worker();

                    std::unique_lock<std::mutex> peer_lock{ worker.mutex_, std::try_to_lock };

                    if (!peer_lock.owns_lock())
                        continue;

                    if (worker.work_queue_.empty())
                        continue;

                    found_work = true;

                    const auto v{ std::move(worker.work_queue_.front()) };

                    worker.work_queue_.pop();

                    peer_lock.unlock();

                    v();

                    break;
                }

                lock.lock();

                if (found_work)
                    continue;
            }

            have_work_cv_.wait(lock);
        }

    }
    catch (...)
    {
        worker_exception_ptr_ = std::current_exception();
    }
}

int YetAnotherThreadPool::random_id() const
{
    const auto top = int(workers_.size()) - 1;

    if (top < 1)
        return 0;

    return std::uniform_int_distribution<>{0, top}(rng_);
}
