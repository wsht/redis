/**
 * 字典又称为符号表(symbol table)、关联数组(associative array)、或映射(map)，是一种用于保存键值对(key-value pair)
 * 的抽象数据结构。例如: redis中的所有key 到value的映射，即使通过字典结构维护，还有hash类型的键值
 * 
 * Redis的字典是通过hash表实现的，一个hash表有多个节点，每一个节点保存一个键值对
 * 
 * HSET user name mike
 * HSET user passwd 123456
 * HLEN user //user 就是一个包含三个键值对的hash键
 * HGETALL user
 */

/**
 * 好的hash函数具有以下两个好的特点
 * 1.hash函数是可以的。
 * 2.具有雪崩效应，意思是，输入值1bit位的变化，会造成输出值1/2的bit位变化
 * 
 */

#include <stdint.h>

#ifndef __DICT_H
#define __DICT_H

#define DICT_OK 0
#define DICT_ERR 1

/* Unused arguments generate annoying warning*/
#define DICT_NOTUSED(V) ((void)V)


typedef struct dictEntry{
    void *key;
    union{
        void *val;
        uint64_t u64;
        int64_t s64;
        double d;
    } v;
    struct dictEntry *next; //指向下一个hash节点， 用来解决hash键冲突问题
}dictEntry;

/**
 * dictType类型保存着 操作字典不同类型key和value的放的的指针
 */
typedef struct dictType{
    uint64_t (*hashFunction)(const void *key); //计算hash值的函数
    void *(*keyDup)(void *privdata, const void *key); //复制key的函数
    void *(*valDup)(void *privdata, const void *obj); //复制value的函数
    int (*keyCompare)(void *privdata, const void *key1, const void *key2);//比较key的函数
    void (*keyDesturctor)(void *privdata, void *key); //销毁key的函数
    void (*valDesturctor)(void *privdata, void *obj);//销毁value的函数
}dictType;

/**
 * This is our hash table structure. Every dictionary ahs two of this as we
 * implement incremental rehashing, for the old to the new table;
 */
typedef struct dictht{
    dictEntry **table; //存放一个数组的地址，数组存放着哈希表节点dictEntry的地址
    unsigned long size; //哈希表table的大侠，初始化大下为4
    unsigned long sizemask;//用于将hash值映射到table的位置索引，其值总为 size-1;
    unsigned long used; //急速哈希表已有的节点(键值对)数量。
}dictht;

typedef struct dict{
    dictType *type; //指向dictType结构，
    void *privdata;//私有数据，保存着dictType结构中函数的参数
    dictht ht[2];//两张hash表
    long rehashidx; /* rehashing not in progress if rehashidx == -1 rehash的标记，如果为-1表示没有进行rehash*/
    unsigned long iterators; /*number of iterators currently running*/
}dict;

typedef struct dictIterator{
    dict *d;
    long index;
    int table, safe;
    dictEntry *entry, *nextEntry;
    /* unsafe iterator fingerprint for misuse detection. */
    long long fingerprint;
}dictIterator;

typedef void (dictScanFunction)(void *privdata, const dictEntry *de);
typedef void (dictScanBucketFunction)(void *privdata, dictEntry *bucketref);

//定义hash table 初始值大小
#define DICT_HT_INITAL_SIZE 4;

#define dictFreeVal(d, entry) \
    if((d)->type->valDesturctor)\
        (d)->type->valDesturctor((d)->privdata, (entry)->v.val)

#define dictSetVal(d, entry, _val_) do{\
    if((d)->type->valDup)\
        (entry)->v.val = (d)->type->valDup((d)->privdata, _val_);\
    else\
        (entry)->v.val = (_val_);
}while(0);

#define dictSetSignedIntegerVal(entry, _val_)\
    do{ (entry)->v.s64 = _val_; }while(0);

#define dictSetUnsignedIntegerVal(entry, _val_)\
    do{ (entry)->v.u64 = _val_; }while(0);

#define dictSetDoubleVal(entry, _val_)\
    do{ (entry)->v.d = _val_; }while(0);

#define dictFreeKey(d, entry)\
    if((d)->type->keyDesturctor)\
        (d)->type->keyDesturctor((d)->privdata, (entry)->key);

#define dictSetKey(d, entry, _key_) do{\
    if((d)->type->keyDup)\
        (entry)->key = (d)->type->keyDup((d)->privdata, _key_);\
    else\
        (entry)->key = _key_;\
}while(0)

#define dictCompareKeys(d, key1, key2)\
    (((d)->type->keyCompare) ? \
        (d)->type->keyCompare((d)->privdata, key1, key2) :\
        (key1) == (key2))

#define dictHashKey(d, key) (d)->type->hashFunction(key)
#define dictGetKey(he) ((he)->key)
#define dictGetVal(he) ((he)->v.val)
#define dictGetSignedIntegerVal(he) ((he)->v.s64)
#define dictGetUnsignedIntegerVal(he) ((he)->v.u64)
#define dictGetDoubleVal(he) ((he)->v.d)
#define dictSlots(d) ((d)->ht[0].size+(d)->ht[1].size)
#define dictSize(d) ((d)->ht[0].used+(d)->ht[1].used)
#define dictIsRehashing(d) ((d)->rehashidx != -1)

//TODO API

/* Hash table types */
extern dictType dictTypeHeapStringCopyKey;
extern dictType dictTypeHeapStrings;
extern dictType dictTypeHeapStringCopyKeyValue;

#endif