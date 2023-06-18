#include "../../include/thread_pond/steady_pond.h"

using namespace myHipe;

util::SyncStream stream;

void foo1() {
    stream.print("call foo1");
}

void foo2(std::string name) {
    stream.print(name, " call foo2");
}

struct Functor {
    void operator()() {
        stream.print("functor executed");
    }
};

void test_submit(SteadyThreadPond & pond) 
{
    stream.print("\n", util::boundary('=', 15), util::strong("submit"), util::boundary('=', 16));

    // no return
    pond.submit(&foo1);
    pond.submit([] () -> void {
        stream.print("April Thirty say hello world.");
    });
    pond.submit(std::bind(foo2, "April Thirty"));

    pond.submit(Functor());
    // 若需要返回值
    auto ret = pond.submitForReturn([] () -> int { return 2023; });
    stream.print("return = ", ret.get());

    // 若是需要多个返回值
    int n = 5;
    util::Futures<int> futures;
    for (int i = 0; i < n; i++) {
        futures.push_back(pond.submitForReturn([i] () -> int { return i; }));
    }
    std::vector<int> results = std::move(futures.get());
    std::for_each(results.begin(), results.end(), [] (int item) -> void {
        stream.print(item);
    });
}

void test_submit_in_batch(SteadyThreadPond & pond) 
{
    stream.print("\n", util::boundary('=', 11), util::strong("submit in batch"), util::boundary('=', 11));

    int n = 5;

    // 使用 std::vector，这个 vector 必须重载 '[]'
    std::vector<util::SafeTask> vec;
    for (int i = 0; i < n; i++) {
        vec.emplace_back([i] () -> void { 
            stream.print("vector task ", i);
        });
    }
    pond.submitInBatch(vec, vec.size());

    // 也可以这样提交重复任务
    util::repeat([&] () {
        pond.submit([] () {
            stream.print("submit task");
        });
    }, 10);
}

void test_task_overflow() 
{
    stream.print("\n", util::boundary('=', 11), util::strong("task overflow"), util::boundary('=', 13));

    // 任务容量为 100
    SteadyThreadPond pond(10, 100);

    pond.setRefuseCallBack([&] () -> void {
        stream.print("task overflow !");
        std::vector<util::SafeTask> blok = std::move(pond.pullOverFlowTasks());
        stream.print("Losed ", blok.size(), " tasks");
    });

    std::vector<util::SafeTask> my_block;
    for (int i = 0; i < 101; i++) {
        my_block.emplace_back([] () -> void { util::sleep_for_milliseconds(10); });
    }
    pond.submitInBatch(my_block, my_block.size());
}

void test_other_interface(SteadyThreadPond & pond, int thread_numb) 
{
    stream.print("\n", util::boundary('=', 11), util::strong("other interface"), util::boundary('=', 13));

    util::print("enable rob tasks.");

    // 启动任务窃取机制, 减少异常阻塞线程的影响
    pond.enableStealTasks(thread_numb / 2);

    util::print("disable rob tasks");
    // 禁用这个功能
    pond.disableStealTasks();
}

int main(int argc, char * argv[])
{
    // 无限的任务容量
    SteadyThreadPond pond(8, 800);
    // 无限的任务容量不能设置 提交失败回调
    pond.setRefuseCallBack([] () -> void {  stream.print("task overflow"); });

    test_submit(pond);
    test_submit_in_batch(pond);
    test_task_overflow();
    test_other_interface(pond, 8);

    pond.waitForTasks();

    return 0; 
}