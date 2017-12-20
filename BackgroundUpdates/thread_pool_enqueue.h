#pragma once

template<class T, class F, class... Args>
auto thread_pool_enqueue(T& tp, F&& f, Args&&... args)
-> std::future<typename std::result_of<F(Args ...)>::type>
{
    using return_type = typename std::result_of<F(Args ...)>::type;

    auto task = std::make_shared<std::packaged_task<return_type()>>(
        std::bind(std::forward<F>(f), std::forward<Args>(args)...));

    auto ret = task->get_future();

    tp.enqueue_work([task] { (*task)(); });

    return std::move(ret);
}

//template<class F, class... Args>
//auto enqueue(F&& f, Args&&... args)
//    -> std::future<typename std::result_of<F(Args ...)>::type>
//{
//    using return_type = typename std::result_of<F(Args ...)>::type;

//    auto task = std::make_shared<std::packaged_task<return_type()>>(
//        std::bind(std::forward<F>(f), std::forward<Args>(args)...));

//    auto ret = task->get_future();

//    enqueue_work([task] { (*task)(); });

//    return std::move(ret);
//}


