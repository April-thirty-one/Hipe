#include <iostream>
#include "../../include/thread_pond/dynamic_pond.h"

myHipe::util::SyncStream stream;
const int thread_numb = 4;

void func1() 
{
    stream.print("call func1");
}

void test_submit_task(myHipe::DynamicThreadPond & pond) 
{
    stream.print("\n", myHipe::util::boundary('=', 15), myHipe::util::strong("submit"), myHipe::util::boundary('=', 16));

    // 不要返回值
    pond.submit([] () { stream.print("hello world"); });
    pond.submit(func1);
    std::cout << std::endl;

    // 一个返回值
    std::future<int> ret = pond.submitForReturn([] () { return 2023; });
    stream.print("return = ", ret.get());
    std::cout << std::endl;

    // 多个返回值
    int n = 5;
    myHipe::util::Futures<int> futures;
    for (int i = 0; i < n; i++) {
        futures.push_back(pond.submitForReturn([i] () { return i; }));
    }
    futures.wait();
    std::vector<int> ret1 = std::move(futures.get());
    for (auto & item : ret1) {
        stream.print("result = ", item);
    }
    pond.waitForTasks();
}

void test_submit_in_batch(myHipe::DynamicThreadPond & pond) 
{
    stream.print("\n", myHipe::util::boundary('=', 11), myHipe::util::strong("submit by batch"), myHipe::util::boundary('=', 11));

    // 使用 std::vector
    int n = 5;
    std::vector<myHipe::util::SafeTask> vTask(n);
    for (int i = 0; i < n; i++) {
        vTask[i].reset([i] () -> void { 
            stream.print("vector task ", i);
        });
    }
    pond.submitInBatch(vTask, vTask.size());
    std::cout << std::endl;

    // 也可以使用这种方式进行重复提交
    std::function<void()> func = [] () -> void { stream.print("submit task by repeat"); };
    myHipe::util::repeat([&] () -> void { pond.submit(func); }, 5);

    pond.waitForTasks();
}

void test_motify_thread_numb(myHipe::DynamicThreadPond & pond)
{
    stream.print("\n", myHipe::util::boundary('=', 11), myHipe::util::strong("modify threads"), myHipe::util::boundary('=', 11));

    pond.waitForThreads();
    stream.print("thread-numb = ", pond.getRunningThreadNumb());

    stream.print("Now we push some time consuming tasks(the count equal thread number) and delete all the threads");
    for (int i = 0; i < pond.getRunningThreadNumb(); i++) {
        pond.submit([] () -> void { myHipe::util::sleep_for_milliseconds(300); });
    }

    // 等待所有线程结束
    myHipe::util::sleep_for_milliseconds(100);
    pond.delThreads(pond.getRunningThreadNumb());

    // 还有多少线程在工作
    stream.print("Get-Running-thread-numb = ", pond.getRunningThreadNumb());
    stream.print("Get-Expect-thread-numb = ", pond.getExpectThreadNumb(), "\n");

    // 等待线程的结束
    stream.print("Wait for threads deleted ...");
    pond.waitForThreads();

    // join dead threads to recycle thread resource
    pond.joinDeadThreads();

    stream.print("Get-Running-thread-numb-again = ", pond.getRunningThreadNumb());
    stream.print("Get-Expect-thread-numb-again = ", pond.getExpectThreadNumb());

    // 任务队列阻塞
    pond.submit([] () -> void { stream.print("task 1 done"); });
    pond.submit([] () -> void { stream.print("task 2 done"); });
    pond.submit([] () -> void { stream.print("task 3 done"); });

    stream.print("\nNow sleep for two seconds and then add one thread ...");  // 2s
    myHipe::util::sleep_for_seconds(2);

    // 上面任务开始执行
    pond.addThreads(1);
    pond.waitForTasks();
    pond.delThreads(1);
    pond.waitForThreads();

    stream.print("We have deleted the only one thread and now there are no threads");
    stream.print("Now we adjust the thread number to target number");

    int target_thread_number = 3;
    pond.adjustThreads(target_thread_number);
    pond.waitForThreads();
    stream.print("thread-numb now: ", pond.getRunningThreadNumb());
}

int main(int argc, char * args[])
{
    stream.print(myHipe::util::title("Test DynamicThreadPond", 10));

    myHipe::DynamicThreadPond pond(thread_numb);
    pond.waitForThreads(); 
#if 0
    // 检查是否可以动态调整线程池中线程的数量
    pond.addThreads();
    pond.delThreads(2);
    pond.waitForThreads();
#endif 

#if 0
    // 检查是否可以正常关闭线程池
    pond.close();
#endif
    myHipe::util::print("\nthread-num = ", pond.getRunningThreadNumb());
    myHipe::util::print("tasks-remain = ", pond.getTasksRemain());

    // test_submit_task(pond);
    // test_submit_in_batch(pond);
    test_motify_thread_numb(pond);
    return 0;
}
