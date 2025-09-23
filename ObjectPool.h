#pragma once
#include "Common.h"



// 定长内存池
//template<size_t N>
//class ObjectPool
//{
//
//};


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
			// 剩余内存不够一个对象大小时，则重新分配一块大空间
			if (_remainBytes < sizeof(T))
			{
                _remainBytes = 128 * 1024;
				//_memory = (char*)malloc(_remainBytes);
                _memory = (char*)SystemAlloc(_remainBytes >> 13);
				if (_memory == nullptr)
				{
					throw std::bad_alloc();
				}
			}

			obj = (T*)_memory;
			size_t objSize = sizeof(T) < sizeof(void*) ? sizeof(void*) : sizeof(T);
			_memory += objSize;
			_remainBytes -= objSize;
		}

		// 定位 New，显示调用T的构造函数进行初始化
		new(obj)T;

		return obj;
	}

	void Delete(T* obj)
	{
		// 显示调用析构函数清理对象
		obj->~T();

		// 回收到自由链表中（链表头插）
		*((void**)obj) = _freeList;
		_freeList = obj;
	}

private:
	char* _memory = nullptr;			// 指向大块内存的指针
	size_t _remainBytes = 0;			// 大块内存在切分过程中剩余字节数
	void* _freeList = nullptr;			// 还回来过程中链接的自由链表的头指针
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