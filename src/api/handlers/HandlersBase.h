#ifndef HANDLERS_BASE_H
#define HANDLERS_BASE_H

#include <memory>
#include <iostream>
#include "../../ConfigManager.h"
#include <coro/coro.hpp>

class HandlersBase {
public:
    // 获取单例实例的静态方法
    static HandlersBase &getInstance() {
        static HandlersBase instance;
        return instance;
    }

    HandlersBase(const HandlersBase &) = delete;
    HandlersBase &operator=(const HandlersBase &) = delete;

    std::shared_ptr<coro::thread_pool> tp;
    std::shared_ptr<coro::io_scheduler> scheduler;
    coro::task_container<coro::thread_pool> cleanup_task_container_;

protected:
    // 构造函数设置为受保护，防止外部直接实例化
    HandlersBase()
        : tp(std::make_shared<coro::thread_pool>(coro::thread_pool::options{
              .thread_count = get_cpu_thread_count(),  // 使用动态线程数
          })),
          scheduler(coro::io_scheduler::make_shared(coro::io_scheduler::options{
              .thread_strategy = coro::io_scheduler::thread_strategy_t::spawn,
              .on_io_thread_stop_functor = [] { std::cout << "io_scheduler::event thread stop\n"; },
              .pool = coro::thread_pool::options{
                  .thread_count = get_io_thread_count(),  // 使用动态线程数
                  .on_thread_start_functor = [](size_t i) {
                      std::cout << "io_scheduler::thread_pool worker " << i << " starting\n";
                  },
                  .on_thread_stop_functor = [](size_t i) {
                      std::cout << "io_scheduler::thread_pool worker " << i << " stopping\n";
                  },
              },
              .execution_strategy = coro::io_scheduler::execution_strategy_t::process_tasks_on_thread_pool,
          })),
          cleanup_task_container_(tp) {}

    ~HandlersBase() = default;

    static unsigned int get_cpu_thread_count() {
        unsigned int num_threads = ConfigManager::getInstance().getConfig().num_threads;
        unsigned int io_threads = get_io_thread_count();
        unsigned int cpu_threads = std::max(1u, num_threads - io_threads);
        return std::max(1u, cpu_threads);  // 动态分配 CPU 线程数（至少 1 个）
    }

    static unsigned int get_io_thread_count() {
        unsigned int num_threads = ConfigManager::getInstance().getConfig().num_threads;
        return std::max(1u, num_threads / 4);  // 动态分配 I/O 线程数（至少 1 个）
    }

};

#endif // HANDLERS_BASE_H
