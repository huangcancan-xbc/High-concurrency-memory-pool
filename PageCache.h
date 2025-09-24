#pragma once
#include "Common.h"
#include "ObjectPool.h"
#include "PageMap.h"

class PageCache
{
public:
	static PageCache* GetInstance()
	{
		return &_sInst;
	}

	// 获取从对象到 span 的映射
	Span* MapObjectToSpan(void* obj);

	// 释放空间 span 回到 PageCache，并合并相邻的 span
	void ReleaseSpanToPageCache(Span* span);

	// 获取一个 k 页的 Span
	Span* NewSpan(size_t k);

	std::mutex _pageMtx;
private:
	SpanList _spanLists[NPAGES];
	ObjectPool<Span> _spanPool;

	//std::unordered_map<PAGE_ID, Span*> _idSpanMap;
	//std::map<void*, Span*> _idSpanMap;
	TCMalloc_PageMap1<32 - PAGE_SHIFT> _idSpanMap;

	PageCache() {}

	PageCache(const PageCache&) = delete;
	static PageCache _sInst;
};