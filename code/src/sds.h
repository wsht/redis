#ifndef __SDS_H__
#define __SDS_H__

#define SDS_MAX_PREALLOC (1024*1024) //预分配内存 最大长度为1MB

#include <sys/types.h>
#include <stdarg.h>
#include <stdint.h>

typedef char *sds;

/**
 * 
 * 参考资料
 * https://www.2cto.com/database/201610/552614.html
 * http://blog.csdn.net/wpf199402076118/article/details/51767527?locationNum=1
 * http://blog.csdn.net/men_wen/article/details/69396550
 * /
 

/* Note: sdshdr5 is never used, we just access the flags byte directly.
 * However is here to document the layout of type 5 SDS strings. */
struct __attribute__ ((__packed__)) sdshdr5 {
    unsigned char flags; /* 3 lsb of type, and 5 msb of string length */ /*类型标记 低三位表示类型，高五位暂时没有使用*/
    char buf[];
};

struct __attribute__ ((__packed__)) sdshdr8{
    uint8_t len; //表示使用长度
    uint8_t alloc; //表示分配的空间，不包括头空间以及null结尾
    unsigned char flags;
    char buf[];
}

struct __attribute__ ((__packed__)) sdshdr16 {
    uint16_t len; /* used */
    uint16_t alloc; /* excluding the header and null terminator */
    unsigned char flags; /* 3 lsb of type, 5 unused bits */
    char buf[];
};
struct __attribute__ ((__packed__)) sdshdr32 {
    uint32_t len; /* used */
    uint32_t alloc; /* excluding the header and null terminator */
    unsigned char flags; /* 3 lsb of type, 5 unused bits */
    char buf[];
};
struct __attribute__ ((__packed__)) sdshdr64 {
    uint64_t len; /* used */
    uint64_t alloc; /* excluding the header and null terminator */
    unsigned char flags; /* 3 lsb of type, 5 unused bits */
    char buf[];
};

#define SDS_TYPE_5 0
#define SDS_TYPE_8 1
#define SDS_TYPE_16 2
#define SDS_TYPE_32 3
#define SDS_TYPE_64 4

#define SDS_TYPE_MASK 7
#define SDS_TYPE_BITS 3

//获取SDS头部指针
//其中s是指针，其指向地址为buf数组
//而sizeof(struct sdshdr##T)计算结果可以理解为结构体sdshdr的到buf的偏移量（结构体是连续分配的内存空间），
//那么&sdshdr + sizeof(struct sdshdr) == s
//所以下列计算方法可以获得保存此buf的 struct sdshdr##T的指针
//具体可见 learn_c/pointer/offset.c 

//相关教程
#define SDS_HDR_VAR(T,s) struct sdshdr##T *sh = (void*)((s)-(sizeof(struct sdshdr##T)));

