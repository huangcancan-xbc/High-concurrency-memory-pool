#pragma once
#include "Common.h"
#include <cstdint>



// 定长内存池
//template<size_t N>
//class ObjectPool
//{
//
//};


// 轻量对象池：只负责分配/回收元数据对象，不做复杂生命周期管理
template<typename T>
class ObjectPool
{
public:
	T* New()
	{
		T* obj = nullptr;

		// 优先把还回来的内存块对象再次重复利用
		if (_freeList != nullptr)
		{
			void* next = *((void**)_freeList);
            obj = (T*)_freeList;
			_freeList = next;
		}
		else
		{
			size_t objSize = sizeof(T) < sizeof(void*) ? sizeof(void*) : sizeof(T);
			size_t align = alignof(T);
			size_t alignMask = align - 1;

			auto allocBlock = [&]() {
				// 大块内存按页申请，失败直接抛异常
				_remainBytes = 128 * 1024;
				//_memory = (char*)malloc(_remainBytes);
				_memory = (char*)SystemAlloc(_remainBytes >> 13);
				if (_memory == nullptr)
				{
					throw std::bad_alloc();
				}
			};

			// 使用 objSize 并考虑对齐填充，避免越界/下溢
			if (_remainBytes < objSize + alignMask)
			{
				allocBlock();
			}

			uintptr_t raw = reinterpret_cast<uintptr_t>(_memory);
			uintptr_t aligned = (raw + alignMask) & ~static_cast<uintptr_t>(alignMask);
			size_t padding = static_cast<size_t>(aligned - raw);

			if (_remainBytes < padding + objSize)
			{
				allocBlock();
				raw = reinterpret_cast<uintptr_t>(_memory);
				aligned = (raw + alignMask) & ~static_cast<uintptr_t>(alignMask);
				padding = static_cast<size_t>(aligned - raw);
			}

			obj = (T*)aligned;
			_memory = (char*)aligned;
			_memory += objSize;
			_remainBytes -= (padding + objSize);
		}

		// 定位 New，显式调用T的构造函数进行初始化
		new(obj)T;

		return obj;
	}

	void Delete(T* obj)
	{
		// 显式调用析构函数清理对象
		obj->~T();

		// 回收到自由链表中（链表头插）
		*((void**)obj) = _freeList;
		_freeList = obj;
	}

private:
	char* _memory = nullptr;            // 指向大块内存的指针
	size_t _remainBytes = 0;            // 大块内存在切分过程中剩余字节数
	void* _freeList = nullptr;          // 还回来过程中链接的自由链表的头指针
};




//struct TreeNode
//{
//    int _val;
//    TreeNode* _left;
//    TreeNode* _right;
//
//    TreeNode()
//        : _val(0)
//        , _left(nullptr)
//        , _right(nullptr)
//    {
//    }
//};
//
//void TestObjectPool()
//{
//    // 申请释放的轮次
//    const size_t Rounds = 10;
//
//    // 每轮申请释放多少次
//    const size_t N = 1000000;
//
//    std::vector<TreeNode*> v1;
//    v1.reserve(N);
//
//    size_t begin1 = clock();
//    for (size_t j = 0; j < Rounds; ++j)
//    {
//        for (int i = 0; i < N; ++i)
//        {
//            v1.push_back(new TreeNode);
//        }
//        for (int i = 0; i < N; ++i)
//        {
//            delete v1[i];
//        }
//        v1.clear();
//    }
//
//    size_t end1 = clock();
//
//    std::vector<TreeNode*> v2;
//    v2.reserve(N);
//
//    ObjectPool<TreeNode> TNPool;
//    size_t begin2 = clock();
//    for (size_t j = 0; j < Rounds; ++j)
//    {
//        for (int i = 0; i < N; ++i)
//        {
//            v2.push_back(TNPool.New());
//        }
//        for (int i = 0; i < N; ++i)
//        {
//            TNPool.Delete(v2[i]);
//        }
//        v2.clear();
//    }
//
//    size_t end2 = clock();
//
//    cout << "普通申请释放花费时间：" << end1 - begin1 << endl;
//    cout << "对象池申请释放花费时间：" << end2 - begin2 << endl;
//}