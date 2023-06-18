#include "../../include/thread_pond/steady_pond.h"

using namespace myHipe;

// ======================
//      测试接口的性能
// ======================
int thread_numb = 4;
int batch_size = 10;
int min_task_numb = 100;
int max_task_numb = 100000000;

void test_Hipe_steady_batch_submit() {
    myHipe::util::print("\n", myHipe::util::title("Test C++(11) Thread Pool Hipe-Steady-Batch-Submit(10)"));

    // myHipe::SteadyThreadPond pond(thread_numb, thread_numb * 1000);
    myHipe::SteadyThreadPond pond(thread_numb);
    std::vector<util::SafeTask> tasks;
    tasks.reserve(batch_size);

    auto foo = [&](int task_numb) {
        for (int i = 0; i < task_numb;) {
            for (int j = 0; j < batch_size; ++j, ++i) {
                tasks.emplace_back([] {});
            }
            pond.submitInBatch(tasks, batch_size);
            tasks.clear();
        }
        pond.waitForTasks();
    };

    for (int nums = min_task_numb; nums <= max_task_numb; nums *= 10) {
        double time_cost = myHipe::util::timeWait(foo, nums);
        printf("threads: %-2d | task-type: empty task | task-numb: %-9d | time-cost: %.5f(s)\n", thread_numb, nums,
               time_cost);
    }
}

int main() 
{
    test_Hipe_steady_batch_submit();

    return 0;
}