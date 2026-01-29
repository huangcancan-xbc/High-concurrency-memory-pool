#pragma once
#include "Common.h"
#include "ThreadCache.h"
#include "PageCache.h"

// 统一获取线程私有缓存：避免跨线程共享导致锁竞争
static ThreadCache* GetThreadCache()
{
    // 使用 thread_local 保证线程局部存储初始化一致，避免并发下的对象池竞争
    if (pTLSThreadCache == nullptr)
    {
        thread_local ThreadCache tc;
        pTLSThreadCache = &tc;
    }

    return pTLSThreadCache;
}

// 对外统一入口：小对象走线程缓存，大对象走页级分配
static void* ConcurrentAlloc(size_t size)
{
	if (size > MAX_BYTES)
	{
		// 大对象按页对齐，减少系统碎片
		size_t alignedSize = SizeClass::RoundUp(size);
		size_t kpage = alignedSize >> PAGE_SHIFT;

		// PageCache 是全局共享资源，需要加锁保护
		PageCache::GetInstance()->_pageMtx.lock();
		Span* span = PageCache::GetInstance()->NewSpan(kpage);
		span->objSize = size;
		span->_isUse = true; // 防止大对象 span 被误合并
		PageCache::GetInstance()->_pageMtx.unlock();

		void* ptr = (void*)(span->_pageId << PAGE_SHIFT);
		return ptr;
	}
	else
	{
		// 小对象直接走线程缓存，尽量不加锁
		// 每个线程无锁的获取自己专属的 ThreadCache 对象
		return GetThreadCache()->Allocate(size);

	}
}

// 与 ConcurrentAlloc 配套释放，必须传入原始指针
static void ConcurrentFree(void* ptr)
{
	Span* span = PageCache::GetInstance()->MapObjectToSpan(ptr);
	size_t size = span->objSize;

	if (size > MAX_BYTES)
	{
		// 大对象直接归还给 PageCache，再由其合并
		PageCache::GetInstance()->_pageMtx.lock();
		PageCache::GetInstance()->ReleaseSpanToPageCache(span);
		PageCache::GetInstance()->_pageMtx.unlock();
	}
	else
	{
		// 小对象回收到线程缓存，降低锁开销
		// free 路径也需要保证初始化
		GetThreadCache()->Deallocate(ptr, size);
	}
}