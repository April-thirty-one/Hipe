#include "../../include/thread_pond/dynamic_pond.h"

const int thread_numb = 4;
const int batch_size = 10;
const int min_task_numb = 100;
const int max_task_numb = 100000000;

void test_myHipe_Dynamic_pond_batch_submit()
{
    myHipe::util::print("\n", myHipe::util::title("Test C++(11) Thread Pool Hipe-Dynamic-Batch-Submit(10)"));

    myHipe::DynamicThreadPond pond(thread_numb);
    std::vector<myHipe::util::SafeTask> tasks;
    tasks.reserve(batch_size);

    auto func = [&] (int task_numb) {
        for (int i = 0; i < task_numb; ) {
            for (int j = 0; j < batch_size; j++, i++) {
                tasks.emplace_back([] () {});
            }
            pond.submitInBatch(tasks, batch_size);  // 任务在这里转移
            tasks.clear();
        }
        pond.waitForTasks();
    };
    for (int nums = min_task_numb; nums <= max_task_numb; nums *= 10) {
      double time_cost = myHipe::util::timeWait(func, nums);
      printf("threads: %-2d | task-type: empty task | task-numb: %-9d | time-cost: %.5f(s)\n", thread_numb, nums, time_cost);
    }
}

int main(int argc, char * args[])
{
    test_myHipe_Dynamic_pond_batch_submit();
    return 0;
}