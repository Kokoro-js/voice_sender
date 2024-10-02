#ifndef HANDLERS_H
#define HANDLERS_H

#include <memory>
#include <string>
#include <unordered_map>
#include "../../DownloadManager/DownloadManager.h"
#include "Request.pb.h"
#include "Response.pb.h"

class Handlers {
public:
    // 获取单例实例的静态方法
    static Handlers &getInstance() {
        static Handlers instance;
        return instance;
    }

    // 删除拷贝构造函数和赋值操作符，防止复制单例实例
    Handlers(const Handlers &) = delete;

    Handlers &operator=(const Handlers &) = delete;

    std::shared_ptr<coro::thread_pool> tp;
    std::shared_ptr<coro::io_scheduler> scheduler;
    coro::task_container<coro::thread_pool> cleanup_task_container_;
    std::unordered_map<std::string, DownloadManager*> instanceMap;

    void startStreamHandler(const OMNI::StartStreamPayload *data);
    void stopStreamHandler(const OMNI::RemoveStreamPayload *data);

private:
    // 私有化构造函数，防止外部实例化
    Handlers()
        : tp(std::make_shared<coro::thread_pool>(coro::thread_pool::options{.thread_count = 4})),
          scheduler(coro::io_scheduler::make_shared(coro::io_scheduler::options{
              .thread_strategy = coro::io_scheduler::thread_strategy_t::spawn,
              .on_io_thread_stop_functor = [] { std::cout << "io_scheduler::process event thread stop\n"; },
              .pool =
              coro::thread_pool::options{
                  .thread_count = 1,
                  .on_thread_start_functor = [](size_t i) {
                      std::cout << "io_scheduler::thread_pool worker " << i << " starting\n";
                  },
                  .on_thread_stop_functor = [](size_t i) {
                      std::cout << "io_scheduler::thread_pool worker " << i << " stopping\n";
                  },
              },
              .execution_strategy = coro::io_scheduler::execution_strategy_t::process_tasks_on_thread_pool
          })),
          cleanup_task_container_(tp) {}

    // 私有化析构函数，确保单例对象在程序结束时销毁
    ~Handlers() = default;
};

#endif // HANDLERS_H
