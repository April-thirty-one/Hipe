#ifndef MYHIPE_INCLUDE_THREAD_POND_BALANCED_POND_H__
#define MYHIPE_INCLUDE_THREAD_POND_BALANCED_POND_H__

//===-- thread_pond/balanced_pond.h - 均衡线程池 -------*- C++ -*-----------===//
//     Hipe-Balanced在异步线程与主线程之间竞争次数较多的时候性能会有所下降，同时
// 其批量提交接口的表现也会有所下降，甚至有可能低于其提交单个任务的接口（具体还要
// 考虑任务类型等许多复杂的因素）。但是由于线程类中只有一条任务队列，因此所有任务
// 都是可以被窃取的。这也导致Hipe-Balance在面对不稳定的任务流时（可能会有超时任务）
// 具有更好的表现。
//
// 结构：
//     创建线程池时指定线程池中线程的数量，主线程负责计算出线程池中哪个线程当下最
// 闲，在向该线程的任务队列添加任务，注意：和 Dyncmic_pond 不同的是，balanced_pond
// 中的每一个线程都有一个容量大于1的任务队列，线程执行任务时，从自己的任务队列中取
// 出任务，所以Balanced_pond 中的线程与线程之间没有竞争
//     当前线程没有任务后，若开启了窃取机制，则该线程会尝试从线程池的其他线程中获取
// 线程（这种方式说是 '窃取' 并不合适，'施舍' 更好）
//     Balanced_pond 的竞争主要来自于 主线程 和 异步线程 之间的竞争，因为异步线程
// 在从自己的任务队列中加载任务时，可能同时主线程要向这个任务队列添加任务，或者反之，
// 这就造成了竞争
//===----------------------------------------------------------------------===//

#include <iterator>
#include <thread>
#include "../header.h"

namespace myHipe
{

//=======================================//
//  OqThread 是 Balanced_pond 的基础线程
// 成员变量：
//          util::SafeTask task;                        // 当前正在执行的任务
//          std::queue<util::SafeTask> task_queue;      // 当前线程的中任务队列
//          std::mutex task_queue_locker;               // 任务队列专用锁
//          bool is_wait{false};                        // 当前线程是否工作，若是 true，表示要自己任务队列中的任务全部执行完
//          std::thread handle;                         // 处理任务的线程
//          std::atomic<int> task_numb{0};              // 任务的数量(算上正在执行的任务)
//          std::condition_variable task_done;          // 当前任务结束，通知
//          std::mutex locker;                          // 互斥锁
// 成员方法:
//          bool tryGiveTaskToOther(OqThread & another)
//          void enqueue(T&& tarTask)
//          void enqueue(Container & container)
//          void runTask()
//          bool tryLoadTask()
//=======================================//
class OqThread : public ThreadBase
{
public:
    /**
     * @brief 尝试将当前线程的一个任务交给另外一个线程
     * @param other 另一个线程
     * @return 若成功 -- true，反之
     */
    bool tryGiveTaskToOther(OqThread & another) {
        if (this->task_queue_locker.try_lock()) {
            if (!this->task_queue.empty()) {
                another.task = std::move(this->task_queue.front());
                this->task_queue.pop();
                this->task_queue_locker.unlock();
                this->task_numb -= 1;
                another.task_numb += 1;
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
        this->task_queue_locker.lock();
        this->task_queue.emplace(std::forward<T>(tarTask));
        this->task_numb += 1;
        this->task_queue_locker.unlock();
    }

    /**
     * @brief 添加多个任务到任务队列中
     * @param container 存储多个任务的容器
     * @param size container 的 size
    */
    template <typename Container>
    void enqueue(Container & container, size_t size) {
        this->task_queue_locker.lock();
        for (size_t i = 0; i < size; i++) {
            this->task_queue.emplace(std::move(container[i]));
            this->task_numb += 1;
        }
        this->task_queue_locker.unlock();
    }

    /**
     * @param 运行任务
    */
    void runTask() {
        util::invoke(this->task);
        this->task_numb -= 1;
    }

    /**
     * @brief 从自己的任务队列中加载任务 
    */
    bool tryLoadTask() {
        this->task_queue_locker.lock();
        if (!this->task_queue.empty()) {
            this->task = std::move(this->task_queue.front());
            this->task_queue.pop();
            this->task_queue_locker.unlock();
            return true;
        }
        else {
            this->task_queue_locker.unlock();
            return false;
        }
    }

   private:
    util::SafeTask task;
    std::queue<util::SafeTask> task_queue;
    std::mutex task_queue_locker;
};

// ======================================


class BalancedThreadPond : public FixedThreadPond<OqThread>
{
public:
    /**
     * @param thread_numb 固定线程的数量
     * @param task_capactiry 线程池中任务的容量，默认是 unlimited
    */
    explicit BalancedThreadPond(int thread_numb, int task_capactiry = HipeUnlimited) 
        : FixedThreadPond(thread_numb, task_capactiry) {
        // 创建线程
        this->threads.reset(new OqThread[this->thread_numb]);

        for (int i = 0; i < this->thread_numb; i++) {
            this->threads[i].bindHandle(std::thread(&BalancedThreadPond::worker, this, i));
        }
    }

    // 在基类 FixedThreadPond 对线程池中的线程进行集体释放
    ~BalancedThreadPond() override = default;

private:
    void worker(int index) {
        OqThread & self = this->threads[index];        

        while (this->is_stop == false) {
            // 若当前任务队列中也没有任务
            if (self.notTask()) {
                // 若是当前 self.isWaiting 返回true，表示 主线程调用了 waitForTask 方法，要等待当前线程执行完现有的任务, 然后在这里循环等待其他的线程结束自己的任务 
                if (self.isWaiting()) {
                    self.notifyTaskDone();  // 没有任务了，进行通知
                    std::this_thread::yield();
                    continue;
                }

                // 程序进行到这里，表示任务队列为空，但是主线程并没有要停止该线程的意图, 所以 从其他线程中 窃取 任务
                if (this->enable_steal_tasks) {
                    for (int i = 0, j = 0; j < this->max_steal; j++) {
                        util::recyclePlus(i, 0, this->thread_numb);
                        if (this->threads[i].tryGiveTaskToOther(self)) {
                            self.runTask();
                            break;
                        }
                    }
                    if (!self.notTask() || self.isWaiting()) {
                        // 若有任务，去执行文物
                        // 若没有任务，但是 this->is_wait 是 true，转到 上上上个 if语句 等待 notifyTaskDone 信号
                        continue;
                    }
                }
                std::this_thread::yield();
            }
            else {
                // 尝试加载自己任务队列中的任务
                if (self.tryLoadTask()) {
                    // 因为有任务窃取机制，所以上一刻有任务，下一刻可能就没有任务了
                    self.runTask();
                }
            }
        }
    }
};

}   // !! myHipe

#endif  // MYHIPE_INCLUDE_THREAD_POND_BALANCED_POND_H__