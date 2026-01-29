#include "CentralCache.h"
#include "PageCache.h"

CentralCache CentralCache::_sInst;

// 获取一个非空的 Span
Span* CentralCache::GetOneSpan(SpanList& list, size_t size)
{
    // 先在本桶里找，有空闲就不触发 PageCache
    // 先查看当前的 spanlist 中是否还有未分配的对象 span
    Span* it = list.Begin();
    while (it != list.End())
    {
        if (it->_freeList != nullptr)
        {
            return it;
        }
        else
        {
            it = it->_next;
        }
    }

    // 先把桶锁解掉，避免锁住整个桶去做慢操作
    // 先把 central cache 的桶锁解掉，这样，如果其他线程释放内存对象回来，不会阻塞
    list._mtx.unlock();

    // PageCache 是全局共享资源，需要单独加锁
    // 走到这里说明没有空闲的 span 了，只能找 page cache 申请内存
    PageCache::GetInstance()->_pageMtx.lock();
    Span* span = PageCache::GetInstance()->NewSpan(SizeClass::NumMovePage(size));
    span->_isUse = true;
    span->objSize = size;
    PageCache::GetInstance()->_pageMtx.unlock();
    
    // 对获取的 span 进行切分不加锁：此时还未挂回桶，其他线程看不到
        
    // 计算 span 的大块内存的起始地址和大块内存的大小（字节数）
    char* start = (char*)(span->_pageId << PAGE_SHIFT);
    size_t bytes = span->_n << PAGE_SHIFT;
    char* end = start + bytes;

    // 把大块内存切成自由链表链接起来
    // 1. 先切一块下来去做头，方便尾插
    span->_freeList = start;
    start += size;
    void* tail = span->_freeList;
    int i = 1;
    while (start < end)
    {
        ++i;
        NextObj(tail) = start;
        tail = NextObj(tail);       // tail = satrt;
        start += size;
    }

    NextObj(tail) = nullptr;
    
    //// 条件断点
    //// 疑似死循环，可以中断程序，程序会在正在运行的地方停下来
    //int j = 0;
    //void* cur = span->_freeList;
    //while (cur)
    //{
    //    cur = NextObj(cur);
    //    ++j;
    //}
    //if (j != (bytes / size))
    //{
    //    int x = 0;
    //}


    // 切好后再挂回桶，减少持锁时间
    // 切好 span 后，需要把 span 挂到桶里面去的时候，在加锁
    list._mtx.lock();
    list.PushFront(span);

    return span;
}

// 从中心缓存获取一定数量的对象给 thread cache
size_t CentralCache::FetchRangeObj(void*& start, void*& end, size_t batchNum, size_t size)
{
    size_t index = SizeClass::Index(size);
    // 桶级锁：只有访问同一桶的线程才会竞争
    _spanLists[index]._mtx.lock();

    Span* span = GetOneSpan(_spanLists[index], size);
    assert(span);
    assert(span->_freeList != nullptr);

    // 从 span 中获取 batchNum 个对象
    // 如果不够 batchNum 个，有多少拿多少
    start = span->_freeList;
    end = start;
    size_t i = 0;
    size_t actualNum = 1;

    while (i < batchNum - 1 && NextObj(end) != nullptr)
    {
        end = NextObj(end);
        i++;
        actualNum++;
    }

    span->_freeList = NextObj(end);
    NextObj(end) = nullptr;
    // 记录分配出去的数量，便于判断是否可归还 PageCache
    span->_useCount += actualNum;

    //// 条件断点
    //int j = 0;
    //void* cur = start;
    //while (cur)
    //{
	   // cur = NextObj(cur);
	   // ++j;
    //}
    //if (actualNum != j)
    //{
	   // int x = 0;
    //}


    _spanLists[index]._mtx.unlock();

    return actualNum;
}


// 将一定数量的对象释放到 span 跨度中
void CentralCache::ReleaseListToSpans(void* start, size_t size)
{
    size_t index = SizeClass::Index(size);
    // 桶级锁：只有访问同一桶的线程才会竞争
    _spanLists[index]._mtx.lock();

    while (start)
    {
        void* next = NextObj(start);

        Span* span = PageCache::GetInstance()->MapObjectToSpan(start);
        NextObj(start) = span->_freeList;
        span->_freeList = start;
        span->_useCount--;

        // 说明 span 的切出去的所有小块内存都回来了
        // 这个 span 就可以再回去给 page cache，pagecache 可以再尝试去做前后页的合并
        if (span->_useCount == 0)
        {
            _spanLists[index].Erase(span);
            span->_freeList = nullptr;
            span->_next = nullptr;
            span->_prev = nullptr;

            // 释放 span 给 page cache 时，使用 page cache 的锁就可以了
            // 这时把桶锁解掉
            _spanLists[index]._mtx.unlock();

            PageCache::GetInstance()->_pageMtx.lock();
            PageCache::GetInstance()->ReleaseSpanToPageCache(span);
            PageCache::GetInstance()->_pageMtx.unlock();

            _spanLists[index]._mtx.lock();
        }

        start = next;
    }

    _spanLists[index]._mtx.unlock();
}