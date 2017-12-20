#pragma once

#include "seeded_random.h"

class YetAnotherThreadPool final
{
public:
    typedef std::function<void()> handler_type;

    explicit YetAnotherThreadPool(int thread_count = 0);
    ~YetAnotherThreadPool();

    void enqueue_work(std::function<void()>&& work)
    {
        random_worker().enqueue_work(std::move(work));
    }

    void stop() const
    {
        for (auto& w : workers_)
            w->stop();
    }
private:
    class WorkerThread
    {
    public:
        explicit WorkerThread(YetAnotherThreadPool& pool);
        ~WorkerThread();

        void enqueue_work(std::function<void()>&& work);

        void stop();

    private:
        bool done_ = false;
        std::mutex mutex_;
        std::queue<std::function<void()>> work_queue_;
        std::condition_variable have_work_cv_;
        YetAnotherThreadPool& pool_;

        std::thread thread_;
        std::exception_ptr worker_exception_ptr_;

        void run();
    };

    int random_id() const;

    WorkerThread& random_worker()
    {
        return *workers_[random_id()];
    }

    std::vector<std::unique_ptr<WorkerThread>> workers_;

    static thread_local seeded_random rng_;

    friend class WorkerThread;
};
