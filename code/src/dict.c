

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
static unsigned int dict_forc_resize_ratio = 5;

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
    ht-used = 0;
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

    if(!dict_can_resize || dictIsRehashing(d)) return DICT_ERR;
    minimal = d->ht[0].used;
    if(minimal < DICT_HT_INITAL_SIZE)
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
    if(dictIsRehashing(d) || d->ht[0].used > size)
        return DICT_ERR;

    /* Rehashing to the same table size is not useful*/
    if(realsize == d->ht[0].size) return DICT_ERR;

    /*Allocate the new hash table and initialize all pointer to NULL*/
    n.size = realsize;
    n.sizemask = realsize-1;
    n.table = zcalloc(realsize * sizeof(dictEntry*)); //??
    n.used = 0;

    /**
     * Is this the first initialization? If so it's not really a rehashing
     * we just set the first hash table so that it can accept keys. 
     */
    if(d->ht[0].table == NULL)
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
    int empty_visits = n * 10;/*Max number of empty buckets to visit.*/
    if(!dictIsRehashing(d)) return 0;

    while(n-- && d->ht[0].used != 0)
    {
        dictEntry *de, *nextde;

        /* Note that rehashidx can't overflow as we are sure there are more
         * elements because ht[0].used != 0*/
        assert(d->ht[0].size > (unsigned long)d->rehashidx);
        while(d->ht[0].table[d->rehashidx] == NULL)
        {
            d->rehashidx++;
            if(--empty_visits == 0) return 1;
        }

        de = d->ht[0].table[d->rehashidx];
        /* Move all the keys in this bucket from the old to the new hash HT*/
        while(de)
        {
            unsigned int h;

            nextde = de->next;
            /* Get the index in the new hash table*/
            h = dictHashKey(d, de->key) & d->ht[1].sizemask;
            //每次插入 都将插入elements的头部
            de->next = d->ht[1].table[h];
            d->ht[1].table[h] = de;
            d->ht[0].used --;
            d->ht[1].used ++;
            de = nextde;
        }
        d->ht[0].table[d->rehashidx] = NULL;
        d->rehashidx++;
    }

    /*Check if we already rehashed the thole table...*/
    if(d->ht[0].used == 0)
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

long long timeInMilliseconds(void){
    struct timeval tv;
    
    gettimeofday($tv,NULL);
    return (((long long)tv.tv_sec)*1000)+(tv.tv_usec/1000);
}

/* Rehash for an amount of time between ms milliseconds and ms + 1 milliseconds*/
int dictRehashMilliseconds(dict *d, int ms)
{
    long long start = timeInMilliseconds();
    int rehashes = 0;

    while(dictRehash(d,100)){
        rehashes += 100;
        if(timeInMilliseconds()-start > ms) break;
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
    if(d->iterators == 0) dictRehash(d, 1);
}

/* Add an element to the target hash table*/
int dictAdd(dict *d, void *key, void *val)
{
    dictEnty *entry = dictAddRaw(d,key,NULL);
    if(!entry) return DICT_ERR;

    dictSetVal(d, entry, val);
    return DICK_OK;
}

dictEntry *dictAddRaw(dict *d, void *key, dictEntry **existing){
    int index;
    dictEntry *entry;
    dictht *ht;

    if(dictIsRehashing(d)) _dictRehashStep(d);

    /**
     * Get the index of the new element, or -1 if 
     * the element already exists.
     */
    if((index = _dictKeyIndex(d, key, dictHashKey(d,key), existing)) == -1)
        return NULL;
    
    /**
     * Allocate the memory and store the new entry.
     * Insert the element in top, with the assumption that in a database
     * system it is more likely that recently added entries are accessed
     * more frequently.
     */
    ht = dictIsRehashing(d) ? &d->ht[1] : &d->ht[0];
    entry = zmalloc(sizeof(*entry));
    entry->next = ht->table[index];
    ht->table[index] = entry;
    ht->used++;

    /* Set the hash entry fields */
    dictSetKey(d, entry, key);
    
    return entry;
}

static int _dictExpandIfNeeded(dict *d)
{
    /*Incremental rehashing already in progress. Return. */
    if(dictIsRehashing(d)) return DICT_OK;
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

    if(existing) *existing = NULL;

    /*Expand the hash table if needed*/
    if(_dictExpandIfNeeded(d) == DICT_ERR)
        return -1;
    
    for(table = 0; table <= 1; table++)
    {
        idx = hash & d->ht[table].sizemask;
        /*Search if this slot does not already contain the given key*/
        he = d->ht[table].table[idx];
        while(he){
            if(key == he->key || dictCompareKeys(d,key,he->key))
            {
                if(existing) *existing = he;
                return -1;
            }
            he = he->next;
        }
        //如果当前没有进行 Rehashing 那么则不进行遍历d->ht[1]
        //TODO ??是否有可能存在，在寻找过程中，rehash完成，由于并发，hash重复覆盖
        if(!dictIsRehashing(d)) break;
    }
    
    return idx;
}