#pragma once

#ifdef _WIN32
#include "CoInitializeHandle.h"
#endif

template<int MaxHandlers = 30>
class HandlerThread final
{
public:
    typedef std::function<void()> handler_type;

    HandlerThread()
    {
        thread_ = std::thread{ &HandlerThread::run, this };
    }

    void request_stop()
    {
        std::lock_guard<std::mutex> lock{ mutex_ };

        done_ = true;

        have_work_cv_.notify_all();
    }

    ~HandlerThread()
    {
        request_stop();

        if (thread_.joinable())
            thread_.join();
    }

    template<class F, class... Args>
    auto enqueue_work(F&& f, Args&&... args)
        -> std::future<typename std::result_of<F(Args ...)>::type>
    {
        using return_type = typename std::result_of<F(Args ...)>::type;

        auto task = std::make_shared<std::packaged_task<return_type()>>(
            std::bind(std::forward<F>(f), std::forward<Args>(args)...));

        auto ret = task->get_future();

        bool was_empty;

        {
            // Scope
            std::unique_lock<std::mutex> lock(mutex_);

            if (done_)
                throw std::runtime_error("enqueue_work after done");

            was_empty = work_queue_.empty();

            work_queue_.emplace([task] { (*task)(); });
        }

        if (was_empty)
            have_work_cv_.notify_one();

        return std::move(ret);
    }

#if 0
    //template<typename R>
    //std::future<R> queue_work(std::function<void()> work)
    //{
    //    std::lock_guard<std::mutex> lock{ mutex_ };

    //    if (done_)
    //        throw std::exception("Worker not running");

    //    auto p = work_queue_.emplace(std::packaged_task<R>(work));

    //    return std::move(p.get_future());
    //}

    //template<>
    //std::future<void> queue_work(std::function<void()> work)
    //{
    //    std::lock_guard<std::mutex> lock{ mutex_ };

    //    if (done_)
    //        throw std::exception("Worker not running");

    //    auto x = std::make_shared<std::packaged_task< void() >>(work);

    //    //auto p = work_queue_.emplace(std::packaged_task<void>(work));
    //    work_queue_.emplace([x]() { (*x)(); });

    //    have_work_cv_.notify_one();

    //    return std::move(x->get_future());
    //}
#endif // 0

    bool request_signal(int signal)
    {
        std::lock_guard<std::mutex> lock{ mutex_ };

        if (done_)
            return false;

        if (flags_.test(signal))
            return;

        flags_.set(signal);

        have_work_cv_.notify_one();

        return true;
    }

    int add_signal(const handler_type handler) noexcept
    {
        std::lock_guard<std::mutex> lock{ mutex_ };

        if (done_)
            return -1;

        const auto p = find(begin(handlers_), end(handlers_), nullptr);

        if (p == end(handlers_))
            return -1;

        *p = handler;

        auto index = p - begin(handlers_);

        flags_.reset(index);

        return index;
    }

    bool remove_signal(const handler_type handler) noexcept
    {
        std::lock_guard<std::mutex> lock{ mutex_ };

        auto p = find(begin(handlers_), end(handlers_), handler);

        if (p == end(handlers_))
            return false;

        *p = nullptr;

        auto index = p - begin(handlers_);

        flags_.reset(index);

        return true;
    }

    bool is_running() const noexcept
    {
        return !done_ && thread_.joinable();
    }

    void verify_on_thread() const;
private:
    std::thread thread_;

    bool done_ = false;
    std::bitset<MaxHandlers> flags_;
    std::vector<handler_type> handlers_{ MaxHandlers };
    std::vector<std::exception_ptr> handler_exceptions_{ MaxHandlers };

    std::mutex mutex_;
    std::queue<std::function<void()>> work_queue_;
    std::condition_variable have_work_cv_;

    void run()
    {
#ifdef _WIN32
        const CoInitializeWrapper co_wrapper{ COINIT_MULTITHREADED };

        if (FAILED(co_wrapper.get()))
            return;
#endif // _WIN32

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
                const auto v{ move(work_queue_.front()) };

                work_queue_.pop();

                lock.unlock();

                v();

                lock.lock();
            }
            else if (flags_.any())
            {
                auto pending_flags = flags_;
                flags_.reset();

                lock.unlock();

                for (auto i = 0; i < pending_flags.size(); ++i)
                {
                    if (!pending_flags.test(i))
                        continue;

                    auto handler = handlers_[i];

                    if (!handler)
                        continue;

                    try
                    {
                        handler();
                    }
                    catch (...)
                    {
                        handler_exceptions_[i] = std::current_exception();
                    }
                }

                lock.lock();
            }
            else
                have_work_cv_.wait(lock);
        }
    }
    };

template<int MaxHandlers>
void HandlerThread<MaxHandlers>::verify_on_thread() const
{
    if (std::this_thread::get_id() == thread_.get_id())
        return;

    throw std::exception("Operation must execute on worker thread");
}
