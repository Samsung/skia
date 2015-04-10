/*
 * Copyright 2013 Google Inc.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "SkDiscardableMemory.h"
#include "SkDiscardableMemoryPool.h"
#include "SkImageGenerator.h"
#include "SkLazyPtr.h"
#include "SkTInternalLList.h"
#include "SkThread.h"
#include "SkTime.h"

#define SK_DEFAULT_CACHEABLE_THRESHOLD 256 * 1024

static const size_t gDiscardableMemoryLimits[] = {
    256 * 1024,
    512 * 1024,
   1024 * 1024,
   2048 * 1024,
   4096 * 1024
};

enum DiscardableMemoryLimits {
    k256K_Limit = 0,
    k512K_Limit,
    k1M_Limit,
    k2M_Limit,
    k4M_Limit,
    kOther_Limit
};

#define MAX_ELAPSED_TIME_IN_MSECS 3000

// Note:
// A PoolDiscardableMemory is memory that is counted in a pool.
// A DiscardableMemoryPool is a pool of PoolDiscardableMemorys.

namespace {

class PoolDiscardableMemory;

/**
 *  This non-global pool can be used for unit tests to verify that the
 *  pool works.
 */
class DiscardableMemoryPool : public SkDiscardableMemoryPool {
public:
    /**
     *  Without mutex, will be not be thread safe.
     */
    DiscardableMemoryPool(size_t budget, SkBaseMutex* mutex = NULL);
    virtual ~DiscardableMemoryPool();

    virtual SkDiscardableMemory* create(size_t bytes) SK_OVERRIDE;

    virtual size_t getRAMUsed() SK_OVERRIDE;
    virtual void setRAMBudget(size_t budget) SK_OVERRIDE;
    virtual size_t getRAMBudget() SK_OVERRIDE { return fBudget; }

    virtual void setCacheableThreshold(size_t threshold) SK_OVERRIDE { fCacheableThreshold = threshold; }
    virtual size_t getCacheableThreshold() SK_OVERRIDE { return fCacheableThreshold; }

    /** purges all unlocked DMs */
    virtual void dumpPool() SK_OVERRIDE;

    #if SK_LAZY_CACHE_STATS  // Defined in SkDiscardableMemoryPool.h
    virtual int getCacheHits() SK_OVERRIDE { return fCacheHits; }
    virtual int getCacheMisses() SK_OVERRIDE { return fCacheMisses; }
    virtual void resetCacheHitsAndMisses() SK_OVERRIDE {
        fCacheHits = fCacheMisses = 0;
    }
    int          fCacheHits;
    int          fCacheMisses;
    #endif  // SK_LAZY_CACHE_STATS

private:
    SkBaseMutex* fMutex;
    size_t       fBudget;
    size_t       fCacheableThreshold;
    size_t       fUsed;
    SkTInternalLList<PoolDiscardableMemory> fLists[kOther_Limit+1];

    /** Function called to free memory if needed */
    void dumpDownTo(size_t budget);
    /** called by DiscardableMemoryPool upon destruction */
    void free(PoolDiscardableMemory* dm);
    /** called by DiscardableMemoryPool::lock() */
    bool lock(PoolDiscardableMemory* dm);
    /** called by DiscardableMemoryPool::unlock() */
    void unlock(PoolDiscardableMemory* dm);

    friend class PoolDiscardableMemory;

    typedef SkDiscardableMemory::Factory INHERITED;
};

/**
 *  A PoolDiscardableMemory is a SkDiscardableMemory that relies on
 *  a DiscardableMemoryPool object to manage the memory.
 */
class PoolDiscardableMemory : public SkDiscardableMemory {
public:
    PoolDiscardableMemory(DiscardableMemoryPool* pool,
                            void* pointer, size_t bytes);
    virtual ~PoolDiscardableMemory();
    virtual bool lock() SK_OVERRIDE;
    virtual void* data() SK_OVERRIDE;
    virtual void unlock() SK_OVERRIDE;
    friend class DiscardableMemoryPool;
private:
    SK_DECLARE_INTERNAL_LLIST_INTERFACE(PoolDiscardableMemory);
    DiscardableMemoryPool* const fPool;
    bool                         fLocked;
    void*                        fPointer;
    const size_t                 fBytes;
    SkMSec                       fTimestamp;
};

PoolDiscardableMemory::PoolDiscardableMemory(DiscardableMemoryPool* pool,
                                             void* pointer,
                                             size_t bytes)
    : fPool(pool)
    , fLocked(true)
    , fPointer(pointer)
    , fBytes(bytes) {
    SkASSERT(fPool != NULL);
    SkASSERT(fPointer != NULL);
    SkASSERT(fBytes > 0);
    fPool->ref();
    fTimestamp = SkTime::GetMSecs();
}

PoolDiscardableMemory::~PoolDiscardableMemory() {
    SkASSERT(!fLocked); // contract for SkDiscardableMemory
    fPool->free(this);
    fPool->unref();
}

bool PoolDiscardableMemory::lock() {
    SkASSERT(!fLocked); // contract for SkDiscardableMemory
    // renew timestamp
    fTimestamp = SkTime::GetMSecs();
    return fPool->lock(this);
}

void* PoolDiscardableMemory::data() {
    SkASSERT(fLocked); // contract for SkDiscardableMemory
    return fPointer;
}

void PoolDiscardableMemory::unlock() {
    SkASSERT(fLocked); // contract for SkDiscardableMemory
    fPool->unlock(this);
}

////////////////////////////////////////////////////////////////////////////////

DiscardableMemoryPool::DiscardableMemoryPool(size_t budget,
                                             SkBaseMutex* mutex)
    : fMutex(mutex)
    , fBudget(budget)
    , fUsed(0) {
    fCacheableThreshold = SK_DEFAULT_CACHEABLE_THRESHOLD;
    #if SK_LAZY_CACHE_STATS
    fCacheHits = 0;
    fCacheMisses = 0;
    #endif  // SK_LAZY_CACHE_STATS
}
DiscardableMemoryPool::~DiscardableMemoryPool() {
    // PoolDiscardableMemory objects that belong to this pool are
    // always deleted before deleting this pool since each one has a
    // ref to the pool.
    for (int i = 0; i < SK_ARRAY_COUNT(fLists); ++i) {
        SkASSERT(fLists[i].isEmpty());
    }
}

void DiscardableMemoryPool::dumpDownTo(size_t budget) {
    if (fMutex != NULL) {
        fMutex->assertHeld();
    }
    if (fUsed <= budget) {
        return;
    }
    typedef SkTInternalLList<PoolDiscardableMemory>::Iter Iter;
    Iter iter;

    int index = 0;

    for (int i = 0; i < SK_ARRAY_COUNT(fLists); ++i) {
        index = 0;
        PoolDiscardableMemory* cur = iter.init(fLists[i], Iter::kTail_IterStart);
        while ((fUsed > budget) && (cur)) {
            if (!cur->fLocked) {
                PoolDiscardableMemory* dm = cur;
                SkASSERT(dm->fPointer != NULL);
                sk_free(dm->fPointer);
                dm->fPointer = NULL;
                SkASSERT(fUsed >= dm->fBytes);
                fUsed -= dm->fBytes;
                cur = iter.prev();
                // Purged DMs are taken out of the list.  This saves times
                // looking them up.  Purged DMs are NOT deleted.
                fLists[i].remove(dm);
            } else {
                cur = iter.prev();
            }
        }
    }

    // we have cleaned fLists, up to index, we continue look for others to
    // to clean if dm is too old
    if (index == SK_ARRAY_COUNT(fLists) - 1) {
        return;
    }

    SkMSec now = SkTime::GetMSecs();
    for (int i = index+1; i < SK_ARRAY_COUNT(fLists); ++i) {
        PoolDiscardableMemory* cur = iter.init(fLists[i], Iter::kTail_IterStart);
        while (cur && (now - cur->fTimestamp > MAX_ELAPSED_TIME_IN_MSECS)) {
            if (!cur->fLocked) {
                PoolDiscardableMemory* dm = cur;
                SkASSERT(dm->fPointer != NULL);
                sk_free(dm->fPointer);
                dm->fPointer = NULL;
                SkASSERT(fUsed >= dm->fBytes);
                fUsed -= dm->fBytes;
                cur = iter.prev();
                // Purged DMs are taken out of the list.  This saves times
                // looking them up.  Purged DMs are NOT deleted.
                fLists[i].remove(dm);
            } else {
                cur = iter.prev();
            }
        }
    }
}

SkDiscardableMemory* DiscardableMemoryPool::create(size_t bytes) {
    void* addr = sk_malloc_flags(bytes, 0);
    if (NULL == addr) {
        return NULL;
    }
    PoolDiscardableMemory* dm = SkNEW_ARGS(PoolDiscardableMemory,
                                             (this, addr, bytes));

    bool found = false;
    int count = SK_ARRAY_COUNT(gDiscardableMemoryLimits);

    if (dm->fBytes > fCacheableThreshold) {
        SkAutoMutexAcquire autoMutexAcquire(fMutex);

        for (int i = 0; i < count; ++i) {
            if (gDiscardableMemoryLimits[i] < dm->fBytes) {
                continue;
            }
            fLists[i].addToHead(dm);
            fUsed += bytes;
            found = true;
            break;
        }

        if (!found) {
            fLists[count].addToHead(dm);
            fUsed += bytes;
        }
        this->dumpDownTo(fBudget);
    }
    return dm;
}

void DiscardableMemoryPool::free(PoolDiscardableMemory* dm) {
    // This is called by dm's destructor.
    if (dm->fPointer != NULL) {
        SkAutoMutexAcquire autoMutexAcquire(fMutex);
        sk_free(dm->fPointer);
        dm->fPointer = NULL;
        if (dm->fBytes > fCacheableThreshold) {
            int count = SK_ARRAY_COUNT(gDiscardableMemoryLimits);
            bool found = false;
            SkASSERT(fUsed >= dm->fBytes);
            fUsed -= dm->fBytes;
            for (int i = 0; i < count; ++i) {
                if (gDiscardableMemoryLimits[i] < dm->fBytes) {
                    continue;
                }
                fLists[i].remove(dm);
                found = true;
                break;
            }
            if(!found) {
                fLists[count].remove(dm);
            }             
        }
    } else {
        int count = SK_ARRAY_COUNT(gDiscardableMemoryLimits);
        bool found = false;
        for (int i = 0; i < count; ++i) {
            if (gDiscardableMemoryLimits[i] < dm->fBytes) {
                continue;
            }
            SkASSERT(!fLists[i].isInList(dm));
            found = true;
            break;
        }
        if (!found) {
          SkASSERT(!fLists[count].isInList(dm));
        }
    }
}

bool DiscardableMemoryPool::lock(PoolDiscardableMemory* dm) {
    SkASSERT(dm != NULL);
    if (NULL == dm->fPointer) {
        #if SK_LAZY_CACHE_STATS
        SkAutoMutexAcquire autoMutexAcquire(fMutex);
        ++fCacheMisses;
        #endif  // SK_LAZY_CACHE_STATS
        return false;
    }
    SkAutoMutexAcquire autoMutexAcquire(fMutex);
    if (NULL == dm->fPointer) {
        // May have been purged while waiting for lock.
        #if SK_LAZY_CACHE_STATS
        ++fCacheMisses;
        #endif  // SK_LAZY_CACHE_STATS
        return false;
    }
    dm->fLocked = true;

    if (dm->fBytes > fCacheableThreshold) {
        dm->fTimestamp = SkTime::GetMSecs();
        int count = SK_ARRAY_COUNT(gDiscardableMemoryLimits);
        bool found = false;
        for (int i = 0; i < count; ++i) {
            if (gDiscardableMemoryLimits[i] < dm->fBytes) {
                continue;
            }
            fLists[i].remove(dm);
            fLists[i].addToHead(dm);
            found = true;
            break;
        }
        if (!found) {
            fLists[count].remove(dm);
            fLists[count].addToHead(dm);
        }
    }
    #if SK_LAZY_CACHE_STATS
    ++fCacheHits;
    #endif  // SK_LAZY_CACHE_STATS

    return true;
}

void DiscardableMemoryPool::unlock(PoolDiscardableMemory* dm) {
    SkASSERT(dm != NULL);
    SkAutoMutexAcquire autoMutexAcquire(fMutex);
    dm->fLocked = false;
    if (dm->fBytes <= fCacheableThreshold) {
        SkASSERT(dm->fPointer != NULL);
        sk_free(dm->fPointer);
        dm->fPointer = NULL;
    } else {
        this->dumpDownTo(fBudget);
    }
}

size_t DiscardableMemoryPool::getRAMUsed() {
    return fUsed;
}
void DiscardableMemoryPool::setRAMBudget(size_t budget) {
    SkAutoMutexAcquire autoMutexAcquire(fMutex);
    fBudget = budget;
    this->dumpDownTo(fBudget);
}
void DiscardableMemoryPool::dumpPool() {
    SkAutoMutexAcquire autoMutexAcquire(fMutex);
    this->dumpDownTo(0);
}

////////////////////////////////////////////////////////////////////////////////
SK_DECLARE_STATIC_MUTEX(gMutex);
SkDiscardableMemoryPool* create_global_pool() {
    return SkDiscardableMemoryPool::Create(SK_DEFAULT_GLOBAL_DISCARDABLE_MEMORY_POOL_SIZE,
                                           &gMutex);
}

}  // namespace

SkDiscardableMemoryPool* SkDiscardableMemoryPool::Create(size_t size, SkBaseMutex* mutex) {
    return SkNEW_ARGS(DiscardableMemoryPool, (size, mutex));
}

SK_DECLARE_STATIC_LAZY_PTR(SkDiscardableMemoryPool, global, create_global_pool);

SkDiscardableMemoryPool* SkGetGlobalDiscardableMemoryPool() {
    return global.get();
}

////////////////////////////////////////////////////////////////////////////////
