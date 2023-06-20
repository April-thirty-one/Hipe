#include <algorithm>
#include "../include/myHipe.h"

int main() 
{
    // thread number = core number - 1
    int thread_numb = static_cast<int>(std::thread::hardware_concurrency()) - 1;

    myHipe::SteadyThreadPond core_pond(thread_numb, thread_numb * 10);
    // myHipe::DynamicThreadPond cach_pond(thread_numb / 2);
    myHipe::DynamicThreadPond cach_pond(std::max(thread_numb / 2, 1));
    std::atomic_int var(0);

    // 通过这个任务将溢出任务传递给 cache pond
    core_pond.setRefuseCallBack([&]() -> void {
        auto tasks = std::move(core_pond.pullOverFlowTasks());  // you can move or just copy
        cach_pond.submitInBatch(tasks, tasks.size());
        myHipe::util::print("Overflow task number = ", tasks.size());
    });

    // 溢出 3 个任务
    for (int i = 0; i < (thread_numb * 10 + 3); ++i) {
        core_pond.submit([&]() -> void {
            myHipe::util::sleep_for_milliseconds(5);
            var++;
        });
    }
    core_pond.waitForTasks();
    cach_pond.waitForTasks();

    if (var.load() == (thread_numb * 10 + 3)) {
        myHipe::util::print("All task done");
    }
}