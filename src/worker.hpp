#pragma once
#include <condition_variable>
#include <deque>
#include <functional>
#include <mutex>
#include <thread>
namespace palette {
class Worker {
public:
    using Job=std::function<void(std::stop_token)>;
    Worker(): thread_([this](std::stop_token stop) {
        for (;;) {
            Job job;
            {
                std::unique_lock lock(mutex_);
                if (!ready_.wait(lock,stop,[this]{return !jobs_.empty();})) return;
                job=std::move(jobs_.front()); jobs_.pop_front();
            }
            if(stop.stop_requested()) return;
            job(stop);
        }
    }) {}
    ~Worker() { stop(); }
    void enqueue(Job job, bool priority=false) {
        { std::lock_guard lock(mutex_); if(priority) jobs_.push_front(std::move(job)); else jobs_.push_back(std::move(job)); }
        ready_.notify_one();
    }
    void stop() { thread_.request_stop(); ready_.notify_all(); if(thread_.joinable()) thread_.join(); }
private:
    std::mutex mutex_;
    std::condition_variable_any ready_;
    std::deque<Job> jobs_;
    std::jthread thread_;
};
}
