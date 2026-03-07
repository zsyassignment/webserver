#pragma once

#include <condition_variable>
#include <functional>
#include <mutex>
#include <queue>
#include <vector>

#include "Thread.h"

class TaskExecutor {
public:
    explicit TaskExecutor(size_t threadCount = 4);
    ~TaskExecutor();

    void start();
    void stop();
    void submit(std::function<void()> task);

private:
    void workerLoop();

    std::vector<std::unique_ptr<Thread>> workers_;
    std::queue<std::function<void()>> tasks_;
    std::mutex mutex_;
    std::condition_variable cv_;
    bool running_;
};
