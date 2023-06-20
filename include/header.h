#ifndef MYHIPE_INCLUDE_HEADER_H__
#define MYHIPE_INCLUDE_HEADER_H__

#include "./util.h"

#include <iostream>
#include <stdexcept>
#include <type_traits>
#include <utility>
#include <thread>
#include <utility>
#include <cassert>
#include <mutex>
#include <condition_variable>
#include <future>
#include <atomic>
#include <queue>
#include <map>

namespace myHipe
{

// ======================
//        化名
// ======================
static const int HipeUnlimited = 0;

// ======================
//      线程异常类
// ======================
class ThreadPoolError : public std::exception
{
public:
    explicit ThreadPoolError(std::string str) : message(str) {}

    const char * what() const noexcept override {
        return message.data();
    }

private:
    std::string message;
};

// ======================
//      任务异常类
// ======================
class TaskOverFlowError : public ThreadPoolError {};

// ======================
//      基础线程类
// ======================
class ThreadBase
{
public:
    ThreadBase() = default;
    virtual ~ThreadBase() = default;

    // 获得当前线程的任务数量
    int getTasksNumb() const {
        return this->task_numb.load();
    }

    // 当前是不是 没有任务
    bool notTask() const {
        return !this->task_numb;
    }

    // 等待当前线程结束
    void join() {
        this->handle.join();
    }

    // 将用户传入的 线程 使用 std::move() 绑定在 this->headle 上
    void bindHandle(std::thread && handle_) {
        this->handle = std::move(handle_);
    }

    // 当前线程是否 空闲
    bool isWaiting() {
        return this->is_wait;
    }

    // 给当前线程的任务队列进行上锁，等待当前线程中所有的任务结束
    void waitTasksDone() {
        this->is_wait = true;
        std::unique_lock<std::mutex> lock(this->task_queue_locker);
        this->task_done.wait(lock, [this] () -> bool { 
                return !this->task_numb; 
            });
    }

    // 清除等待标记 / 将当前线程设置为 非空闲
    void cleanWaitingFlag() {
        this->is_wait = false;
    }

    // 通知 当前任务结束
    void notifyTaskDone() {
        std::unique_lock<std::mutex> lock;
        this->task_done.notify_one();
    }

protected:
    bool is_wait{false};      // 是否执行完当前任务后，在等待下一个任务 / 是否停止该线程
    std::thread handle;         // 处理任务的线程
    std::atomic<int> task_numb{0};   // 任务的数量
    std::condition_variable task_done;   // 信号量，当前结束狗发送通知
    std::mutex task_queue_locker;          // 互斥锁
};

// ==========================================================================================================
//  基本线程池，定义了除了异步线程循环之外的所有基本线程池机制
//  继承了 TheadBase 的 线程包装器类型(并不是这个类是 ThreadBase 的基类，而是模板参数 Type 是 ThreadBase 的基类)
//  is_base_of<A, B> 表示 A 是 B 的基类
// ===========================================================================================================
template <class Type, typename = typename std::enable_if<std::is_base_of<ThreadBase, Type>::value>::type>
class FixedThreadPond
{
protected:
    /**
     * @brief 构造函数，计算出线程池的线程容量 和 每个线程的任务容量
     * @param threadNumb: 线程的数量
     * @param taskCapacity: 线程池中任务的数量，默认是 unlimited
     * @param type_limit: 使用 SFINAE 来限制模板参数的类型只能从 ThreadBase 上继承
    */
    explicit FixedThreadPond(int threadNumb = 0, int taskCapacity = HipeUnlimited) {
        assert(threadNumb >= 0);
        assert(taskCapacity >= 0);

        // 计算线程的数量
        if (threadNumb == 0) {
            int temp = static_cast<int>(std::thread::hardware_concurrency());       // temp = 当前服务器的 cpu 核心数
            this->thread_numb = (temp > 0) ? temp : 1;
        } 
        else {
            this->thread_numb = threadNumb;
        }

        // 计算每个线程的任务容量
        if (taskCapacity == 0) {
            this->taskNum_of_thread_capacity = 0;
        }
        else if (taskCapacity > this->thread_numb) {
            this->taskNum_of_thread_capacity = taskCapacity / this->thread_numb;
        }
        else {
            this->taskNum_of_thread_capacity = 1;
        }

        // 设置负载均衡
        this->cousor_move_limit = this->getBastMoveLimit(threadNumb);

        // ===== test =====
        // std::cout << "FixedThreadPond constructor success." << std::endl;
        // std::cout << "thread number = " << this->thread_numb << std::endl;
        // std::cout << "thread capacity = " << this->thread_capacity << std::endl;
        // ===== test =====
    }

    virtual ~FixedThreadPond() {
        if (!this->is_stop) {
            this->close();
        }
    }

public:
    // ====================================================
    //                Universal interfaces
    // ====================================================
    
    /**
     * @brief 等待所有线程结束它们的任务
    */
    void waitForTasks() {
        // 检查两次，避免一些极端的情况
        for (size_t i = 0; i < this->thread_numb; i++) {
            this->threads[i].waitTasksDone();
        }
        for (size_t i = 0; i < this->thread_numb; i++) {
            this->threads[i].waitTasksDone();
        }

        // 这里因为均衡线程池的 worker 中的任务窃取机制，当前线程执行完任务 不能和 设置标志位一起进行
        for (size_t i = 0; i < this->thread_numb; i++) {
            this->threads[i].cleanWaitingFlag();
        }
    }

    /**
     * @brief 关闭线程池
     * 注意：还在等待的任务不会得到执行了
     * 若是想要确保所有任务都被执行，要先调用 waitForTasks()
    */
    void close() {
        this->is_stop = true;
        for (size_t i = 0; i < this->thread_numb; i++) {
            this->threads[i].join();
        }
    }

    /**
     * @brief 获取当前线程池中所有线程的任务总数
    */
    int getTasksRemain() {
        int result = 0;
        for (size_t i = 0; i < this->thread_numb; i++) {
            result += this->threads[i].getTasksNumb();
        }
        return result;
    }

    /**
     * @brief 获取线程池中线程的数量
    */
    int getThreadNumb() {
        return this->thread_numb;
    }

    /**
     * @brief 提交任务, 没有返回值
     * @param func: 可执行对象
    */
    template <typename Func>
    void submit(Func && func) {
        if (!this->admit()) {   // 提交失败，交给 this->taskOverFlow
            this->taskOverFlow(std::forward<Func>(func));
            return;
        }
        // 将任务交个最不忙的线程
        Type * t = getLeastBusyThread();
        t->enqueue(std::forward<Func>(func));       // ??? 
    }

    /**
     * @brief 提交任务并获得结果
     * @param func 任务
     * @return 一个 future
    */
    template <typename Func>
    auto submitForReturn(Func && func) -> std::future<typename std::result_of<Func()>::type> {
        if (!admit()) {
            this->taskOverFlow(std::forward<Func>(func));
            return std::future<typename std::result_of<Func()>::type>();
        }

        using RT = typename std::result_of<Func()>::type;
        std::packaged_task<RT()> pack(std::forward<Func>(func));
        std::future<RT> future(pack.get_future());

        Type * t = this->getLeastBusyThread();
        t->enqueue(std::move(pack));        // enqueue() 在 balanced_pond.h 和 steady_pond.h 中，加入任务队列
        return future;
    }
    /**
     * @brief 批量提交任务，注意：任务容器必须重载 '[]'
     * @param func 任务容器
     * @param size 任务容器的 size
    */
    template <typename Container>
    void submitInBatch(Container && container, size_t size) {
        if (this->taskNum_of_thread_capacity != 0) {
            this->moveCursorToLeastBusy();
            for (size_t i = 0; i < size; i ++) {
                // 提交一个任务
                if (admit()) {
                    this->getThreadNow()->enqueue(std::move(container[i]));
                }
                else {
                    // 若是当前提交失败，则后面的任务也不会成功
                    this->taskOverFlow(std::forward<Container>(container), i, size);
                    break;
                }
            }
        }
        else {
            this->getLeastBusyThread()->enqueue(std::forward<Container>(container), size);
        }
    }

protected:
    // ====================================================
    //              设置负载平衡机制
    // ====================================================

    /**
     * @brief 移动 cursor 到当前线程池中最闲的线程, 若 cursor 指向的是当前最不繁忙的线程，则 cursor 不会移动
    */
    void moveCursorToLeastBusy() {
        int temp = cursor;
        for (size_t i = 0; i < this->cousor_move_limit; i++) {
            if (this->threads[cursor].getTasksNumb()) {
                cursor = (this->threads[temp].getTasksNumb() < this->threads[cursor].getTasksNumb()) ? temp : cursor;
                util::recyclePlus(temp, 0, this->thread_numb);
            }
            else {
                break;
            }
        }
    }

    /**
     * @brief 移动 cursor 到当前线程池中最闲的线程，并将指针指向它
    */
    Type * getLeastBusyThread() {
        this->moveCursorToLeastBusy();
        return &this->threads[cursor];
    }

    /**
     * @brief 计算最优的 cursor 移动范围，这里设置 limit 在 [0, 4] 之间
    */
    int getBastMoveLimit(int threadNumber) {
        if (threadNumber == 1) {
            return 0;
        }
        int temp = threadNumber / 4;
        temp = (temp < 1) ? 1 : temp;
        return (temp > 4) ? 4 : temp;
    }

public:
    /**
     * @brief 启动两个线程之间的 任务窃取
    */
    void enableStealTasks(int maxNumb = 0) {
        assert(maxNumb >= 0);

        // 数量必须要在0到线程数量之间，定义在 [1, 8] 之间
        if (maxNumb == 0) {
            maxNumb = std::max(this->thread_numb / 4, 1);
            maxNumb = std::min(maxNumb, 8);
        }

        if (maxNumb >= this->thread_numb) {
            throw std::invalid_argument("[myHipeError]: The number of stealing threads must smaller than thread number and greater than zero.");
        }
        this->max_steal = maxNumb;
        this->enable_steal_tasks = true;
    }

    /**
     * @brief 关闭任意两个线程之间的任务窃取
    */
    void disableStealTasks() {
        this->enable_steal_tasks = false;
    }

public:
    // ====================================================
    //                 任务溢出机制
    // ====================================================

    /**
     * @brief 设置 this->refuseCallBack
     * @brief 若容量是无限的，myHipe 会抛出一个异常
     * @brief 若没有设置this->refuseCallBack，myHipe 会抛出一个异常并终止程序
     */
    template <typename Func, typename... Args>
    void setRefuseCallBack(Func && func, Args&&... args) {
        static_assert(util::isRunning<Func, Args...>::value, "[HipeError]: The refuse callback is a non-runnable object.");

        if (this->taskNum_of_thread_capacity == 0) {
            throw std::logic_error("[HipeError]: The refuse callback will never be invoked bacause the capacity has heen set unlimited.");
        }
        else {
            this->refuse_call_back.reset(std::bind(std::forward<Func>(func), std::forward<Args>(args)...));
        }
    }

    /**
     * @brief 从vecotr中拉取一个溢出的任务，一致保存到下一个溢出任务
     * @brief 然后新的任务会取代旧的任务
     */
    std::vector<util::SafeTask> & pullOverFlowTasks() {
        return this->overflow_tasks;
    }

protected:
    Type * getThreadNow() {
        return &this->threads[this->cursor];
    }

    /**
     * @brief 判断是否有能力执行完成 tarCapacity 个任务
     * @brief 若线程池中的任务容量是无限的，这个方法也会返回 true
     * @brief 这个方法会移动线程池的 cursor 来获得足够的任务容量
     *
     * @param tarCapacity 目标容量
    */
    bool admit(int tarCapacity = 1) {
        if (this->taskNum_of_thread_capacity == 0) {
            return true;
        }

        // 当前线程的任务数量 + tarCapacity <= 当前任务容量上限，返回 true
        // 若是将 this->threads 循环一遍之后，没有一个满足上述，返回 false
        int previous = cursor;
        auto spare = [this, tarCapacity] (Type & t) -> bool {
            return (t.getTasksNumb() + tarCapacity) <= this->taskNum_of_thread_capacity;
        };

        // 判断线程池中是否有线程可以加入 tarCapaCity 个任务
        while (!spare(this->threads[cursor])) {
            util::recyclePlus(cursor, 0, this->thread_numb);
            if (cursor == previous) {
                return false;
            }
        }
        return true;
    }

    /**
     * @brief 任务溢出后，进行回调一个任务
     * @param T 类似于 SafeTask类型
    */
    template <typename T>
    void taskOverFlow(T && task) {
        this->overflow_tasks.clear();
        this->overflow_tasks.emplace_back(std::forward<T>(task));

        if (this->refuse_call_back.isSet()) {
            util::invoke(this->refuse_call_back);
        }
        else {
            throw std::runtime_error("[myHipeError]: Task overflow while submitting task.");
        }
    }

    /**
     * @brief 对批量提交的任务进行任务回调
     * @param left 边界的左边(include)
     * @param right 边界的右边(include)
     * @param T 类似于 SafeTask类型
    */
    template <typename T>
    void taskOverFlow(T && tasks, int left, int right) {
        int nums = right - left;
        this->overflow_tasks.clear();
        // this->overflowTasks 的容量不够，进行扩充
        if (static_cast<int>(this->overflow_tasks.capacity()) < nums) {
            this->overflow_tasks.reserve(nums);
        }
        
        for (int i = left; i < right; i++) {
            this->overflow_tasks.emplace_back(std::move(tasks[i]));
        }
        
        if (this->refuse_call_back.isSet()) {
            util::invoke(this->refuse_call_back);
        }
        else {
            throw std::runtime_error("[myHipeError]: Task overflow while submitting task in batch.");
        }
    }


protected:
    bool is_stop{false};            // 是否停止线程池
    int thread_numb{0};             // 线程池中线程的数量
    int cursor{0};                  // 线程池的游标
    int cousor_move_limit{0};       // 在任务窃取时(负载均衡机制)，游标可以移动的范围
    int max_steal{0};               // 线程池最多可以偷窃的任务数量
    bool enable_steal_tasks{false}; // 是否可以使用 任务窃取
    std::unique_ptr<Type[]> threads{nullptr};           // 指向线程池的指针（Type 是 ThreadBase）
    int taskNum_of_thread_capacity{0};                             // 每个线程的任务容量
    std::vector<util::SafeTask> overflow_tasks{1};   // 提交失败的任务
    util::SafeTask refuse_call_back;                    // 处理任务溢出，回调到 refuse_call_back 中
};

}   // !! namespace myHipd

#endif // MYHIPE_INCLUDE_HEADER_H__
