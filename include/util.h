#ifndef MYHIPE_INCLUDE_UTIL_H__
#define MYHIPE_INCLUDE_UTIL_H__

#include <atomic>
#include <future>
#include <functional>
#include <ostream>
#include <iostream>
#include <chrono>
#include <mutex>
#include <ostream>
#include <ratio>
#include <thread>
#include <tuple>
#include <type_traits>
#include <utility>
#include <vector>
#include <algorithm>

namespace myHipe 
{

namespace util
{

// ======================
//         睡眠方法 
// ======================
inline void sleep_for_seconds(int sec)
{
    std::this_thread::sleep_for(std::chrono::seconds(sec));
}

inline void sleep_for_milliseconds(int sec)
{
    std::this_thread::sleep_for(std::chrono::milliseconds(sec));
}

inline void sleep_for_microseconds(int sec)
{
    std::this_thread::sleep_for(std::chrono::microseconds(sec));
}

inline void sleep_for_nanoseconds(int sec)
{
    std::this_thread::sleep_for(std::chrono::nanoseconds(sec));
}

// ======================
//        简单IO
// ======================
template <class T>
void print(T && element)
{
    std::cout << std::forward<T>(element) << std::endl;
}

// 这个方法可以将参数全部打印出来，但是它要依托上面的方法才能执行 
template <class T, class... Args>
void print(T && element, Args... args)
{
    std::cout << std::forward<T>(element);
    print(std::forward<Args>(args)...);
}

// =====================================
//        线程通用输出流
//   它可以保证输出不会受到多线程的影响
// =====================================
class SyncStream
{
public:
    explicit SyncStream(std::ostream & out_stream = std::cout) : out_stream(out_stream) {}

    template <typename T>
    void print(T && items) {
        this->io_locker.lock();
        this->out_stream << std::forward<T>(items) << std::endl;
        this->io_locker.unlock();
    }

    template <class T, class... Args>
    void print(T && item, Args... args) {
        this->io_locker.lock();
        this->out_stream << std::forward<T>(item);
        this->print(std::forward<Args>(args)...);
        this->io_locker.unlock();
    }

private:
    std::ostream & out_stream;
    std::recursive_mutex io_locker;     // 递归锁
};

// ======================
//          TMP
// ======================

// 判断模板参数是否为可运行对象
template <typename Func, typename... Args>
using isRunning = std::is_constructible<std::function<void(Args...)>, typename std::remove_reference<Func>::type>;

// 判断可运行对象 Func 的返回对象类型是否是 R
template <typename Func, typename R>
using isReturn = std::is_same<typename std::result_of<Func()>::type, R>;

// 判断 U 是否是 std::reference_wrapper<...>
template <typename U, typename DU = typename std::decay<U>::type>
struct is_reference_wrapper 
{
    template <typename T, typename D = typename T::type>
    static constexpr bool check(T *) {
        return std::is_same<T, std::reference_wrapper<D>>::value;
    }

    static constexpr bool check(...) {
        return false;
    }

    static constexpr bool value = check(static_cast<DU *>(0));
};

// ======================
//        语法糖
// ======================
// 调用 func times 次
template <typename Func, typename = typename std::enable_if<isRunning<Func>::value>::type>
void repeat(Func && func, int times = 1) 
{
    for (int i = 0; i < times; i++) {
        std::forward<Func>(func)();
    }
}

// 函数调用
template <typename Func, typename... Args>
void invoke(Func && func, Args... args)
{
    static_assert(isRunning<Func, Args...>::value, "[HipeError]: Invoke non-runnable object.");
    func(std::forward<Args>(args)...);
}

// 区间循环
template <typename Var>
void recyclePlus(Var & var, Var left_border, Var right_border)
{
    var = (++var == right_border) ? left_border : var;
}

// ===========================================================
//        等待可运行对象的时间
// 注意：要使用 std::micro 或 std::nano 填充模板参数 Precision
// ===========================================================
template <typename Precision, typename Func, typename... Args>
double timeWait(Func && func, Args... args)
{
    static_assert(isRunning<Func, Args...>::value, "[myHipeError]: timeWait for non-runnable objuce.");

    auto timeStart = std::chrono::steady_clock::now();
    func(std::forward<Args>(args)...);
    auto timeEnd = std::chrono::steady_clock::now();
    return std::chrono::duration<double, Precision>(timeEnd - timeStart).count();
}

// ======================
//   等待可运行对象的时间
//   精度是 std::seconds
// ======================
template <typename Func, typename... Args>
double timeWait(Func && func, Args... args)
{
    static_assert(isRunning<Func, Args...>::value, "[myHipeError]: timeWait for not-runnable object.");
    
    auto timeStart = std::chrono::steady_clock::now();
    func(std::forward<Args>(args)...);
    auto timeEnd = std::chrono::steady_clock::now();
    return std::chrono::duration<double>(timeEnd - timeStart).count();
}

//===============
//     title
//===============
inline std::string title(const std::string & tar, int left_right_edge = 4)
{
    static std::string ele1 = "=";
    static std::string ele2 = " ";
    static std::string ele3 = "*";

    std::string res;

    repeat([&] { res.append(ele1); }, left_right_edge * 2 + static_cast<int>(tar.size()));
    res.append("\n");

    res.append(ele3);
    repeat([&] { res.append(ele2); }, left_right_edge - static_cast<int>(ele3.size()));
    res.append(tar);
    repeat([&] { res.append(ele2); }, left_right_edge - static_cast<int>(ele3.size()));
    res.append(ele3);
    res.append("\n");

    repeat([&] { res.append(ele1); }, left_right_edge * 2 + static_cast<int>(tar.size()));
    return res;
}

/**
 * just like this
 * <[ something ]>
 */
inline std::string strong(const std::string &tar, int left_right_edge = 2) 
{
    static std::string ele1 = "<[";
    static std::string ele2 = "]>";

    std::string res;
    res.append(ele1);

    repeat([&] { res.append(" "); },
           left_right_edge - static_cast<int>(ele1.size()));
    res.append(tar);
    repeat([&] { res.append(" "); },
           left_right_edge - static_cast<int>(ele2.size()));

    res.append(ele2);
    return res;
}

inline std::string boundary(char element, int length = 10) 
{
    return std::string(length, element);
}

// ======================================
//             基础模型
// ======================================
template <typename T>
class Futures
{
public:
    Futures() : futures(0), results(0) {}

    // 将返回的结果存放发哦容器 vector 中
    std::vector<T> & get() {
        this->results.resize(this->futures.size());
        for (size_t i = 0; i < this->futures.size(); i++) {
            this->results[i] = this->futures[i].get();
            // this->results.at(i) = this->futures.at(i).get();
        }
        return this->results;
    }

    std::future<T> & operator [] (size_t idx) const {
        return this->futures.at(idx);
    }

    void push_back(std::future<T> && future) {
        this->futures.push_back(std::forward<std::future<T>>(future));      // 这里写的和 Hipe 不同
    }

    size_t size() const {
        return this->futures.size();
    }

    // 等待所有的 future 完成
    void wait() const {
        for (size_t i = 0; i < this->futures.size(); i++) {
            this->futures.at(i).wait();
        }
    }

private:
    std::vector<std::future<T>> futures;
    std::vector<T> results;
};

// ======================================
//  基于C++11 std::atomic_flag 的 自旋锁
// ======================================
class SpinLock
{
public:
    void lock() {
        while (this->flag.test_and_set(std::memory_order_acquire)) {}
    }

    void unlock() {
        this->flag.clear(std::memory_order_release);
    }

    bool try_lock() {
        return !flag.test_and_set();
    }

private:
    std::atomic_flag flag = ATOMIC_FLAG_INIT;
};

// =====================================================
//  使用上面的自旋锁，构建一个类似于 std::lock_guard 的类
// =====================================================
class SpinLock_guard
{
public:
    explicit SpinLock_guard(SpinLock & locker) : locker(&locker)
    {
        this->locker->lock();
    }
    ~SpinLock_guard()
    {
        this->locker->unlock();
    }

private:
    SpinLock * locker;
};

// =====================================================
//  它是一种安全的任务类型，支持保存不同类型的可运行对象。
//  它允许用户通过引用(左值或右值)构造一个新的可运行对象。
// =====================================================
class SafeTask
{
public:
    SafeTask() = default;
    SafeTask(SafeTask && ohter) = default;
    ~SafeTask() = default;

    SafeTask(SafeTask & other) = delete;
    SafeTask(const SafeTask &) = delete;
    SafeTask & operator = (const SafeTask &) = delete;

    // 构造一个任务
    template <typename Func, typename = typename std::enable_if<isRunning<Func>::value>::type> 
    SafeTask(Func && foo) : exe(new GenericExec<Func>(std::forward<Func>(foo))) {}

    // 重新设置任务
    template <typename Func, typename = typename std::enable_if<isRunning<Func>::value>::type>
    void reset(Func && func) {
        // unique_ptr::result(new) -- 指向一个新的内存，释放原先的
        this->exe.reset(new GenericExec<Func>(std::forward<Func>(func)));
    }

    // 是否设置了任务
    bool isSet() {
        return static_cast<bool>(this->exe);
    }

    // 重载 ‘=’
    SafeTask & operator = (SafeTask && other) {
        this->exe.reset(other.exe.release());
        return *this;
    }

    // runnable
    void operator () () {
        this->exe->call();
    }

private:
    struct BaseExec {
        virtual void call() = 0;
        virtual ~BaseExec() = default;
    };

    template <typename Func, typename T = typename std::decay<Func>::type>
    struct GenericExec : BaseExec {
        T foo;
        
        GenericExec(Func && func) : foo(std::forward<Func>(func)) {
            static_assert(!is_reference_wrapper<Func>::value, "[HipeError]: Use 'reference_wrapper' to save temporary variable is dangerous.");
        }

        ~GenericExec() override = default;

        void call() override {
            foo();
        }
    };

private:
    // 动态创建，若使用 new GenericExec<Func> 则 exe 指向 GenericExec对象，但是智能调用 BaseExec 的方法
    std::unique_ptr<BaseExec> exe{nullptr};
};

// =====================================================
// 它是一个快速任务，支持保存不同类型的可运行对象。
// 它允许用户通过引用(左值或右值)来构造它。
// 它不会在里面构造一个新的物体，但是通过保存引用来延长可运行对象的生命。
// =====================================================
class QuickTask
{
public:
    QuickTask() = default;
    QuickTask(QuickTask && other) = default;
    ~QuickTask() = default;

    QuickTask(QuickTask &) = delete;
    QuickTask(const QuickTask &) = delete;
    QuickTask & operator = (const QuickTask &) = delete;

    // 构造一个任务
    template <typename Func, typename = typename std::enable_if<isRunning<Func>::value>::type>
    QuickTask(Func && func) : exe(new GenericExec<Func>(std::forward<Func>(func))) {}

    // 重置任务
    template <typename Func, typename = typename std::enable_if<isRunning<Func>::value>::type>
    void reset(Func && func) {
        this->exe.reset(new GenericExec<Func>(std::forward<Func>(func)));
    }

    // 判断是否设置了
    bool isSet() const {
        return static_cast<bool>(this->exe);    
    }

    // 重载 '='
    QuickTask & operator = (QuickTask && other) {
        this->exe.reset(other.exe.release());
        return *this;
    }

    // runnable
    void operator () () {
        this->exe->call();
    }

private:
    struct BaseExec {
        virtual void call() = 0;
        virtual ~BaseExec() = default;
    };

    template <typename Func>
    struct GenericExec : BaseExec {
        Func foo;
        GenericExec(Func && foo) : foo(std::forward<Func>(foo)) {}
        ~GenericExec() = default;

        void call() override {
            // 这里是重载了 ()。
            this->foo();
        }
    };

private:
    std::unique_ptr<BaseExec> exe{nullptr};
};

}   // !! namespace util
}   // !! namespace myHipe

#endif
