#pragma once
#include "Common.h"

class ThreadCache
{
public:
	// 申请和释放内存对象
	void* Allocate(size_t size);
	void Deallocate(void* ptr, size_t size);

	// 从中心缓存获取对象
	void* FetchFromCentralCache(size_t index, size_t size);

	// 释放对象时，链表过长时，回收内存回到中心缓存
	void ListTooLong(FreeList& list, size_t size);
private:
	// 每个桶只被当前线程访问，无需加锁
	FreeList _freeLists[NFREELISTS];
};


// 线程局部存储指针声明：每个线程只绑定一个 ThreadCache 实例
// 头文件只声明线程局部存储指针，避免跨编译单元多份实例
extern thread_local ThreadCache* pTLSThreadCache;

