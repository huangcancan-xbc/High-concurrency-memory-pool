#include "ConcurrentAlloc.h"

// ntimes:一轮申请和释放内存的次数
// rounds:轮次
void BenchmarkMalloc(size_t ntimes, size_t nworks, size_t rounds)
{
    std::vector<std::thread> vthread(nworks);
    // 计时累加用 atomic，避免多线程写冲突
    std::atomic<size_t> malloc_costtime = 0;
    std::atomic<size_t> free_costtime = 0;

    for (size_t k = 0; k < nworks; ++k)
    {
        vthread[k] = std::thread([&, k] {
            std::vector<void*> v;
            // 预留容量，避免测到 vector 扩容开销
            v.reserve(ntimes);

            for (size_t j = 0; j < rounds; ++j)
            {
                size_t begin1 = clock();
                for (size_t i = 0; i < ntimes; i++)
                {
                    //v.push_back(malloc(16));
                    // 尺寸波动更接近真实业务，而不是固定值（上下选一个）
                    v.push_back(malloc((16 + i) % 8192 + 1));
                }
                size_t end1 = clock();

                size_t begin2 = clock();
                for (size_t i = 0; i < ntimes; i++)
                {
                    free(v[i]);
                }
                size_t end2 = clock();
                v.clear();

                malloc_costtime += (end1 - begin1);
                free_costtime += (end2 - begin2);
            }
            });
    }

    for (auto& t : vthread)
    {
        t.join();
    }

    // 使用 .load() 获取 atomic 值，使用 %zu 替代 %u
    printf("%zu个线程并发执行%zu轮次，每轮次malloc %zu次：花费：%zu ms\n",
        nworks, rounds, ntimes, malloc_costtime.load());

    printf("%zu个线程并发执行%zu轮次，每轮次free %zu次：花费：%zu ms\n",
        nworks, rounds, ntimes, free_costtime.load());

    // 先获取两个atomic的值再相加
    size_t total_costtime = malloc_costtime.load() + free_costtime.load();
    printf("%zu个线程并发malloc&free %zu次，总计花费：%zu ms\n",
        nworks, nworks * rounds * ntimes, total_costtime);
}

// 单轮次申请释放次数 线程数 轮次
void BenchmarkConcurrentMalloc(size_t ntimes, size_t nworks, size_t rounds)
{
    std::vector<std::thread> vthread(nworks);
    // 计时累加用 atomic，避免多线程写冲突
    std::atomic<size_t> malloc_costtime = 0;
    std::atomic<size_t> free_costtime = 0;

    for (size_t k = 0; k < nworks; ++k)
    {
        vthread[k] = std::thread([&]() {
            std::vector<void*> v;
            // 预留容量，避免测到 vector 扩容开销
            v.reserve(ntimes);

            for (size_t j = 0; j < rounds; ++j)
            {
                size_t begin1 = clock();
                for (size_t i = 0; i < ntimes; i++)
                {
                    //v.push_back(ConcurrentAlloc(16));
                    // 尺寸波动更接近真实业务，而不是固定值（上下选一个）
                    v.push_back(ConcurrentAlloc((16 + i) % 8192 + 1));
                }
                size_t end1 = clock();

                size_t begin2 = clock();
                for (size_t i = 0; i < ntimes; i++)
                {
                    ConcurrentFree(v[i]);
                }
                size_t end2 = clock();
                v.clear();

                malloc_costtime += (end1 - begin1);
                free_costtime += (end2 - begin2);
            }
            });
    }

    for (auto& t : vthread)
    {
        t.join();
    }

    // 使用 .load() 获取 atomic 值，使用 %zu 替代 %u
    printf("%zu个线程并发执行%zu轮次，每轮次concurrent alloc %zu次：花费：%zu ms\n",
        nworks, rounds, ntimes, malloc_costtime.load());

    printf("%zu个线程并发执行%zu轮次，每轮次concurrent dealloc %zu次：花费：%zu ms\n",
        nworks, rounds, ntimes, free_costtime.load());

    // 先获取两个atomic的值再相加
    size_t total_costtime = malloc_costtime.load() + free_costtime.load();
    printf("%zu个线程并发concurrent alloc&dealloc %zu次，总计花费：%zu ms\n",
        nworks, nworks * rounds * ntimes, total_costtime);
}

int main()
{
    size_t n = 50000;   //  每个线程、每一轮要执行的分配/释放次数（次数越大，压力越高）
    cout << "=============================================" << endl;
    BenchmarkConcurrentMalloc(n, 5, 10);    // 参数：分配/释放次数、线程数、轮数（每个线程重复 10 轮）
    cout << endl << endl;

    BenchmarkMalloc(n, 5, 10);  // 参数：分配/释放次数、线程数、轮数（每个线程重复 10 轮）

    cout << "=============================================" << endl;

    return 0;
}