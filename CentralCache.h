#pragma once
#include "Common.h"

// 单例模式
class CentralCache
{
public:
	static CentralCache* GetInstance()
	{
		return &_sInst;
	}

	// 获取一个非空的 Span
	Span* GetOneSpan(SpanList& list,size_t size);

	// 从中心缓存获取一定数量的对象给 thread cache
	size_t FetchRangeObj(void*& start, void*& end, size_t batchNum, size_t size);

	// 将一定数量的对象释放到 span 跨度中
	void ReleaseListToSpans(void* start, size_t byte_size);
private:
	// 每个桶维护自己的 SpanList，桶锁在 SpanList 内部
	SpanList _spanLists[NFREELISTS];

private:
	CentralCache()
	{

	}

	CentralCache(const CentralCache&) = delete;

	static CentralCache _sInst;
};