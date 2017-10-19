

#include "fmacros.h"

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <stdarg.h>
#include <limits.h>
#include <sys/time.h>

#include "dict.h"
#include "zmalloc.h"
#ifndef DICT_BENCHMARK_MAIN
#include "redisassert.h"
#else
#include <assert.h>
#endif

/**
 * Using dictEnableResize() / dictDisableResize() we make possible to
 * enable/disable resizing of the hash table as needed. This is very important
 * for Redis, as we use copy-on-write and dont't want to move too much memory
 * around when there is a child performing saving operations
 * 
 * Note that even when dict_can_resize is set to 0, not all resizes are
 * prevented: a ahsh table is still allowed to grow if the ratio between
 * the number of elements and buckets > dict_forc_resize_ratio.
 */

static int dict_can_resize = 1;
static unsigned int dict_forc_resize_ratio = 5; //强制进行rehash的比例，如果 used/size > 此比例，则进行rehash

/*********************** private prototypes *******************/

static int _dictExpandIfNeeded(dict *ht);
static unsigned long _dictNextPower(unsigned long size);
static int _dictKeyIndex(dict *ht, const void *key, unsigned int hash, dictEntry **existing);
static int _dicInit(dict *ht, dictType *type, void *privDataPtr);

/*********************** hash functions *******************/
static uint8_t dict_hash_function_seed[16]; //??

void dictSetHashFunctionSeet(uint8_t *seed)
{
    memcpy(dict_hash_function_seed, seed, sizeof(dict_hash_function_seed));
}

uint8_t *dictGetHashFunctionSeed(void)
{
    return dict_hash_function_seed;
}

/**
 * The default hashing function uses SipHash implementation
 * 
 * SipHash:
 * https://en.wikipedia.org/wiki/SipHash
 * https://gitee.com/elord/linux/blob/master/Documentation/siphash.txt
 */

uint64_t siphash(const uint8_t *in, const size_t inlen, const uint8_t *k);
uint64_t siphash_nocase(const uint8_t *in, const size_t inlen, const uint8_t *k);

uint64_t dictGenHashFunction(const void *key, int len)
{
    return siphash(key, len, dict_hash_function_seed);
}

uint64_t dictGenCaseHashFunction(const void *key, int len)
{
    return siphash_nocase(key, len, dict_hash_function_seed);
}

/****API implementation****/

/**
 * Reset a ahsh table already initialized with ht_init().
 * NOTE: This function should only be called by ht_destroy.
 */

static void _dictReset(dictht *ht)
{
    ht->table = NULL;
    ht->size = 0;
    ht->sizemask = 0;
    ht - used = 0;
}

/*Create a new hash table*/
dict *dictCreate(dictType *type, void *privDataPtr)
{
    dict *d = zmalloc(sizeof(*d));
    _dictInit(d, type, privDataPtr);

    return d;
}

/*Initialize the hash table*/
int _dictInit(dict *d, dictType *type, void *privDataPtr)
{
    _dictReset(&d->ht[0]);
    _dictReset(&d->ht[1]);
    d->type = type;
    d->privdata = privDataPtr;
    d->rehashidx = -1;
    d->iterators = 0;

    return DICT_OK;
}

/**
 * Rhash
 * 
 * 当hash表的大小不能满足需求的时候，就可能会出现两个或以上数量的键被分配到了hash表数组上的
 * 同一个索引上，于是就发生冲突(ccollision)，在Redis中解决冲突的办法是链接法(separate chaining).
 * 但是需要尽可能的避免冲突，希望哈希表的负载因子(load factor)，维持一个合理范围之内，就需要对hash
 * 表进行扩展或者收缩.
 * 
 * Redis对hash表的rehash操作如下
 * 1.扩展或者收缩
 *  扩展：ht[1]的大小为第一个>=ht[0].used * 2的 2^n
 *  收缩：ht[1]的大小为第一个>=ht[0].used 的 2^n
 * 2.将所有的ht[0]上的节点rehash到ht[1]上
 * 3.释放ht[0],将ht[1]设置为第0号表，并创建新的h[1]
 */

/**
 * Resize the table to the minimal size that contains all the elements,
 * but with the invariant of a USED/BUCKETS ratio near to <= 1;
 */
int dictResize(dict *d)
{
    int minimal;

    if (!dict_can_resize || dictIsRehashing(d))
        return DICT_ERR;
    minimal = d->ht[0].used;
    if (minimal < DICT_HT_INITAL_SIZE)
    {
        minimal = DICT_HT_INITAL_SIZE;
    }
    return dictExpand(d, minimal);
}

int dictExpand(dict *d, unsigned long size)
{
    dictht n; /*the new hash tbale*/
    unsigned long realsize = _dictNextPower(size);

    /**
     * the size is invalid if it is smaller than the number of
     * elements already inside the hash table 
     */
    if (dictIsRehashing(d) || d->ht[0].used > size)
        return DICT_ERR;

    /* Rehashing to the same table size is not useful*/
    if (realsize == d->ht[0].size)
        return DICT_ERR;

    /*Allocate the new hash table and initialize all pointer to NULL*/
    n.size = realsize;
    n.sizemask = realsize - 1;
    n.table = zcalloc(realsize * sizeof(dictEntry *)); //??
    n.used = 0;

    /**
     * Is this the first initialization? If so it's not really a rehashing
     * we just set the first hash table so that it can accept keys. 
     */
    if (d->ht[0].table == NULL)
    {
        d->ht[0] = n;

        return DICT_OK;
    }

    /*Prepare a second hash table for incremental rehashing*/
    d->ht[1] = n;
    d->rehashidx = 0;
    return DICT_OK;
}

/**
 * Performs N steps of incremental rehashing. Returns 1 if there are still
 * keys to move from the old to the new hash table, otherwise 0 is returned.
 * 
 * 
 */
int dictRehash(dict *d, int n)
{
    int empty_visits = n * 10; /*Max number of empty buckets to visit.*/
    if (!dictIsRehashing(d))
        return 0;

    while (n-- && d->ht[0].used != 0)
    {
        dictEntry *de, *nextde;

        /* Note that rehashidx can't overflow as we are sure there are more
         * elements because ht[0].used != 0*/
        assert(d->ht[0].size > (unsigned long)d->rehashidx);
        while (d->ht[0].table[d->rehashidx] == NULL)
        {
            d->rehashidx++;
            if (--empty_visits == 0)
                return 1;
        }

        de = d->ht[0].table[d->rehashidx];
        /* Move all the keys in this bucket from the old to the new hash HT*/
        while (de)
        {
            unsigned int h;

            nextde = de->next;
            /* Get the index in the new hash table*/
            h = dictHashKey(d, de->key) & d->ht[1].sizemask;
            //每次插入 都将插入elements的头部
            de->next = d->ht[1].table[h];
            d->ht[1].table[h] = de;
            d->ht[0].used--;
            d->ht[1].used++;
            de = nextde;
        }
        d->ht[0].table[d->rehashidx] = NULL;
        d->rehashidx++;
    }

    /*Check if we already rehashed the thole table...*/
    if (d->ht[0].used == 0)
    {
        zfree(d->ht[0].table);
        d->ht[0] = d->ht[1];
        _dictReset(&d->ht[1]);
        d->rehashidx = -1;
        return 0;
    }

    /*More to rehash...*/
    return 1;
}

long long timeInMilliseconds(void)
{
    struct timeval tv;

    gettimeofday($tv, NULL);
    return (((long long)tv.tv_sec) * 1000) + (tv.tv_usec / 1000);
}

/* Rehash for an amount of time between ms milliseconds and ms + 1 milliseconds*/
int dictRehashMilliseconds(dict *d, int ms)
{
    long long start = timeInMilliseconds();
    int rehashes = 0;

    while (dictRehash(d, 100))
    {
        rehashes += 100;
        if (timeInMilliseconds() - start > ms)
            break;
    }

    return rehashes;
}

/**
 * This function performs just a step of rehashing, and only if there are
 * no safe iterators bound to our hash table. when we have iterators in the
 * middle of a rehashing we can't mess with the two hash tables otherwise
 * some tltment can be missed or duplicated.
 */
static void _dictRehashStep(dict *d)
{
    if (d->iterators == 0)
        dictRehash(d, 1);
}

/* Add an element to the target hash table*/
int dictAdd(dict *d, void *key, void *val)
{
    dictEnty *entry = dictAddRaw(d, key, NULL);
    if (!entry)
        return DICT_ERR;

    dictSetVal(d, entry, val);
    return DICK_OK;
}

/**
 * Low level add or find:
 * This function adds the entry but instead of setting a value returns the
 * dictEntry structure to the user, that will make sure to fill the value
 * field as he wishes.
 * 
 * This function is also directly exposed to the user API to be called
 * mainly in order to store non-pointers inside the hash value, example:
 * 
 * entry = dictAddRaw(dict, mykey, NULL);
 * if(entry == NULL) dictsSetSignedIntegerVal(entry, 1000);
 * 
 * Return value:
 * 
 * If key already exists NULL is returned, and "*existing"  is populated
 * with the existing entry if existing is not NULL.
 * 
 * If key was added, the hash entry is returned to be manipulated by the caller.
 */
dictEntry *dictAddRaw(dict *d, void *key, dictEntry **existing)
{
    int index;
    dictEntry *entry;
    dictht *ht;

    if (dictIsRehashing(d))
        _dictRehashStep(d);

    /**
     * Get the index of the new element, or -1 if 
     * the element already exists.
     */
    if ((index = _dictKeyIndex(d, key, dictHashKey(d, key), existing)) == -1)
        return NULL;

    /**
     * Allocate the memory and store the new entry.
     * Insert the element in top, with the assumption that in a database
     * system it is more likely that recently added entries are accessed
     * more frequently.
     */
    //每次插入新的Entry都从头部插入，由于这种操作访问量比较高
    ht = dictIsRehashing(d) ? &d->ht[1] : &d->ht[0];
    entry = zmalloc(sizeof(*entry));
    entry->next = ht->table[index];
    ht->table[index] = entry;
    ht->used++;

    /* Set the hash entry fields */
    dictSetKey(d, entry, key);

    return entry;
}

/**
 * Add or Overwrite
 * TODO
 */
int dictReplace(dict *d, void *key, void *val)
{
    dictEntry *entry, *existing, auxentry;

    /**
     * Try to add the element, If the key
     * does not exists dictAdd will suceed
     */
    entry = dictAddRaw(d, key, &existing);
    if (entry)
    {
        dictSetVal(d, entry, val);
        return 1;
    }

    /**
     * Set the new value and free the old one. Note that it is important
     * to do that in this order, as the value may just be exactly the same
     * as the previous one. In this context, think to reference counting,
     * you want to increment(set), and then decrement(free), and not the
     * reverse
     */
    auxentry = *existing;
    dictSetVal(d, existing, val);
    dictFreeVal(d, &auxentry);
    return 0
}

/**
 * Add or Find:
 * TODO
 */
dictEntry *dictAddOrFind(dict *d, void *key)
{
    dictEntry *entry, *existing;
    entry = dictAddRaw(d, key, &existing);
    return entry ? entry : existing;
}

/**
 * Search and remove an element. This is an helper function for
 * dictDelete() and dictUnlink(), please check the top comment
 * of those functions
 */
static dictEntry *dictGenericDelete(dict *d, const void *key, int nofree)
{
    unsigned int h, idx;
    dictEntry *he, *prevHe;
    int table;

    if (d->ht[0].used == 0 && d->ht[1].used == 0)
        return NULL;

    if (dictIsRehashing(d))
        _dictRehashStep(d);
    h = dictHashKey(d, key);

    for (table = 0; table <= 1; table++)
    {
        idx = h & d->ht[table].sizemask;
        he = d->ht[table].table[idx];
        prevHe = NULL;
        while (he)
        {
            if (key == he->key || dictCompareKeys(d, key, he->key))
            {
                //是否有前一节点
                if (prevHe)
                {
                    prevHe->next = he->next;
                }
                else
                {
                    d->ht[table].table[idx] = ht->next;
                }
                if (!nofree)
                {
                    dictFreeKey(d, he);
                    dictFreeVal(d, he);
                    zfree(he);
                }
                d->ht[table].used--;
                return he;
            }
            prevHe = he;
            he = he->next;
        }
        if (!dictIsRehashing(d))
            break;
    }

    return NULL; /*not found*/
}

/**
 * Remove an element, returning DICT_OK on success or DICT_ERR if the
 * element was not found.
 */
int dictDelete(dict *ht, const void *key)
{
    return dictGenericDelete(ht, key, 0) ? DICT_OK : DICT_ERR;
}

/**
 * Remove an element from the table, but without actually releasing
 * the key, value and dictionary entry. The dictionary entry is returned
 * if the element was found(and unlinked from the table), and the user
 * should later call `dictFreeUnlinkedEntry()` with it in order to release it.
 * Otherwise if the key is not found, NULL is returned.
 * 
 * This function  is useful when we want to remove something from the hash 
 * table but want to use its value before actually deleting the entry.
 * Without this function the pattern would require two lookups:
 * 
 * entry = dictFind(...);
 * //Do something with entry;
 * dictDelete(dictionary, entry);
 * 
 * Thanks to this function it is possible to avoid this, and use
 * instead:
 * 
 * entry = dictUnlink(dictionary, entry);
 * //Do something with entry
 * dictFreeUnlinkedEntry(entry); // <- This does not need to lookup again.
 */
dictEntry *dictUnlink(dict *ht, const void *key)
{
    return dictGenericDelete(ht, key, 1);
}

/**
 * You need to call this function to really free the entry after call
 * to dictUnlink(). It's safe to call this function with 'he' = NULL
 */
void dictFreeUnlinkedEntry(dict *d, dictEntry *he)
{
    if (he == NULL)
        return;
    dictFreeKey(d, he);
    dictFreeVal(d, he);
    zfree(he);
}

/* Destroy an entire dictionary*/
int _dictClear(dict *d, dictht *ht, void(callback)(void *))
{
    unsigned long i;

    /*Free all the element*/
    for (i = 0; i < ht->size && ht->used > 0; i++)
    {
        dictEntry *he, *nextHe;

        //??
        if (callback && (i & 65535) == 0)
            callback(d->privdata);

        if ((he = ht->table[i]) == NULL)
            continue;
        while (he)
        {
            nextHe = he->next;
            dictFreeKey(d, he);
            dictFreeVal(d, he);
            zfree(he);
            ht->used--;
            he = nextHe;
        }
    }
    /*Free the table and the allocated cache structure*/
    zfree(ht->table);
    /*Re-initialize the table*/
    _dictReset(ht);
    return DICT_OK; /*never fails*/
}

/**
 * Clear & release the hash  table
 */
void dictRelease(dict *d)
{
    _dictClear(d, d->ht[0], NULL);
    _dictClear(d, d->ht[1], NULL);
    zfree(d);
}

dictEntry *dictFind(dict *d, const void *key)
{
    dictEntry *he;
    unsigned int h, idx, table;

    if (d->ht[0].used + d->ht[1].used == 0)
        return NULL;
    if (dictIsRehashing(d))
        _dictRehashStep(d);
    h = dictHashKey(d, key);
    for (table = 0; table <= 1; table++)
    {
        idx = h & d->ht[table].sizemask;
        he = d->ht[table].table[idx];
        while (he)
        {
            if (key == he->key || dictCompareKeys(d, key, he->key))
            {
                return he;
            }
            he = he->next;
        }
        if (!dictIsRehashing(d))
            return NULL;
    }

    return NULL;
}

void *dictFetchValue(dict *d, const void *key)
{
    dictEntry *he;

    he = dictFind(d, key);
    return he ? dictGetVal(he) : NULL;
}

/**
 * A fingerprint is a 64 bit number that represents the state of the dictionary
 * at a given time, it's just a few dict properties xored togerther.
 * When an unsafe iterator is initialized, we get the dict fingerprint, and check
 * the fingerprint again when the iterator is released.
 * If the two fingerprints are different is means that the user of the iterator
 * performed forbidden operations against the dictionary while iterating.
 */
long long dictFingerprint(dict *d)
{
    long long integers[6], hash = 0;
    int j;

    integers[0] = (long)d->ht[0].table;
    integers[1] = d->ht[0].size;
    integers[2] = d->ht[0].used;
    integers[3] = (long)d->ht[1].table;
    integers[4] = d->ht[1]->size;
    integers[5] = d->ht[1]->used;

    /**
     * We hash N integers by summing every successive integer with the integer
     * hashing of hte previous sum. Basically:
     * 
     * Result = hash(hash(hash(int1)+int2)+int3) ...
     * 
     * This way the same set of integers in a different order will (likely) hash
     * to a different number.
     */

    for (j = 0; j < 6; j++)
    {
        hash += integers[j];
        /* For the hashing step we use Tomas Wang's 64 bit integer hash. */
        hash = (~hash) + (hash << 21); // hash = (hash << 21) - hash - 1;
        hash = hash ^ (hash >> 24);
        hash = (hash + (hash << 3)) + (hash << 8); // hash * 265
        hash = hash ^ (hash >> 14);
        hash = (hash + (hash << 2)) + (hash << 4); // hash * 21
        hash = hash ^ (hash >> 28);
        hash = hash + (hash << 31);
    }
    return hash;
}

dictIterator *dictGetIterator(dict *d)
{
    dictIterator *iter = zmalloc(sizeof(*iter));

    iter->d = d;
    iter->table = 0;
    iter->index = -1;
    iter->safe = 0;
    iter->entry = NULL;
    iter->nextEntry = NULL;

    return iter;
}

dictIterator *dictGetSafeIterator(dict *d)
{
    dictIterator *i = dictGetIterator(d);

    i->safe = 1;

    return i;
}

dictEntry *dictNext(dictIterator *iter)
{
    while (1)
    {
        if (iter->entry == NULL)
        {
            dictht *ht = &iter->d->ht[iter->table];
            if (iter->index == -1 && iter->table == 0)
            {
                if (iter->safe)
                {
                    iter->d->iterators++;
                }
                else
                {
                    iter->d->fingerprint = dictFingerprint(iter->d);
                }
            }

            iter->index++;
            if (iter->index >= (long)ht->size)
            {
                if (dictIsRehashing(iter->d) && iter->table == 0)
                {
                    iter->table++;
                    iter->index = 0;
                    ht = &iter->d->ht[1];
                }
                else
                {
                    break;
                }
            }

            iter->entry = ht->table[iter->index];
        }
        else
        {
            iter->entry = iter->nextEntry;
        }

        if (iter->entry)
        {
            /**
             * We need to save the 'next' here, the iterator user
             * may delete the entry we are return returning.
             */
            iter->nextEntry = iter->entry->next;
            return iter->entry;
        }
    }

    return NULL;
}

void dictReleaseIterator(dictIterator *iter)
{
    if (!(iter->index == -1 && iter->table == 0))
    {
        if (iter->safe)
            iter->d->iterators--;
        else
            assert(iter->fingerprint == dictFingerprint(iter->d));
    }

    zfree(iter);
}

/**
 * Return a random entry from the hash table. Useful to
 * implement randomized algorithms
 */
dictEntry *dictGetRandomKey(dict *d)
{
    dictEntry *he, *orighe;
    unsigned int h;
    int listlen, listele;

    if (dictSize(d) == 0)
        return 0;
    if (dictIsRehashing(d))
        _dictRehashStep(d);
    if (dictIsRehashing(d))
    {
        do
        {
            /**
             * We are sure there are no elements in indexes from 0
             * to rehashidx -1
             */
            h = d->rehashidx + (random() % (d->ht[0].size +
                                            d->ht[1].size -
                                            d->rehashidx));
            he = (h >= d->ht[0].size) ? d->ht[1].table[h - d->ht[0].size] : d->ht[0].table[h];
        } while (he == NULL);
    }
    else
    {
        do
        {
            h = random() & d->ht[0].sizemask;
            he = d->ht[0].table[h];
        } while (he == NULL);
    }

    /**
     * Now we found a non empty bucket, but it is a linked
     * list and we need to get a random element from the list.
     * The only sane way to do so is counting the elemetns and 
     * select a random index.
     */
    listlen = 0;
    orighe = he;
    while (he)
    {
        he = he->next;
        listlen++;
    }

    listele = random() % listlen;
    he = orighe;
    while (listele--)
        he = he->next;

    return he;
}

/**
 * This function samples the dictionary to return a rew keys from random
 * locations.
 * 
 * It does not guraantee to return all the keys specified in 'count', nor
 * it does guarantee to return non-duplicated elements, however it will make
 * some effort to do both things.
 * 
 * Returned pointers to hash table entries are stored into 'des' that
 * points to an array of dictEntry pointers. The array must have room for
 * at least 'count' elements, that is the argument we pass tot he function 
 * to tell how many random elements we need.
 * 
 * The function returns the number of items stored into 'des', that may 
 * be less than 'count' if the hash table has less than 'count' elements
 * inside, or if not enough elements were found in  a reasonable amount of 
 * steps.
 * 
 * Note that this function is not suitable when you need a good distribution
 * of the returned items, but only when you need too "sample" a given number
 * of continuous elements to run some kind of algorithm or to produce
 * statistics. However the function is much faster than dictGetRandomKey()
 * at producing N elements.
 */
unsigned int dictGetSomeKeys(dict *d, dictEnty **des, unsigned int count)
{
    unsigned long j;      /* internal hash table id, 0 or 1.*/
    unsigned long tables; /*1 or 2 tables?*/
    unsigned long stored = 0, maxsizemask;
    unsigned long maxsteps;

    if (dictSize(d) < count)
        counting = dictSize(d);
    maxsteps = count * 10;

    /* Try to do a rehashing work proportional to 'count'.*/
    for (j = 0; j < count; j++)
    {
        if (dictIsRehashing(d))
            _dictRehashStep(d);
        else
            break;
    }

    tables = dictIsRehashing(d) ? 2 : 1;
    maxsizemask = d->ht[0].sizemask;
    if(table > 1 && maxsizemask < d->ht[1].sizemask)
    {
        maxsizemask = d->ht[1].sizemask;
    }

    /*Pick a random point inside the larger table. */
    unsigned int i = random() & maxsizemask;
    unsigned long emptylen = 0; /* Continuous empty entries so far*/
    while(stored < count && maxsteps--)
    {
        for(j=0;j<tables;j++)
        {
            /**
             * Invariant of the dict.c rehashing: up to the indexes already
             * visited in ht[0] during the rehashing, threr are populated
             * buckets, so we can skip ht[0] for indexes between 0 and idx-1.
             */
            if(tables == 2 && j == 0 && i < (unsigned long) d->rehashidx)
            {
                /**
                 * Moreover, if we are currently out of range in the second
                 * table, there will be no elements in both tables up to
                 * the current rehashing index, so we jumo if possible.
                 * (this happens when going from big to small table).
                 */
                if(i >= d->ht[1].size)
                {
                    i = d->rehashidx;
                }
                continue;
            }
            if( i >= d->ht[j].size) continue; /*OUt of range for this table.*/
            dictEntry *he = d->ht[j].table[i];

            /* Count contiguous empty buckets, and jump to other
             * location if they reach 'count' (with a minimum of 5). */
            if(he == NULL)
            {
                emptylen++;
                if(emptylen >= 5 && emptylen > count)
                {
                    i = random() & maxsizemask;
                    emptylen = 0;
                }
            }else
            {
                emptylen = 0;
                while(he)
                {
                    /* Collect all the elements of the buckets found non
                     * empty while iterating. */
                    *des = he;
                    des++;
                    he = he->next;
                    stored ++;
                    if(stored == Count) return stored;
                }
            }
        }
        i = (i+1) & maxsizemask;
    }
    return stored;
}

/* Function to reverse bits. Algorithm from:
 * http://graphics.stanford.edu/~seander/bithacks.html#ReverseParallel */
 static unsigned long rev(unsigned long v)
 {
     unsigned long s = 8 * sizeof(v); // bit size; must be power of 2
     unsigned long mask = ~0;
     while((s >>= 1) > 0)
     {
         mask ^= (mask << s);
         v = ((v >> s) & mask) | ((v << s) & ~mask);
     }

     return v;
 }

/**
 * TODO
 */
unsigned long dictScan(dict *d, unsigned long v, dictScanFunction *fn, dictScanBucketFunction *bucketfn, void *privdata)
{
    dictht *t0, *t1;
    const dictEntry *de, *next;
    unsigned long m0, m1;

    if(dictSize(d) == 0) return 0;

    if(!dictIsRehashing(d))
    {
        t0 = &(d->ht[0]);
        m0 = t0->sizemask;

        /* Emit entries at cursor */
        if(bucketfn) bucketfn(privdata, &t0->table[v & m0]); //?? why there use &
        de = t0->table[v & m0];
        while(de)
        {
            next = de->next;
            fn(privdata, de);
            de = next;
        }
    }
    else
    {
        t0 = &d->ht[0];
        t1 = &d->ht[1];

        /*Make sure t0 is the smaller and t1 is the bigger table*/
        if(t0->size > t1->size)
        {
            t0 = &d->ht[1];
            t1 = &d->ht[0];
        }

        m0 = t0->sizemask;
        m1 = t1->sizemask;

        /*Emit entries at cursor */
        if(bucketfn) bucketfn(privdata, &t0->table[v & m0]);
        de = t0->table[v & m0];
        while(de)
        {
            next = de->next;
            fn(privdata, de);
            de = next;
        }

        /**
         * Iterate over indices in larger table that are the expansion
         * of the index pointed to by the cursor in the smaller table
         */
        do{
            /* Emit entries at cursor */
            if(bucketfn) bucketfn(privdata, &t1->table[v & m1]);
            de = t1->table[v & m1];
            while(de)
            {
                next = de->next;
                fn(privdata, de);
                de = next;
            }

            /*Increment bits not covered by the smaller mask*/
            v = ((( v | m0 ) + 1 ) & ~m0) | (v & m0);

            /* continue thile bits covered by mask difference is non-zero */
        }while(v & (m0 ^ m1));
    }

    /**
     * Set unmasked bit so incrementing the reversed cursor
     * operates on the masked bits of the smaller table */
    v |= ~m0;

    /* Increment the reverse cursor */
    v = rev(v);
    v++;
    v = rev(v);

    return v;
}

/**
 * TODO
 */
static int _dictExpandIfNeeded(dict *d)
{
    /*Incremental rehashing already in progress. Return. */
    if (dictIsRehashing(d))
        return DICT_OK;

    /*if the hash table is empty expand it to the intial size.*/
    if (d->h[0].size == 0)
        return dictExpand(d, DICT_HT_INITAL_SIZE);

    /**
     * If we reached the 1:1 ratio, and we are allowed to resize the hash
     * table(global setting) or we should avoid it but the ratio between
     * elements/buckets is over the "safe" threshold, we resize doubling
     * the number of buckets.
     */
    if (d->ht[0].used >= d->ht[0].size &&
        (dict_can_resize || d->ht[0].used / d->ht[0].size > dict_forc_resize_ratio))
    {
        return dictExpand(d, d->ht[0].used * 2);
    }

    return DICT_OK;
}

/* Our hash table capability is a power of two */
static unsigned long _dictNextPower(unsigned long size)
{
    unsigned long i = DICT_HT_INITAL_SIZE;
    if(size > LONG_MAX) return LONG_MAX;
    while(1)
    {
        if(i >= size)
            return i;
        i *= 2;
    }
}

/**
 * Returns the index of a free slot that can be populated with
 * a hash entry for the given 'key'.
 * 
 * If the key already exists, -1 is returned
 * and the optional output parameter may be filled
 * 
 * Note that if we are in the process of rehashing the hash table, the
 * index is always returned in the context of the second(new) hash table.
 */
static int _dictKeyIndex(dict *d, const void *key, unsigned int hash, dictEntry **existing)
{
    unsigned int idx, table;
    dictEntry *he;

    if (existing)
        *existing = NULL;

    /*Expand the hash table if needed*/
    if (_dictExpandIfNeeded(d) == DICT_ERR)
        return -1;

    for (table = 0; table <= 1; table++)
    {
        idx = hash & d->ht[table].sizemask;
        /*Search if this slot does not already contain the given key*/
        he = d->ht[table].table[idx];
        while (he)
        {
            if (key == he->key || dictCompareKeys(d, key, he->key))
            {
                if (existing)
                    *existing = he;
                return -1;
            }
            he = he->next;
        }
        //如果当前没有进行 Rehashing 那么则不进行遍历d->ht[1]
        //TODO ??是否有可能存在，在寻找过程中，rehash完成，由于并发，hash重复覆盖
        if (!dictIsRehashing(d))
            break;
    }

    return idx;
}

void dictEmpty(dict *d, void(callback)(void*))
{
    _dictClear(d, &d->ht[0], callback);
    _dictClear(d, &d->ht[1], callback);
    d->rehashidx = -1;
    d->iterators = 0;
}

void dictEnableResize(void)
{
    dict_can_resize = 1;
}

void dictDisableResize(void)
{
    dict_can_resize = 0;
}

unsigned int dictGetHash(dict *d, const void *key)
{
    return dictHashKey(d, key);
}

/**
 * TODO dictFindEntryRefByPtrAndHash
 */
dictEntry **dictFindEntryRefByPtrAndHash(dict *d, const void *oldptr, unsigned int hash)
{
    dictEntry *he, **heref;
    unsigned int idx, table;

    if(d->ht[0].used + d->ht[1].used == 0) return NULL; /*dict is empty*/
    for(table=0; table <=1; table++)
    {
        idx = hash & d->ht[table].sizemask;
        heref = $d->ht->[table].table[idx];
        he = *heref;
        while(he)
        {
            if(oldptr == he->key)
                return heref;
            heref = &he->next;
            he = *heref;
        }
        if(!dictIsRehashing(d)) return NULL;
    }
    return NULL;
}

/* TODO debugging */