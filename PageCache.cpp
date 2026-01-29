#include "PageCache.h"

PageCache PageCache::_sInst;

void PageCache::MapSpan(Span* span)
{
    // 维护每一页到 span 的映射，保证任意页内指针可定位
    for (PAGE_ID i = 0; i < span->_n; i++)
    {
        _idSpanMap.set(span->_pageId + i, span);
    }
}

void PageCache::UnmapSpan(Span* span)
{
    // 释放大块内存前清理映射，避免悬挂指针
    for (PAGE_ID i = 0; i < span->_n; i++)
    {
        _idSpanMap.set(span->_pageId + i, nullptr);
    }
}

// 获取一个 k 页的 Span
// 先复用已有空闲 span，不够再向系统申请
Span* PageCache::NewSpan(size_t k)
{
	assert(k > 0);

	// 大于 128 页的直接向堆申请
	if (k > NPAGES - 1)
	{
		void* ptr = SystemAlloc(k);
		//Span* span = new Span;
		Span* span = _spanPool.New();
		span->_pageId = (PAGE_ID)ptr >> PAGE_SHIFT;
		span->_n = k;

		// 大块 span 也要建立完整页映射，避免 64 位下 MapObjectToSpan 失效
		MapSpan(span);

		return span;
	}

	// 先检查第 k 个桶里面有没有 span
	if (!_spanLists[k].Empty())
	{
		 Span* kSpan = _spanLists[k].PopFront();

		// 建立 id 和 span 的映射，方便 central cache 回收小块内存时，查找对应的 span
		MapSpan(kSpan);

		return kSpan;
	}

	// 检查一下后面的桶里面有没有 span ，如果有可以把它进行切分
	for (size_t i = k + 1; i < NPAGES; i++)
	{
		if (!_spanLists[i].Empty())
		{
			Span* nSpan =_spanLists[i].PopFront();
			//Span* kSpan = new Span;
			Span* kSpan = _spanPool.New();

			// 再 nSpan 的头部切一个 k 页下来
			// k 页 span 返回
			// nSpan 再挂到对应的映射位置
			kSpan->_pageId = nSpan->_pageId;
			kSpan->_n = k;

			nSpan->_pageId += k;
			nSpan->_n -= k;

			_spanLists[nSpan->_n].PushFront(nSpan);
			// free span 也维护完整页映射，便于合并与定位
			MapSpan(nSpan);

			// 建立 id 和 span 的映射，方便 central cache 回收小块内存时，查找对应的 span
			MapSpan(kSpan);

			return kSpan;
		}
	}

	// 走到这个位置就说明后面没有更大的 span 了
	// 这时就要去堆要一个 128 页的 span
	//Span* bigSpan = new Span;
	Span* bigSpan = _spanPool.New();
	void* ptr = SystemAlloc(NPAGES - 1);
	bigSpan->_pageId = (PAGE_ID)ptr >> PAGE_SHIFT;
	bigSpan->_n = NPAGES - 1;

	// 维护 page -> span 映射，保证合并查找正确
	MapSpan(bigSpan);

	_spanLists[bigSpan->_n].PushFront(bigSpan);
	return NewSpan(k);
}


// 通过页号快速定位 span，回收时必须 O(1)
Span* PageCache::MapObjectToSpan(void* obj)
{
	PAGE_ID id = ((PAGE_ID)obj >> PAGE_SHIFT);

	// 保护页表读写，避免并发更新导致数据竞争
	std::lock_guard<std::mutex> lock(_pageMtx);

	auto ret = (Span*)_idSpanMap.get(id);
	assert(ret != nullptr);
	return ret;
}

void PageCache::ReleaseSpanToPageCache(Span* span)
{
	// 大于 128 页的直接还给堆
	if (span->_n > NPAGES - 1)
	{
		// 释放前清理映射，避免悬挂指针
		UnmapSpan(span);

		void* ptr = (void*)(span->_pageId << PAGE_SHIFT);
		SystemFree(ptr);
		//delete span;
		_spanPool.Delete(span);
	
		return;
	}

	// 对 span 前后的页尝试进行合并，缓解内存碎片问题
	while (1)
	{
		PAGE_ID prevId = span->_pageId - 1;
		//auto ret = _idSpanMap.find(prevId);

		//// 前面的页号没有，不合并了
		//if (ret == _idSpanMap.end())
		//{
		//	break;
		//}


		auto ret = (Span*)_idSpanMap.get(prevId);
		if (ret == nullptr)
		{
			break;
		}

		// 前面相邻页的 span 还在使用，不合并了
		Span* prevSpan = ret;
		if (prevSpan->_isUse == true)
		{
			break;
		}

		// 合并出超出 128 页的 span 没办法管理，不合并了
		if (prevSpan->_n + span->_n > NPAGES - 1)
		{
			break;
		}

		span->_pageId = prevSpan->_pageId;
		span->_n += prevSpan->_n;

		_spanLists[prevSpan->_n].Erase(prevSpan);
		//delete prevSpan;
		_spanPool.Delete(prevSpan);
	}

	// 向后合并
	while (1)
	{
		PAGE_ID nextId = span->_pageId + span->_n;
		//auto ret = _idSpanMap.find(nextId);

		//// 后面的页号没有，不合并了
		//if (ret == _idSpanMap.end())
		//{
		//	break;
		//}

		auto ret = (Span*)_idSpanMap.get(nextId);
		if (ret == nullptr)
		{
			break;
		}


		Span* nextSpan = ret;
		if (nextSpan->_isUse == true)
		{
			break;
		}

		// 合并出超出 128 页的 span 没办法管理，不合并了
		if (nextSpan->_n + span->_n > NPAGES - 1)
		{
			break;
		}

		span->_n += nextSpan->_n;

		_spanLists[nextSpan->_n].Erase(nextSpan);
		//delete nextSpan;
		_spanPool.Delete(nextSpan);
	}

	_spanLists[span->_n].PushFront(span);
	span->_isUse = false;

	// 合并后更新所有页到 span 的映射
	MapSpan(span);
}