#pragma once

template<class T, int Align>
class BufferPool final
{
    // Should we pull in boost instead...?

    std::mutex lock_;
    std::stack<std::unique_ptr<T>> free_;
public:
    class Releaser final
    {
    public:
        void operator()(T* p) const
        {
            pool_.release(p);
        }
    private:
        explicit Releaser(BufferPool& pool) : pool_(pool)
        { }
        BufferPool & pool_;
        friend class BufferPool;
    };

    typedef std::unique_ptr<T, std::function<void(T*)>> unique_ptr_type;

    BufferPool(const int buffer_count);
    BufferPool() = delete;
    ~BufferPool();

    unique_ptr_type allocate();
    void release(unique_ptr_type buffer) { buffer.reset(); }
private:
    Releaser releaser_;
    void release(T* p);

    static void* allocate_raw()
    {
#if _WIN32
        return _aligned_malloc(sizeof(T), Align);
#else
        return std::aligned_alloc(Align, sizeof(T));
#endif
    }
    static void free_raw(void* p) noexcept
    {
#if _WIN32
        _aligned_free(p);
#else
        std::aligned_free(p);
#endif
    }

    friend class Releaser;
};

template<class T, int Align = 32>
BufferPool<T, Align>::BufferPool(const int buffer_count) : releaser_(*this)
{
    for (auto i = 0; i < buffer_count; ++i)
    {
        free_.push(std::unique_ptr<T>{new (allocate_raw()) T});
    }
}

template<class T, int Align = 32>
BufferPool<T, Align>::~BufferPool()
{
    decltype(free_) temp;
    {
        std::lock_guard<std::mutex> lock{ lock_ };

        temp.swap(free_);
    }

    while (!temp.empty())
    {
        auto p = std::move(temp.top());

        temp.pop();

        p->~T();

        free_raw(p.release());
    }
}

template<class T, int Align = 32>
typename BufferPool<T, Align>::unique_ptr_type BufferPool<T, Align>::allocate()
{
    std::unique_ptr<T> p;

    {//Scope
        std::lock_guard<std::mutex> lock{ lock_ };

        if (free_.empty())
            return { nullptr, [](T*) { } };

        p = std::move(free_.top());

        free_.pop();
    }

    p->reset();

    return { p.release(), [this](T* p) { release(p); } };
}

template<class T, int Align = 32>
void BufferPool<T, Align>::release(T* p)
{
    std::lock_guard<std::mutex> lock{ lock_ };

    free_.push(std::unique_ptr<T>(p));
}

