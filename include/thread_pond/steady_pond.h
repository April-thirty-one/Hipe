#ifndef MYHIPE_THREAD_POND_STEADY_POND_H__
#define MYHIPE_THREAD_POND_STEADY_POND_H__

//===-- thread_pond/steady_pond.h - 稳定线程池 -------*- C++ -*-----------===//
//
//      Hipe-Steady是Hipe提供的稳定的、具有固定线程数的线程池。支持批量提交任务
// 和批量执行任务、支持有界任务队列和无界任务队列、支持池中线程的任务窃取机制。
// 任务溢出时支持注册回调并执行或者抛出异常。
//
//     myHipe-Steady所调用的线程类DqThread为每个线程都分配了公开任务队列、缓
// 冲任务队列和控制线程的同步变量（thread -local机制），尽量降低乒乓缓存和线程
// 同步对线程池性能的影响。工作线程通过队列替换批量下载公开队列的任务到缓冲队列
// 中执行。生产线程则通过公开任务队列为工作线程分配任务（采用了一种优于轮询的负载
// 均衡机制）。公开队列和缓冲队列（或说私有队列）替换的机制进行读写分离，再通过
// 加轻锁（C++ 11原子量实现的自旋锁）的方式极大地提高了线程池的性能。

//     由于其底层的实现机制，myHipe-Steady适用于稳定的（避免超时任务阻塞线程）、
// 任务量大（任务传递的优势得以体现）的任务流。也可以说myHipe-Steady适合作为核
// 心线程池（能够处理基准任务并长时间运行），而当可以定制容量的,yHipe-Steady面临
// 任务数量超过设定值时 —— 即任务溢出时，我们可以通过定制的回调函数拉取出溢出的任
// 务，并把这些任务推到我们的动态线程池DynamicThreadPond中。在这个情景中，
// DynamicThreadPond或许可以被叫做CacheThreadPond缓冲线程池。
//
// 结构:
//     建立线程池时指定线程池中线程的数量，主线程负责计算出当前线程池中哪个线程
// 最闲，再向该线程的 public_queue 中添加任务，注意，在steady_pond 中的每个线程
// 都有两个容量大于 1 的任务队列( public_queue, buffer_queue )，线程执行任务时
// 执行的是 buffer_queue 中的任务，主线程添加任务时添加到 public_queue 中，这样
// 就减少了主线程和异步线程之间的竞争，竞争转变为 线程内部 buffer_queue 从 
// public_queue 取出任务，和 主线程要向 public_queue 中添加任务 两者的竞争，但是
// 因为 两个相同的 stl容器 之间交换内部元素使用 std::vector<>::swap(std::vector<>)
// 时间复杂度为 O(1), 在加上这个交换使用的不是 std::mutex 而是 自定义的自旋锁，所以
// 竞争的激烈程度远远的小于 balanced_pond
// 
//===----------------------------------------------------------------------===//

#include "../header.h"

    namespace myHipe {

//=======================================//
// 支持双端队列替换算法的 线程对象
//=======================================//
class DqThread : public ThreadBase
{
public:
    /**
    * @brief 执行(this->buffer_queue)中的第一个任务
    */
    void runTask() {
        while (!this->buffer_task_queue.empty()) {
            util::invoke(this->buffer_task_queue.front());
            this->buffer_task_queue.pop();
            this->task_numb -= 1;
        }
    }

    /**
     * @brief 尝试从 this->public_queue 中加载任务到 this->buffer_queue 中
    */
    bool tryLoadTask() {
        this->task_queue_locker.lock();
        this->public_task_queue.swap(this->buffer_task_queue);
        this->task_queue_locker.unlock();

        return !this->buffer_task_queue.empty();
    }

    /**
     * @brief 尝试从其他线程中获得任务
     * @param another 另一个线程
    */
    bool tryGiveTasksToAnother(DqThread & another) {
        if (this->task_queue_locker.try_lock()) {
            if (!this->public_task_queue.empty()) {
                auto numb = this->public_task_queue.size();
                this->public_task_queue.swap(another.buffer_task_queue);
                this->task_queue_locker.unlock();
                this->task_numb -= static_cast<int>(numb);
                another.task_numb += static_cast<int>(numb);
                return true;
            }
            else {
                this->task_queue_locker.unlock();
                return false;
            }
        }
        return false;
    }

    /**
     * @brief 添加一个任务到任务队列中
    */
    template <typename T>
    void enqueue(T && tarTask) {
        util::SpinLock_guard lock(this->task_queue_locker);
        this->public_task_queue.emplace(std::forward<T>(tarTask));
        this->task_numb += 1;
    }

    /**
     * @brief 添加多个任务到任务队列中
    */
    template <typename Container>
    void enqueue(Container & container, size_t size) {
        util::SpinLock_guard locker(this->task_queue_locker);
        for (size_t i = 0; i < size; i++) {
            this->public_task_queue.emplace(std::move(container[i]));
            this->task_numb += 1;
        }
    }

private:
    std::queue<util::SafeTask> public_task_queue;
    std::queue<util::SafeTask> buffer_task_queue;
    util::SpinLock task_queue_locker{};
};

//=======================================//
//              稳定线程池
// 支持任务窃取 和 批量提交任务
//=======================================//
class SteadyThreadPond : public FixedThreadPond<DqThread>
{
public:
    /**
     * @param thread_numb 固定线程的数量
     * @param task_capacity 线程池的任务容量
    */
    explicit SteadyThreadPond(int thread_numb = 0, int task_capacity = HipeUnlimited) : FixedThreadPond(thread_numb, task_capacity) {
        this->threads.reset(new DqThread[this->thread_numb]);
        for (int i = 0; i < this->thread_numb; i++) {
            this->threads[i].bindHandle(std::thread(&SteadyThreadPond::worker, this, i));
        }
    }

    ~SteadyThreadPond() override = default;

private:
    void worker(int index) {
        DqThread & self = this->threads[index];

        while (!this->is_stop) {
            // 若任务队列中没有任务了
            if (self.notTask()) {
                // 主线程有通知 要 等待线程池执行完线程池内部的任务
                if (self.isWaiting()) {
                    self.notifyTaskDone();
                    std::this_thread::yield();
                    continue;
                }

                // 窃取任务
                if (this->enable_steal_tasks) {
                    for (int i = index, j = 0; j < this->max_steal; j++) {
                        util::recyclePlus(i, 0, this->thread_numb);
                        if (this->threads[i].tryGiveTasksToAnother(self)) {
                            self.runTask();     // 和 balanced_pond 不同，这里是直接将 this->buffer_queue 中所有任务都执行
                            break;
                        }
                    }
                    if (!self.notTask() || self.isWaiting()) {
                        continue;
                    }
                }
                std::this_thread::yield();
            }
            else {
                if (self.tryLoadTask()) {
                    self.runTask();
                }
            }
        }
    }
};

}  // !! end namespace myHipe

#endif // !! MYHIPE_THREAD_POND_STEADY_POND_H__