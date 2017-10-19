#ifndef __REDIS_H
#define __REDIS_H

#include "fmacros.h"
#include "config.h"
#include "solarisfixes.h"
#include "rio.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <limits.h>
#include <unistd.h>
#include <errno.h>
#include <inttypes.h>
#include <pthread.h>
#include <syslog.h>
#include <netinet/in.h>
#include <lua.h>
#include <signal.h>

typedef long long mstime_t; /* millisecond time type. */

#include "ae.h"      /* Event driven programming library */
#include "sds.h"     /* Dynamic safe strings */
#include "dict.h"    /* Hash tables */
#include "adlist.h"  /* Linked lists */
#include "zmalloc.h" /* total memory usage aware version of malloc/free */
#include "anet.h"    /* Networking the easy way */
#include "ziplist.h" /* Compact list data structure */
#include "intset.h"  /* Compact integer set structure */
#include "version.h" /* Version macro */
#include "util.h"    /* Misc functions useful in many places */
#include "latency.h" /* Latency monitor API */
#include "sparkline.h" /* ASCII graphs API */
#include "quicklist.h"  /* Lists are encoded as linked lists of
                           N-elements flat arrays */
#include "rax.h"     /* Radix tree */

/* Following includes allow test functions to be called from Redis main() */
#include "zipmap.h"
#include "sha1.h"
#include "endianconv.h"
#include "crc64.h"

#define ZSKIPLIST_MAXLEVEL 32 /* Should be enough for 2^32 elements */
#define ZSKIPLIST_P 0.25      /* Skiplist P = 1/4 */

/**
 * 跳跃表是一个有序链表，其中每个节点包含不定数量的链接，节点中的
 * 第i个链接构成的单项链表跳过含有少于i个链接的节点。
 * 
 * 条约表支持平均O(logN)，最坏情况下O(N)复杂度的节点查找，大部分情况下，
 * 条约表的效率可以和平衡树相媲美
 * 跳跃表在redis中当数据较多时作为有序集合的实现方式之一
 */

/**
 * ZSET use a specialized version of Skiplists 
 */
typedef struct zskiplistNode{
    sds ele;   //动态字符串的地址
    double score; //分值
    struct zskiplistNode *backward;//后跳指针
    struct zskiplistLevel{
        struct zskiplistNode *forward;//前跳指针
        unsigned int span; //跨度
    }level[];//层级，柔型数组
}zskiplistNode;

typedef struct zskiplist{
    struct zskiplistNode *header, *tail;
    unsigned long length;//跳跃表的长度，或者跳跃表节点数量计数器，除去第一个节点
    int level;//跳跃表中节点的最大层级数量，除去第一个点
}zskiplist;

typedef struct zset{
    dict *dict;
    zskiplist *zsl;
}zset;


/* Struct to hold a inclusive/exclusive range spec by score comparison */
typedef struct{
    double min, max;
    int minex, maxex; // are min or max exclusie?
}zrangespec;

/* struct to hole a inclusive/exclusive range spec by lexicographic comparison*/
typedef struct{
    sds min, max; //May be set to shared.(minstring|maxstring)
    int minex, maxex; // are min or max exclusive?
}