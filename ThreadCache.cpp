#include "ThreadCache.h"
#include "CentralCache.h"

// 线程局部存储实例只定义一次，避免跨编译单元重复
thread_local ThreadCache* pTLSThreadCache = nullptr;

void* ThreadCache::FetchFromCentralCache(size_t index, size_t size)
{
	// 慢开始反馈调节算法
	// 1. 最开始不会一次向 central cache 一次批量要太多，因为太多可能用不完
	// 2. 如果不要这个 size 大小内存需求，那么 batchNum 就会不断增长，直到上限
	// 3. size 越大，一次向 central cache 要的 batchNum 就越小
	// 4. size 越小，一次向 central cache 要的 batchNum 就越大
	// 批量大小受桶阈值和全局上限双重约束，避免一次拿太多
	size_t batchNum = min(_freeLists[index].MaxSize(), SizeClass::NumMoveSize(size));
	if (_freeLists[index].MaxSize() == batchNum)
	{
		// 逐步放大批量，常用 size 会越来越“省锁”
		_freeLists[index].MaxSize() += 1;
	}

	void* start = nullptr;
	void* end = nullptr;

	// CentralCache 只在桶锁范围内批量取，减少锁持有时间
	size_t actualNum = CentralCache::GetInstance()->FetchRangeObj(start, end, batchNum, size);
	assert(actualNum > 0);

	if (actualNum == 1)
	{
		assert(start == end);
		return start;
	}
	else
	{
		// 留一个给当前请求，其余挂到本线程 freelist，减少后续锁开销
		_freeLists[index].PushRange(NextObj(start), end, actualNum - 1);
		return start;
	}

}


void* ThreadCache::Allocate(size_t size)
{
	assert(size <= MAX_BYTES);

	// 对齐后的 size 决定桶大小，原始 size 只用于算桶号
	size_t alignedSize = SizeClass::RoundUp(size);
	size_t index = SizeClass::Index(size);

	if (!_freeLists[index].Empty())
	{
		return _freeLists[index].Pop();
	}
	else
	{
		// 本地没货才去中心缓存，尽量走无锁路径
		return FetchFromCentralCache(index, alignedSize);
	}
}

void ThreadCache::Deallocate(void* ptr, size_t size)
{
	assert(ptr);
	assert(size <= MAX_BYTES);

	// 找出对应的自由链表桶，将对象插入进去
	size_t index = SizeClass::Index(size);
	_freeLists[index].Push(ptr);

	// 当链表长度大于一次批量申请的内存时就开始还一段 list 给 central cache
	if (_freeLists[index].Size() >= _freeLists[index].MaxSize())
	{
		// 防止线程独占过多内存，留给其他线程用
		ListTooLong(_freeLists[index], size);
	}
}

void ThreadCache::ListTooLong(FreeList& list, size_t size)
{
	void* start = nullptr;
	void* end = nullptr;

	// 批量归还，减少反复加锁
	list.PopRange(start, end, list.MaxSize());
	CentralCache::GetInstance()->ReleaseListToSpans(start, size);
}