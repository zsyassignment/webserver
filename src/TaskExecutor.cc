#include "TaskExecutor.h"

TaskExecutor::TaskExecutor(size_t threadCount)
    : running_(false) {
    workers_.reserve(threadCount);
}

TaskExecutor::~TaskExecutor() {
    stop();
}

void TaskExecutor::start() {
    if (running_) return;
    running_ = true;
    size_t count = workers_.capacity();
    for (size_t i = 0; i < count; ++i) {
        workers_.emplace_back(new Thread([this]() { workerLoop(); }, "biz-worker"));
        workers_.back()->start();
    }
}

void TaskExecutor::stop() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!running_) return;
        running_ = false;
    }
    cv_.notify_all();
    for (auto& worker : workers_) {
        if (worker->started()) {
            worker->join();
        }
    }
}

void TaskExecutor::submit(std::function<void()> task) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        tasks_.push(std::move(task));
    }
    cv_.notify_one();
}

void TaskExecutor::workerLoop() {
    while (true) {
        std::function<void()> task;
        {
            std::unique_lock<std::mutex> lock(mutex_);
            cv_.wait(lock, [this]() { return !tasks_.empty() || !running_; });
            if (!running_ && tasks_.empty()) break;
            task = std::move(tasks_.front());
            tasks_.pop();
        }
        if (task) task();
    }
}
