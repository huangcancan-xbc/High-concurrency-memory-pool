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

	// 全局页级锁，保护页表和空闲 span 列表
	std::mutex _pageMtx;
private:
	// 建立页号到 span 的映射
	void MapSpan(Span* span);
	// 清理页号映射，避免悬挂
	void UnmapSpan(Span* span);

	// 按页数分桶管理空闲 span
	SpanList _spanLists[NPAGES];
	// span 元数据对象池，避免频繁 new/delete
	ObjectPool<Span> _spanPool;

	//std::unordered_map<PAGE_ID, Span*> _idSpanMap;
	//std::map<void*, Span*> _idSpanMap;
#ifdef _WIN64
	// 64 位地址空间需要更大页号映射，避免 PageMap 越界/失效
	static constexpr int kPageIdBits = 48 - PAGE_SHIFT;
	TCMalloc_PageMap3<kPageIdBits> _idSpanMap;
#else
	TCMalloc_PageMap1<32 - PAGE_SHIFT> _idSpanMap;
#endif

	PageCache() {}

	PageCache(const PageCache&) = delete;
	static PageCache _sInst;
};