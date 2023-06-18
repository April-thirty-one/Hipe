#include "../include/util.h"
#include <algorithm>
#include <functional>
#include <future>

int item = 0;

void threadPrint(const char * str)
{
    std::cout << "test " << str << std::endl;
}

int currentCount()
{
    return ++item;
}

int count = 0;
myHipe::util::SpinLock locker;
void addCount1()
{
    for (size_t i = 0; i < 100000; i++) {
        locker.lock();
        count++;
        locker.unlock();
    }
}

void addCount2()
{
    for (size_t i = 0; i < 100000; i++) {
        myHipe::util::SpinLock_guard spinLock_guard(locker);
        count++;
    }
}

int main(int argc, char * args[])
{
    // 测试 sleep_for_seconds()
    myHipe::util::sleep_for_seconds(1);

    // 测试 print()
    myHipe::util::print(1, 2, 3, 4, 5);

    // 测试 repeat
    auto func = std::bind(threadPrint, "repeat 4 times");
    myHipe::util::repeat(func, 4);

    // 测试 invoke
    myHipe::util::invoke(threadPrint, "invoke");

    // 测试 timeWait()
    std::cout << "Execution time of threadPrint()" << myHipe::util::timeWait(threadPrint, "timeWait") << std::endl;

    // 测试 title
    std::cout << myHipe::util::title("test title") << std::endl;

    // 测试 strong
    std::cout << myHipe::util::strong("test strong") << std::endl;

    // 测试 class Futures
    myHipe::util::Futures<int> futures;
    futures.push_back(std::async(currentCount));
    futures.push_back(std::async(currentCount));
    std::cout << futures.size() << std::endl;
    std::vector<int> vi =  futures.get();
    std::for_each(vi.begin(), vi.end(), [] (int item) {
        std::cout << item << "   ";
    });
    std::cout << std::endl;

    // 测试 class SpinLock
    std::thread td1(addCount1);
    std::thread td2(addCount1);
    td1.join();
    td2.join();
    std::cout << "test class SpinLock -- count = " << count << std::endl;
    
    // 测试 class SpinLock_guard
    std::thread td3(addCount2);
    std::thread td4(addCount2);
    td3.join();
    td4.join();
    std::cout << "test class SpinLock_guard -- count = " << count << std::endl;

    // 测试 class SafeTask
    myHipe::util::SafeTask safeTask(std::bind(threadPrint, "SafeTask"));
    std::cout << safeTask.isSet() << std::endl;
    safeTask();
    safeTask.reset([] () {
                       std::cout << "test safeTask.reset" << std::endl;
                   });
    safeTask();
    auto func1 = std::bind(threadPrint, "safeTask");
    safeTask.reset(func1);
    safeTask();
    
    // 测试 class SafeTask
    myHipe::util::QuickTask quickTask(std::bind(threadPrint, "class QuickTask"));
    quickTask();
    quickTask.reset([] () {
                        std::cout << "test QuickTask::reset" << std::endl;
                    });
    quickTask();
    return 0;
}
