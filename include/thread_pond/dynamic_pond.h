#ifndef MYHIPE_INCLUDE_THREAD_POND_DYNAMIC_POND_H__
#define MYHIPE_INCLUDE_THREAD_POND_DYNAMIC_POND_H__

//===-- thread_pond/dynamic_pond.h - 动态线程池 -------*- C++ -*-----------===//
//
//     提供的动态的、能够扩缩容的线程池。支持批量提交任务、支持线程池吞吐任务速
// 率监测、支持无界队列。当没有任务时所有线程会被自动挂起（阻塞等待条件变量的通知）
// ，较为节约CPU资源。
//
// 线程池的组成结构: 
//     一个公共的任务队列 std::queue<myHipe::util::SafeTask>
//     多个异步线程，每个线程中都会有一个容量为 1 的任务容器，用来存放线程当前要执行的任务
//
// 线程池工作流程: 
//     创建线程池时指定初始线程池中线程的数量nums，线程池会创建 nums 个线程并阻
// 塞(因为当前没有任务)，向线程池中添加一个任务后，会通过条件变量随机通知一个阻塞
// 状态的线程，然后改线程开始工作，若是批量的添加任务，则会通知所有的线程，这时，
// 多个阻塞的线程会产生争抢任务。
//     在线程池工作的过程中，可以手动的调整线程池中线程的数量
//     删除线程，是通过调整 this->shrink_numb 的值，若是 this->shrink_nums 不是
// 0，表示要减少线程池中的线程数量，DyncmicThreadPond::worker 中有一个 if语句，
// 判断当前 this->shrink_nums 是不是 0，若是 0，则将该线程添加到 this->dead_threads 
// 中，再从 pond 中移除, (是使用并没有释放)
//     添加线程，就是创建线程后，使其在 worker 中循环
//
//===----------------------------------------------------------------------===//


#include "../header.h"

namespace myHipe
{

// ===============
//    动态线程池
// ===============
class DynamicThreadPond
{
public:
    /**
     * @brief DynamicThreadPond 的构造函数
     * @param tdNumb 初始的线程数量
    */
    explicit DynamicThreadPond(int tdNumb) {
        this->addThreads(tdNumb);
    }

    ~DynamicThreadPond() {
        if (!this->is_stop) {
            this->close();
        }
    }

public:
    /**
    * @brief 添加 一个 或 多个 线程
    * @param tdNumb 要添加的线程数量
    * 线程池会通过创建新的线程而扩大
    */
    void addThreads(int tdNumb = 1) {
        assert(tdNumb >= 0);
        this->expect_thread_numb += tdNumb;
        std::lock_guard<std::mutex> locker(this->shared_locker);
        while (tdNumb > 0) {
            std::thread td(&DynamicThreadPond::worker, this);       
                    // 这里只是创建了线程，但是没有给线程任务，任务是线程自己获得，所以是有竞争力的
            this->pond.emplace(std::make_pair<std::thread::id, std::thread>(td.get_id(), std::move(td)));   
                    // 线程池是 map 类型，方便查询，但是为什么不使用 std::unordered_map
            tdNumb -= 1;
        }
    }

    /**
     * @brief 释放 tdNumb 个线程
     * @param tdNumb 线程数量
     * 若没有足够的线程，程序会被中断
     * 释放并不会立即执行，只是通知一些线程需要释放，因此，它是非阻塞的
    */
    void delThreads(int tdNumb = 1) {
        assert((tdNumb <= this->expect_thread_numb) && (tdNumb >= 0));
        this->expect_thread_numb -= tdNumb;
        this->shrink_numb += tdNumb;
        std::lock_guard<std::mutex> locker(this->shared_locker);
        awake_cond_var.notify_all();
    }

    /**
     * @brief 关闭线程池
     * 任务队列中若是有阻塞的任务，就会抛出异常
     */
    void close() {
        this->is_stop = true;
        this->adjustThreads(0);
        this->waitForThreads();
        this->joinDeadThreads();
    }

    /**
     * @brief 调整线程池中线程的数量到目标值
     * @param target_td_numb 目标线程数量
     */
    void adjustThreads(int target_td_numb) {
        assert(target_td_numb >= 0);
        if (target_td_numb > this->expect_thread_numb) {
            this->addThreads(target_td_numb - this->expect_thread_numb);
        }
        else if (target_td_numb < this->expect_thread_numb) {
            this->delThreads(this->expect_thread_numb - target_td_numb);
        }
    }


    /**
     * 将不用的线程先加入到 this->dead_threads 中，这是为了避免刚刚释放就要申请的极端情况，循环利用资源
     * @brief 释放 this->dead_threads 中所有的线程
    */
    void joinDeadThreads() {
        std::thread td;
        while (true) {
            this->shared_locker.lock();
            if (this->dead_threads.empty() == false) {
                td = std::move(this->dead_threads.front());
                this->dead_threads.pop();
                this->shared_locker.unlock();
            }
            else {
                this->shared_locker.unlock();
                break;
            }
            td.join();
        }
    }

    /**
     * @return 获取线程池中的任务数量，正在进行中的任务也算在内
    */
    int getTasksRemain() const {        
        return this->total_tasks.load();
    }

    /**
     * @return 获得已经加载到线程池中的任务数量
    */
    int getTasksLoaded() const {
        return this->tasks_loaded.load();
    }

    /**
     * 重置线程加载的任务数量
     * @return the old value
    */
    int resetTasksLoaded() {
        return tasks_loaded.exchange(0);
    }

    /**
     * @return 当前正在工作的现场数量
    */
    int getRunningThreadNumb() const {
        return this->running_thread_numb.load();
    }

    /**
     * @return 期待正在运行的线程数量
    */
    int getExpectThreadNumb() const {
        return this->expect_thread_numb.load();
    }

    /**
     * @brief 等待线程数量的调整
    */
    void waitForThreads() {
        this->is_waiting_for_thread = true;
        std::unique_lock<std::mutex> locker(this->shared_locker);
        this->thread_cond_var.wait(locker, [this] () {
            return this->expect_thread_numb == this->running_thread_numb;
        });
        this->is_waiting_for_thread = false;
    }

    /**
     * @brief 等待线程池中的任务结束
    */
    void waitForTasks() {
        this->is_waiting_for_task = true;
        std::unique_lock<std::mutex> locker(this->shared_locker);
        this->task_done_cond_var.wait(locker, [this] () {
            return !this->total_tasks;
        });
        this->is_waiting_for_task = false;
    }

    /**
     * @brief 提交一个任务，并且没有返回值
    */
    template <typename Runnable>
    void submit(Runnable && func) {
        {
            std::lock_guard<std::mutex> locker(this->shared_locker);
            this->shared_task_queue.emplace(std::forward<Runnable>(func));
            total_tasks += 1;
        }
        awake_cond_var.notify_one();
    }

    /**
     * @brief 提交一个任务，并获得一个返回值
     * @param func 一个可运行对象
     * @return 一个 future
    */
    template <typename Runnable>
    auto submitForReturn(Runnable && func) -> std::future<typename std::result_of<Runnable()>::type> {
        using RT = typename std::result_of<Runnable()>::type;

        std::packaged_task<RT()> pack(std::forward<Runnable>(func));
        std::future<RT> future(pack.get_future());
        {
            std::lock_guard<std::mutex> locker(this->shared_locker);
            this->shared_task_queue.emplace(std::move(pack));
            total_tasks += 1;
        }
        awake_cond_var.notify_one();
        return future;
    }

    /**
     * @brief 批量的提交任务，注意: 包含任务的容器必须要重载 '[]'
     * @param container 任务容器
     * @param size container 的 size
    */
    template <typename Container>
    void submitInBatch(Container & container, size_t size) {
        {
            std::lock_guard<std::mutex> lock(this->shared_locker);
            this->total_tasks += static_cast<int>(size);
            for (size_t  i = 0; i < size; i++) {
                this->shared_task_queue.emplace(std::move(container[i]));
            }
        }
        awake_cond_var.notify_all();
    }

private:
    void notifyThreadAdjust() {
        std::lock_guard<std::mutex> locker(this->shared_locker);
        this->thread_cond_var.notify_one();
    }

    /**
     * @brief 工作线程默认循环
    */
    void worker() {
        util::SafeTask task;    // 任务容器
        this->running_thread_numb += 1;

        if (this->is_waiting_for_thread) {
            this->notifyThreadAdjust();
        }
        
        do {
            std::unique_lock<std::mutex> locker(this->shared_locker);
            this->awake_cond_var.wait(locker, [this] () {       // 调整线程池中线程的数量
                // 若是当前 任务队列 不是空，就抢任务
                // 若当前没有任务 或 有任务并 this->shrink_numb 不是0，就准备删除当前线程
                return !this->shared_task_queue.empty() || this->shrink_numb > 0;
            });

            // 接受到删除通知
            if (this->shrink_numb) {        
                // 若当前 shrink_numb 不是0，表示有待删除的线程，删除当前的线程
                this->shrink_numb -= 1;
                auto id = std::this_thread::get_id();
                this->dead_threads.emplace(std::move(this->pond[id]));  
                this->pond.erase(id);
                break;
            }

            task = std::move(this->shared_task_queue.front());
            this->shared_task_queue.pop();
            locker.unlock();

            tasks_loaded += 1;
            util::invoke(task);
            total_tasks -= 1;

            if (this->is_waiting_for_task) {
                std::lock_guard<std::mutex> lock(this->shared_locker);
                this->task_done_cond_var.notify_one();
            }
        } while (true);

        this->running_thread_numb -= 1;
        if (this->is_waiting_for_thread) {
            this->notifyThreadAdjust();
        }
    }

private:
     bool is_stop{false};                      // 是否停止线程池
     std::atomic<int> running_thread_numb{0};  // 正在运行中的线程数量
     std::atomic<int> expect_thread_numb{0};  // 期望正在运行的线程数量
     bool is_waiting_for_task{false};         // 是否正在等待任务结束
     bool is_waiting_for_thread{false};       // 线程调整是否需要调整
     std::atomic<int> total_tasks{0};         // 任务的数量
     std::queue<util::SafeTask> shared_task_queue{};  // 共享任务队列
     std::mutex shared_locker;                  // 线程的共享互斥锁
     std::condition_variable awake_cond_var{};  // 条件变量去唤醒停止的线程
     std::condition_variable task_done_cond_var{};  // 负责任务结束
     std::condition_variable thread_cond_var{};     // 线程开始或删除
     std::map<std::thread::id, std::thread> pond;   //// 动态线程池
     std::queue<std::thread> dead_threads;          // 保留不工作的线程
     std::atomic<int> shrink_numb{0};               // 线程的收缩空间
     std::atomic<int> tasks_loaded{0};  // 加载到线程中的任务数量
};

}   // !! myHipe

#endif  // !! MYHIPE_INCLUDE_DYNAMIC_POND_H__