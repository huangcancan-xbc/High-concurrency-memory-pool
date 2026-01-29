// ConcurrentMemoryPool.cpp : 此文件包含 "main" 函数。程序执行将在此处开始并结束。
//#define RUN_EXTRA_TESTS   // 条件编译：只有当宏 RUN_EXTRA_TESTS 被定义时，放开这里下文的mian才能被执行

#include "ObjectPool.h"
#include "ConcurrentAlloc.h"
#include <random>

// 覆盖对齐边界尺寸，验证分桶映射稳定
static void TestBoundarySizes()
{
    const size_t kIters = 2000;
    const size_t sizes[] = {
        1, 7, 8, 9, 127, 128, 129,
        1023, 1024, 1025,
        8191, 8192, 8193,
        65535, 65536,
        262143, 262144
    };

    for (size_t s : sizes)
    {
        std::vector<void*> v;
        v.reserve(kIters);
        for (size_t i = 0; i < kIters; ++i)
        {
            v.push_back(ConcurrentAlloc(s));
        }
        for (void* p : v)
        {
            ConcurrentFree(p);
        }
    }
}

// 走大对象路径，验证页级分配/释放
static void TestLargeAlloc()
{
    const size_t kIters = 200;
    const size_t sizes[] = {
        MAX_BYTES + 1,
        MAX_BYTES + 123,
        512 * 1024,
        1024 * 1024
    };

    for (size_t s : sizes)
    {
        std::vector<void*> v;
        v.reserve(kIters);
        for (size_t i = 0; i < kIters; ++i)
        {
            v.push_back(ConcurrentAlloc(s));
        }
        for (void* p : v)
        {
            ConcurrentFree(p);
        }
    }
}

// 跨线程释放，覆盖 CentralCache 回收路径
static void TestCrossThreadFree()
{
    const size_t n = 60000;
    std::vector<void*> v;
    v.reserve(n);

    std::thread producer([&] {
        for (size_t i = 0; i < n; ++i)
        {
            v.push_back(ConcurrentAlloc((i % 8192) + 1));
        }
    });
    producer.join();

    std::atomic<size_t> idx = 0;
    const size_t workers = 4;
    std::vector<std::thread> ts;
    ts.reserve(workers);
    for (size_t t = 0; t < workers; ++t)
    {
        ts.emplace_back([&] {
            while (true)
            {
                size_t i = idx.fetch_add(1);
                if (i >= v.size())
                {
                    break;
                }
                ConcurrentFree(v[i]);
            }
        });
    }
    for (auto& t : ts)
    {
        t.join();
    }
}

// 随机大小 + 乱序释放，模拟真实负载
static void TestRandomMixed()
{
    const size_t total = 100000;
    const size_t batch = 10000;

    // 固定种子保证可复现
    std::mt19937_64 rng(12345);
    std::uniform_int_distribution<size_t> dist(1, MAX_BYTES * 2);

    std::vector<void*> v;
    v.reserve(batch);

    for (size_t offset = 0; offset < total; offset += batch)
    {
        v.clear();
        for (size_t i = 0; i < batch; ++i)
        {
            size_t s = dist(rng);
            v.push_back(ConcurrentAlloc(s));
        }

        std::shuffle(v.begin(), v.end(), rng);

        std::atomic<size_t> idx = 0;
        const size_t workers = 4;
        std::vector<std::thread> ts;
        ts.reserve(workers);
        for (size_t t = 0; t < workers; ++t)
        {
            ts.emplace_back([&] {
                while (true)
                {
                    size_t i = idx.fetch_add(1);
                    if (i >= v.size())
                    {
                        break;
                    }
                    ConcurrentFree(v[i]);
                }
            });
        }
        for (auto& t : ts)
        {
            t.join();
        }
    }
}

#ifdef RUN_EXTRA_TESTS
int main()
{
    TestBoundarySizes();
    TestLargeAlloc();
    TestCrossThreadFree();
    TestRandomMixed();

    cout << "Extra tests: OK" << endl;
    return 0;
}
#endif

// 运行程序: Ctrl + F5 或调试 >“开始执行(不调试)”菜单
// 调试程序: F5 或调试 >“开始调试”菜单

// 入门使用技巧: 
//   1. 使用解决方案资源管理器窗口添加/管理文件
//   2. 使用团队资源管理器窗口连接到源代码管理
//   3. 使用输出窗口查看生成输出和其他消息
//   4. 使用错误列表窗口查看错误
//   5. 转到“项目”>“添加新项”以创建新的代码文件，或转到“项目”>“添加现有项”以将现有代码文件添加到项目
//   6. 将来，若要再次打开此项目，请转到“文件”>“打开”>“项目”并选择 .sln 文件
