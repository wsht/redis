#ifndef __SDS_H__
#define __SDS_H__

#define SDS_MAX_PREALLOC (1024 * 1024) //预分配内存 最大长度为1MB

#include <sys/types.h>
#include <stdarg.h>
#include <stdint.h>

/**
 * 
 * 参考资料
 * https://www.2cto.com/database/201610/552614.html
 * http://blog.csdn.net/wpf199402076118/article/details/51767527?locationNum=1
 * http://blog.csdn.net/men_wen/article/details/69396550
 */

/**
  * 
  * Redis兼容传统的C语言字符串类型，但是没有直接使用C语言传统的字符串(以' \0' 结尾的字符数组)表示,
  * 而是自己构建了一种名为简单动态字符串(simple dynamic string, SDS)的对象。
  * 例如，键值对在底层就是由SDS实现的
  * SET
    GET
    STRLEN
  */

//sds 存放sdshdr结构buf成员的地址
typedef char *sds;

/**
 * sds也存在一个表头(header) 来存放sds的信息
 * 
 * 优点：
 * 1.兼容C的部分函数
 * 
 * 2.二进制安全：传统的C字符串符合ASCII编码，这种编码的操作特点是：遇零则止。那么只要遇到' \0'结尾，那么就认为到达末尾
 * ' \0'结尾以后的所有字符串都被忽略。 因此，如果传统字符串保存图片，视频等二进制文件，操作文件时候，就被截断了
 * 而SDS表头的buf被定义为字节数组，其结尾依照表头len成员来标记。这意味着他可以存放任何二进制的数据和文本数据 包括' \0'
 * 
 * 3.获取字符串长度的操作复杂度为O(1)
 * 传统的C字符串获得长度时的做法：遍历字符串长度，遇0则止，复杂度为O(n)
 * 而SDS表头的类成员保存着字符串长度，其复杂度为O(1)
 * 
 * 4.杜绝缓冲区溢出
 * 因为SDS表头的alloc以及len记录着buf分配的空间和已经使用的空间，所以在进行APPEND命令向字符串后追加字符串时候，如果不够，
 * 会先进行内存扩展
 */

/* Note: sdshdr5 is never used, we just access the flags byte directly.
 * However is here to document the layout of type 5 SDS strings. */
struct __attribute__((__packed__)) sdshdr5
{
    unsigned char flags; /* 3 lsb of type, and 5 msb of string length */
    char buf[];
};

struct __attribute__((__packed__)) sdshdr8
{
    uint8_t len;         //表示使用长度
    uint8_t alloc;       //表示分配的空间，不包括头空间以及null结尾
    unsigned char flags; /*类型标记 低三位表示类型，高五位暂时没有使用*/
    char buf[];          //初始化sds分配的数据空间，而且是柔性数组（https://coolshell.cn/articles/11377.html）
}

struct __attribute__((__packed__)) sdshdr16
{
    uint16_t len;        /* used */
    uint16_t alloc;      /* excluding the header and null terminator */
    unsigned char flags; /* 3 lsb of type, 5 unused bits */
    char buf[];
};
struct __attribute__((__packed__)) sdshdr32
{
    uint32_t len;        /* used */
    uint32_t alloc;      /* excluding the header and null terminator */
    unsigned char flags; /* 3 lsb of type, 5 unused bits */
    char buf[];
};
struct __attribute__((__packed__)) sdshdr64
{
    uint64_t len;        /* used */
    uint64_t alloc;      /* excluding the header and null terminator */
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
#define SDS_HDR_VAR(T, s) struct sdshdr##T *sh = (void *)((s) - (sizeof(struct sdshdr##T))); //获取SDS 头部变量
#define SDS_HDR(T, s) ((struct sdshdr##T *)((s) - (sizeof(struct sdshdr##T))))
#define SDS_TYPE_5_LEN(f) ((f) >> SDS_TYPE_BITS)

static inline size_t sdslen(const sds s)
{
    //指针向前一位，获取flags的值
    unsigned char flags = s[-1];
    switch (flags & SDS_TYPE_MASK)
    {
    case SDS_TYPE_5:
        return SDS_TYPE_5_LEN(flags);
    case SDS_TYPE_8:
        return SDS_HDR(8, s)->len;
    case SDS_TYPE_16:
        return SDS_HDR(16, s)->len;
    case SDS_TYPE_32:
        return SDS_HDR(32, s)->len;
    case SDS_TYPE_64:
        return SDS_HDR(64, s)->len;
    }

    return 0;
}

//获取sds当前有效空间
static inline size_t sdsavail(const sds s)
{
    unsigned char flags = s[-1];
    switch (flags & SDS_TYPE_MASK)
    {
    case SDS_TYPE_5:
        return 0;
    case SDS_TYPE_8:
        SDS_HDR_VAR(8, s);
        return sh->alloc - sh->len;
    case SDS_TYPE_16:
        SDS_HDR_VAR(16, s);
        return sh->alloc - sh->len;
    case SDS_TYPE_32:
        SDS_HDR_VAR(32, s);
        return sh->alloc - sh->len;
    case SDS_TYPE_64:
        SDS_HDR_VAR(64, s);
        return sh->alloc - sh->len;
    }

    return 0;
}

//设置 sds->len的长度
static inline void sdssetlen(sds s, size_t newlen)
{
    unsigned char flags = s[-1];
    switch (flags & SDS_TYPE_MASK)
    {
    //此类型 没有设置len 仅仅是更新一下flags
    case SDS_TYPE_5:
        {
            //获取flags pointer
            unsigned char *fp = ((unsigned char *)s) - 1;

            *fp = SDS_TYPE_5 | (newlen << SDS_TYPE_BITS);
        }
        break;
    case SDS_TYPE_8:
        SDS_HDR(8, s)->len = newlen;
        break;
    case SDS_TYPE_16:
        SDS_HDR(8, s)->len = newlen;
        break;
    case SDS_TYPE_32:
        SDS_HDR(8, s)->len = newlen;
        break;
    case SDS_TYPE_64:
        SDS_HDR(8, s)->len = newlen;
        break;
    }
}

// sds s->len 增加 inc个长度
static inline void sdsinclen(sds s, size_t inc)
{
    unsigned char flags = s[-1];
    switch (flags & SDS_TYPE_MASK)
    {
    //todo 了解type 5的意义
    case SDS_TYPE_5:
        {
            //获取flags pointer
            unsigned char *fp = ((unsigned char *)s) - 1;
            unsigned char newlen = SDS_TYPE_5_LEN(flags) + inc
                                                            *fp = SDS_TYPE_5 | (newlen << SDS_TYPE_BITS);
        }
        break;
    case SDS_TYPE_8:
        SDS_HDR(8, s)->len += inc;
        break;
    case SDS_TYPE_16:
        SDS_HDR(8, s)->len += inc;
        break;
    case SDS_TYPE_32:
        SDS_HDR(8, s)->len += inc;
        break;
    case SDS_TYPE_64:
        SDS_HDR(8, s)->len += inc;
        break;
    }
}

//获取sds分配的空间 = sdsavail() + sdslen()
static inline size_t sdsalloc(const sds s)
{
    unsigned char flags = s[-1];
    switch (flags & SDS_TYPE_MASK)
    {
    case SDS_TYPE_5:
        return SDS_TYPE_5_LEN(flags);
    case SDS_TYPE_8:
        return SDS_HDR(8, s)->alloc;
    case SDS_TYPE_16:
        return SDS_HDR(16, s)->alloc;
    case SDS_TYPE_32:
        return SDS_HDR(32, s)->alloc;
    case SDS_TYPE_64:
        return SDS_HDR(64, s)->alloc;
    }

    return 0;
}

static inline void sdssetalloc(sds s, size_t newlen)
{
    unsigned char flags = s[-1];
    switch(flags & SDS_TYPE_MASK)
    {
    case SDS_TYPE_5:
        //nothing to do ,this type has no total allocation info
        break;
    case SDS_TYPE_8:
        SDS_HDR(8, s)->alloc = newlen;
        break;
    case SDS_TYPE_16:
        SDS_HDR(16, s)->alloc = newlen;
        break;
    case SDS_TYPE_32:
        SDS_HDR(32, s)->alloc = newlen;
        break;
    case SDS_TYPE_64:
        SDS_HDR(64, s)->alloc = newlen;    
        break;
    }
}
