#pragma once
#include "Common.h"
#include "ObjectPool.h"
#include <cstdint>
#include <cstring>

// 1. 单层数组实现
// 适合 32 位地址空间：速度快，但内存占用固定
template <int BITS>
class TCMalloc_PageMap1
{
private:
    // 固定长度数组，避免运行期扩容
    static const int LENGTH = 1 << BITS;
    void** array_;

public:
    typedef uintptr_t Number;

    //explicit TCMalloc_PageMap1(void* (*allocator)(size_t)) {
    // 一次性分配，查找 O(1)
    explicit TCMalloc_PageMap1()
    {
        //array_ = reinterpret_cast<void**>((*allocator)(sizeof(void*) << BITS));
        size_t size = sizeof(void*) << BITS;
        size_t alignSize = SizeClass::_RoundUp(size, 1 << PAGE_SHIFT);
        array_ = (void**)SystemAlloc(alignSize >> PAGE_SHIFT);
        memset(array_, 0, sizeof(void*) << BITS);
    }

    // 返回 key 的当前值，如果没设置，或者k超出范围，返回NULL
    // 超界直接返回空，避免野指针访问
    void* get(Number k) const
    {
        if ((k >> BITS) > 0)
        {
            return NULL;
        }

        return array_[k];
    }

    //要求“k”处于“[0, 2 ^ bits - 1]”范围内
    //要求“k”之前已验证
    //为键“k”设置值“v”
    // set 时按需创建路径，避免一次性占用大内存
    void set(Number k, void* v)
    {
        array_[k] = v;
    }
};




// 2. 两层基数树实现
// 适合更大地址空间：按需分配叶子，节省内存
template <int BITS>
class TCMalloc_PageMap2
{
private:
    // 在根节点中放入32个条目，在每个叶节点中放入(2^bits)/32个条目。
    static const int ROOT_BITS = 5;
    static const int ROOT_LENGTH = 1 << ROOT_BITS;

    static const int LEAF_BITS = BITS - ROOT_BITS;
    static const int LEAF_LENGTH = 1 << LEAF_BITS;

    // 叶节点
    struct Leaf
    {
        void* values[LEAF_LENGTH];
    };

    Leaf* root_[ROOT_LENGTH];            // 指向32个子节点的指针
    void* (*allocator_)(size_t);         // 内存分配器

public:
    typedef uintptr_t Number;

    //explicit TCMalloc_PageMap2(void* (*allocator)(size_t)) {
    // 预留根节点，叶子按需创建
    explicit TCMalloc_PageMap2()
    {
        //allocator_ = allocator;
        memset(root_, 0, sizeof(root_));

        PreallocateMoreMemory();
    }

    // 超界直接返回空，避免野指针访问
    void* get(Number k) const
    {
        const Number i1 = k >> LEAF_BITS;
        const Number i2 = k & (LEAF_LENGTH - 1);
        if ((k >> BITS) > 0 || root_[i1] == NULL)
        {
            return NULL;
        }

        return root_[i1]->values[i2];
    }

    // set 时按需创建路径，避免一次性占用大内存
    void set(Number k, void* v)
    {
        const Number i1 = k >> LEAF_BITS;
        const Number i2 = k & (LEAF_LENGTH - 1);
        assert(i1 < ROOT_LENGTH);
        root_[i1]->values[i2] = v;
    }

    // 确保区间内叶子已建立，避免访问时频繁判断
    bool Ensure(Number start, size_t n)
    {
        for (Number key = start; key <= start + n - 1; )
        {
            const Number i1 = key >> LEAF_BITS;

            // 检查溢出
            if (i1 >= ROOT_LENGTH)
            {
                return false;
            }

            // 如果有必要，创建二级节点
            if (root_[i1] == NULL)
            {
                //Leaf* leaf = reinterpret_cast<Leaf*>((*allocator_)(sizeof(Leaf)));
                //if (leaf == NULL) return false;
                static ObjectPool<Leaf> leafPool;
                Leaf* leaf = (Leaf*)leafPool.New();

                memset(leaf, 0, sizeof(*leaf));
                root_[i1] = leaf;
            }

            // 将键向前移动，越过此叶节点所覆盖的任何内容
            key = ((key >> LEAF_BITS) + 1) << LEAF_BITS;
        }

        return true;
    }

    // 一次性预分配全量页号，减少运行期分配
    void PreallocateMoreMemory()
    {
        // 分配足够的资源来跟踪所有可能的页
        Ensure(0, 1 << BITS);
    }
};




// 3. 三层基数树实现
// 适合 64 位大地址空间：分层更细，内存更省
template <int BITS>
class TCMalloc_PageMap3
{
private:
    // 在每个内部层级，我们应该消耗多少比特？
    static const int INTERIOR_BITS = (BITS + 2) / 3;  // Round-up
    static const int INTERIOR_LENGTH = 1 << INTERIOR_BITS;

    // 在叶子节点级别，我们应该消耗多少位？
    static const int LEAF_BITS = BITS - 2 * INTERIOR_BITS;
    static const int LEAF_LENGTH = 1 << LEAF_BITS;

    // 内部节点
    struct Node
    {
        Node* ptrs[INTERIOR_LENGTH];
    };

    // 叶节点
    struct Leaf
    {
        void* values[LEAF_LENGTH];
    };

    Node* root_;        // 基数树的根节点

    // 节点来自对象池，避免频繁 malloc
    static Node* NewNode()
    {
        static ObjectPool<Node> nodePool;
        Node* result = nodePool.New();

        if (result != NULL)
        {
            memset(result, 0, sizeof(*result));
        }

        return result;
    }

    // 叶子同样走对象池，减少碎片
    static Leaf* NewLeaf()
    {
        static ObjectPool<Leaf> leafPool;
        Leaf* result = leafPool.New();

        if (result != NULL)
        {
            memset(result, 0, sizeof(*result));
        }

        return result;
    }

public:
    typedef uintptr_t Number;

    explicit TCMalloc_PageMap3()
    {
        root_ = NewNode();
    }

    // 超界直接返回空，避免野指针访问
    void* get(Number k) const
    {
        const Number i1 = k >> (LEAF_BITS + INTERIOR_BITS);
        const Number i2 = (k >> LEAF_BITS) & (INTERIOR_LENGTH - 1);
        const Number i3 = k & (LEAF_LENGTH - 1);

        if ((k >> BITS) > 0 ||
            root_->ptrs[i1] == NULL ||
            root_->ptrs[i1]->ptrs[i2] == NULL) {
            return NULL;
        }

        return reinterpret_cast<Leaf*>(root_->ptrs[i1]->ptrs[i2])->values[i3];
    }

    // set 时按需创建路径，避免一次性占用大内存
    void set(Number k, void* v)
    {
        assert((k >> BITS) == 0);
        const Number i1 = k >> (LEAF_BITS + INTERIOR_BITS);
        const Number i2 = (k >> LEAF_BITS) & (INTERIOR_LENGTH - 1);
        const Number i3 = k & (LEAF_LENGTH - 1);

        if (root_->ptrs[i1] == NULL)
        {
            root_->ptrs[i1] = NewNode();
        }

        if (root_->ptrs[i1]->ptrs[i2] == NULL)
        {
            root_->ptrs[i1]->ptrs[i2] = reinterpret_cast<Node*>(NewLeaf());
        }

        reinterpret_cast<Leaf*>(root_->ptrs[i1]->ptrs[i2])->values[i3] = v;
    }

    // 确保区间内叶子已建立，避免访问时频繁判断
    bool Ensure(Number start, size_t n)
    {
        for (Number key = start; key <= start + n - 1;)
        {
            const Number i1 = key >> (LEAF_BITS + INTERIOR_BITS);
            const Number i2 = (key >> LEAF_BITS) & (INTERIOR_LENGTH - 1);

            // 检查溢出情况
            if (i1 >= INTERIOR_LENGTH || i2 >= INTERIOR_LENGTH)
            {
                return false;
            }

            // 如果有必要，创建二级节点
            if (root_->ptrs[i1] == NULL)
            {
                Node* n = NewNode();

                if (n == NULL)
                {
                    return false;
                }

                root_->ptrs[i1] = n;
            }

            // 必要时创建叶节点
            if (root_->ptrs[i1]->ptrs[i2] == NULL)
            {
                Leaf* leaf = NewLeaf();

                if (leaf == NULL)
                {
                    return false;
                }

                root_->ptrs[i1]->ptrs[i2] = reinterpret_cast<Node*>(leaf);
            }

            // 将键前进到超过此叶节点所覆盖的全部内容
            key = ((key >> LEAF_BITS) + 1) << LEAF_BITS;
        }

        return true;
    }

    // 一次性预分配全量页号，减少运行期分配
    void PreallocateMoreMemory()
    {
        // 无操作：预分配通过Ensure()处理
    }
};