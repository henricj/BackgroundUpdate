#pragma once

class WindowsQueueWorkItemThreadPool
{
public:
    void enqueue_work(std::function<void()> work) const;
};
