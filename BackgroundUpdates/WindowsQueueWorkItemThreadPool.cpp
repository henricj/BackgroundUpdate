#include "stdafx.h"

#include "WindowsQueueWorkItemThreadPool.h"
#include "Win32Exception.h"

void WindowsQueueWorkItemThreadPool::enqueue_work(std::function<void()> work) const
{
    auto p = std::make_unique<std::function<void()>>(std::move(work));

    const auto ret = ::QueueUserWorkItem([](void* arg) -> DWORD
    {
        std::unique_ptr<std::function<void()>> work{ static_cast<std::function<void()>*>(arg) };

        (*work)();

        return 0;
    },
        p.release(),
        WT_EXECUTEDEFAULT);

    Win32Exception::ThrowLastErrorIf(!ret, "Unable to queue work item");
}
