# Hipe Thread Pool
## 1. 工具箱 - util.h
### 1.1 简介
在 *util.h* 中，主要定义了一些常用的语法糖，如：
1. 睡眠方法
2. `io`输出 -> 多线程安全`io`输出(递归锁)
3. 判断是否是可运行对象
4. 判断可运行对象的返回值类型是否和我们想的一样
5. 判断是否是使用了 `std::reference_wrapper<...>`
6. 重复执行
7. 函数调用
8. 获得方法的执行时间
9. 基础类型的封装

### 1.2 对 `std::future<T>` 进行封装

### 1.3 使用 C++11 创建 `RAII` 的自旋锁类
1. 成员变量只有一个 `std::atomic_flag flag`
2. lock -- `std::atomic_flag::test_and_set()` 将标志位设置为 `true`，并返回之前的值，`std::memory_order_acquire` 参数表示获取操作的内存顺序，确保在其他线程执行释放操作之前，该线程能够获取最新的标志位值。这样，若返回的是 `true`，表示有其他任务上锁了，需要等待，反之若为 `false`，表示之前没有其他人上锁，执行完后，由这条语句上锁成功
3. `clear` 是 `std::atomic_flag` 类的一个成员函数，用于将标志位设置为`false`，表示释放自旋锁。`std::memory_order_release` 参数表示释放操作的内存顺序，确保在当前线程释放锁之后，其他线程能够获取到最新的标志位值。
4. 为了避免上锁后忘记解锁，使用类似于 `std::lock_guard<>` 智能锁的思想创建 `SpinLock_guard`

### 1.4 创建 安全任务类型 `SafeTask`
SafeTask 中使用 `std::decay<Func>::type` 转换，它的作用是将 `Func` 类型转化为其对应的 **衰减** `type` 类型。

衰减类型的特点是将引用类型转换为对应的值类型，移除顶层的`const`和`volatile` 修饰符，并将数组类型转换为指针类型。

在这个特定的代码中，`std::decay<Func>::type` 用于推导模板参数 `Func` 的衰减类型 `T`。这是为了确保 `Func` 类型在 `GenericExec` 结构体中以值的形式存储，并且不包含引用、常量和数组。

通过使用 `std::decay`，可以避免在模板实例化时出现引用类型或其他类型相关的问题，确保模板参数的一致性和可靠性。


1. 在 `class SafeTask` 中创建了 `struct BaseExec` 和 继承 `struct BaseExec` 的 `struct GenericExec`, 这里是使用了 **工厂设计模式**，是为了可以动态的调整要传入的任务类型，`BaseExec` 中的 `call()` 方法可以满足任务的最低要求。
2. 因为要传入的必须是可执行对象，所以需要加一些判断条件
```cpp
template <typename Func, typename = typename std::enable_if<isRunning<Func>::value>::type>

// isRunning<> 自定义判断是否是可执行对象
// enable_if<> 是否满足条件，若不满足则不执行当前的模板函数
```
### 1.4 创建 安全任务类型 `QuickTask`
思想类似与 `SafeTask`

但是没有使用 `std::decay<Func>::type` 


## 2. 基础类 - header.h
在 *header.h* 中定义了两个基础的线程类，`class ThreadBase` 和 继承 `class ThreadBase` 的 `class FixedThreadPool` 

这里并不细讲，因为也是使用了工厂设计模式，在被 balancedPond 和 steadyPond 继承的时候功能是不同的，所以在具体的线程池中讲

## 3. 动态线程池 - dynamic_pond.h
![动态线程池逻辑图](../Hipe/images/dynamic_pond.png)
### 3.1 简介
`Dynamic` 是动态的、能够**扩缩容**的线程池。支持批量提交任务、支持无界队列，当没有任务时会自动挂起（阻塞等待条件变量通知），相比较其他两个线程池，`Dynamic` 更加节省资源。

### 3.2 提供的接口
```cpp
void addThreads(int);       向线程池中添加线程
void delThreads(int);       减少线程池中线程的数量
void close();               关闭线程池
void adjustThreads(int);    调整线程池中线程的数量
void joinDeadThreads();     回收 this->dead_threads 容器中的线程资源
int getTaskRemain();        获取线程池中的任务数量
int getTaskLoaded();        正在处理任务的数量
int resetTasksLoaded();     重置线程池加载任务的数量（重置为0）
int getRunningThreadNumb(); 当前正在工作的线程数量
int getExpectThreadNumb();  期待正在运行的线程数量
void waitForThreads();      等待线程数量的调整
void waitForTasks();        等待线程池中任务的结束
void submit(Func);          提交一个任务
auto submitForReturn(Func); 提交一个任务，并获得返回值
void submitInBatch(container, size);    批量的提交任务，注意: 包含任务的容器必须要重载 '[]'
```

### 3.3 Dynamic 的特色
`Dynamic` 采用的是 **多线程竞争单任务队列** 的模型。该任务队列是无界的，能够存储大量的任务，知道系统资源耗尽为止。由于 `Dynamic` 没有私有任务队列且面向单个任务，因此可以灵活的调度，但是这也造成了数据竞争严重（主线程和线程池中所有线程竞争一个任务队列），导致性能下降。为了可以动态的调整线程数，在 *util.h* 中提供了监测线程池执行速率的接口 `myHipe::util::timeWait()`，可在 *Hipe/test/efficience/test_dynamic_efficience_pond.cpp* 中查看示例，下面是测试 1亿 个空任务所需要耗费的时间：
![dynamic_eff](../Hipe/images/dynamic_eff.png)

## 4. 均衡线程池 - balanced_pond.h
![balanced_pond](../Hipe/images/balanced_pond.png)
### 4.1 简介
`Balance` 对比 `Steady` 除了对其使用的线程类做了简化之外，其余机制包括线程间负载均衡和任务溢出机制都是相同的。提供的接口也是相同的。但是，相对于 `Steady` 面向批量任务的机制不同，`Balance` 采用的是和 `Dynamic` 相同的 **面向单个任务** 的思想，即每次只获取一个任务执行。这也使得两个线程池之间的工作方式略有不同。

### 4.2 提供的接口
#### 4.2.1 QqThread 提供的接口
```cpp
void getTaskNumb();         获得当前线程的任务数量
bool notTask();             当前是否没有任务
void join();                回收当前线程资源
void bindHeadle();          将用户传入的 线程 使用 std::move() 绑定在 this->headle 上
bool isWaiting();           当前线程是否停止
void waitTaskDone();        当前线程的任务队列进行上锁，等待当前线程中所有的任务结束
void cleanWaitingFlag();    将当前线程设置为 非空闲
void notifyTaskDone();      通知 当前任务结束
bool tryGiveTaskToOther(OqThread &);    尝试将当前线程的一个任务交给另外一个线程
void enqueue(T && task);                添加一个任务到任务队列中
void enqueue(Container &, size_t);      添加多个任务到任务队列中       
void runTask();                         运行一个任务
bool tryLoadTask();                     尝试从自己的任务队列中加载任务
```

#### 4.2.2 balanced_pond 提供的接口
```cpp
explicit BalancedThreadPond(thread_numb, task_capactiry);    计算出线程池内线程的数量，和每个线程的任务容量 
void waitForTasks();        等待所有线程结束它们的任务
void close();               关闭线程池，主要是在线程池的析构函数中使用
int getTasksRemain();       获取当前线程池中所有线程的任务总数
int getThreadNumb();        获取线程池中线程的数量
void submit(Func &&);       提交任务，没有返回值
auto submitForReturn(Func &&);                  提交任务并获得返回值
void submitInBatch(Container &&, size_t);       批量提交任务，注意：任务容器必须重载 '[]'
void enableStealTasks(int);                     启动两个任务之间的 任务窃取 机制, 线程范围在[0, 8]之间（可以在代码中修改）
void disableStealTasks();                       关闭任意两个线程之间的
void setRefuseCallBack(Func &&, Args &&...);    将用户传入的任务放入 this->refuse_call_back 中，等待回调
std::vector<Task> & pullOverFlowTasks();        取出线程池中溢出的任务
```
### 4.3 balanced_pond 的特点
`Balance` 采用的是 **单队列线程**，即内置了单条任务队列，主线程采用一种优于轮询的负载均衡机制向线程类内部的任务队列分发任务，工作线程直接查询该任务队列并获取任务。

由于线程类中只有一条任务队列，因此所有任务都是可以被窃取的。这也导致了 `Balance` 在面对 **不稳定任务流** 时可能会有更好的表现。

在数据竞争方面，线程池中每个线程都一个专属的任务队列，主线程向该线程分发任务会把任务传入到这个任务队列中，这样数据竞争就只发生在主线程与一个子线程之间（`Dynamic` 是发生在主线程和所有子线程之间）。可在 *Hipe/test/efficience/test_balanced_efficience_pond.cpp* 中查看示例，下面是在 **4核 8G内存** 的虚拟机上测试 1亿 个空任务所需要耗费的时间：
![balanced_pond](../Hipe/images/balanced_eff.png)

## 5. 稳定线程池 - Steady_pond.h
![steady_pond](../Hipe/images/steady_pond.png)
### 5.1 简介
`Steady` 是一个稳定的、具有固定线程数量的线程池。支持批量提交任务和批量执行任务、支持有界任务队列和无界任务队列, 支持池中线程的 **任务窃取机制**。任务溢出是支持 **注册回调** 并执行或者 **抛出异常**。

### 5.2 Steady 提供的接口
#### 5.2.1 DqThread 提供的接口
```cpp
void getTaskNumb();         获得当前线程的任务数量
bool notTask();             当前是否没有任务
void join();                回收当前线程资源
void bindHeadle();          将用户传入的 线程 使用 std::move() 绑定在 this->headle 上
bool isWaiting();           当前线程是否停止
void waitTaskDone();        当前线程的任务队列进行上锁，等待当前线程中所有的任务结束
void cleanWaitingFlag();    将当前线程设置为 非空闲
void notifyTaskDone();      通知 当前任务结束
void runTask();             执行 this->buffer_task_queue 中的第一个任务
bool tryLoadTask();         尝试从 this->public_task_queue 中加载任务到 this->buffer_task_queue 中
bool tryGiveTasksToAnother(DqThread &);     尝试从其他线程中获取任务
void enqueue(T &&);         添加一个任务到任务队列中
void enqueue(Container &, size_t);          添加多个任务到任务队列中
```

#### 5.2.1 Steady_pond 提供的接口
```cpp
explicit SteadyThreadPond(thread_numb, task_capactiry);    计算出线程池内线程的数量，和每个线程的任务容量 
void waitForTasks();        等待所有线程结束它们的任务
void close();               关闭线程池，主要是在线程池的析构函数中使用
int getTasksRemain();       获取当前线程池中所有线程的任务总数
int getThreadNumb();        获取线程池中线程的数量
void submit(Func &&);       提交任务，没有返回值
auto submitForReturn(Func &&);                  提交任务并获得返回值
void submitInBatch(Container &&, size_t);       批量提交任务，注意：任务容器必须重载 '[]'
void enableStealTasks(int);                     启动两个任务之间的 任务窃取 机制, 线程范围在[0, 8]之间（可以在代码中修改）
void disableStealTasks();                       关闭任意两个线程之间的
void setRefuseCallBack(Func &&, Args &&...);    将用户传入的任务放入 this->refuse_call_back 中，等待回调
std::vector<Task> & pullOverFlowTasks();        取出线程池中溢出的任务
```

### 5.3 Steady 的特点
`Steady` 使用的线程类 `DqThread` 为每个线程分配了公开任务队列、缓冲任务队列 和 控制线程的同步变量（线程专属成员变量），尽量降低 **乒乓缓存** 和 **线程同步** 对线程池性能的影响。工作线程通过队列替换 **批量下载** 公开任务队列的任务到缓冲任务队列中执行。生产线程则通过公开任务队列为工作线程分配任务（采用了一个优于轮询的 **负载均衡** 机制）。通过公开任务队列和缓冲任务队列替换的机制实现了 **读写分离**，在通过加 **轻锁**（C++11 原子量实现的自旋锁）的方式极大的提高了线程池的性能。

由于底层的实现机制，`Steady` 适用于 **稳定的**（避免超时任务阻塞线程）、**任务量大**（任务传递的优势得以体现）的任务流。可以说 Steady 适合作为核心线程池（能够处理基准任务并长时间运行），而当 **定制容量** 的 `Steady` 面临任务数量超出设定值时 -- 即 **任务溢出** 时，可以通过定制的 **回调函数** 拉取溢出的任务，并把这些任务推到 Dynamic 中，在这个场景中，Dynamic 可以被叫做 `Cache Thread Pond` 缓冲线程池，实践示例：*Hipe/test/test_steady_dynamic.cpp* 。

在数据竞争方面，线程池中每个线程都两个专属的任务队列，公共任务队列 和 缓冲任务队列，主线程向该线程分发任务会把任务传入到 公共任务队列 中，而工作线程在执行完当前任务后会从 缓冲任务队列 中取出下一个任务，这样只会在缓冲任务队列中没有任务，进行公共任务队列和缓冲任务队列交换时出现数据竞争，这种数据竞争的场景相对前面两个线程池会很少出现，也就提高了性能。可在 *Hipe/test/efficience/test_steady_efficience_pond.cpp* 中查看示例，下面是在 **4核 8G内存** 的虚拟机上测试 1亿 个空任务所需要耗费的时间：
![steady_pond](../Hipe/images/steady_eff.png)