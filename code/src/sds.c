#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <assert.h>
#include <limits.h>
#include "sds.h"
#include "sdsalloc.h"

//根据类型获取字符串header 长度
static inline int sdsHdrSize(char type)
{
    switch (type & SDS_TYPE_MASK)
    {
    case SDS_TYPE_5:
        return sizeof(struct sdshdr5);
    case SDS_TYPE_8:
        return sizeof(struct sdshdr8);
    case SDS_TYPE_16:
        return sizeof(struct sdshdr16);
    case SDS_TYPE_32:
        return sizeof(struct sdshdr32);
    case SDS_TYPE_64:
        return sizeof(struct sdshdr64);
    }

    return 0;
}

static inline int sdsReqType(size_t string_size)
{
    if (string_size < 1 << 5)
        return SDS_TYPE_5;
    if (string_size < 1 << 8)
        return SDS_TYPE_8;
    if (string_size < 1 << 16)
        return SDS_TYPE_16;

#if (LONG_MAX == LLONG_MAX)
    if (string_size < 1ll << 32)
        return SDS_TYPE_32;
#endif
    return SDS_TYPE_64;
}

/**
 * Create a new sds string with the content specified by the 'init' pointer
 * and initlen
 * 
 * If NULL is used for 'init' the string is initialized with zero bytes.
 * 
 * the string is always null-termined(all the sds strings are, always) so 
 * even if you create an sds string with:
 * 
 * mystring = sdsnewlen("abc", 3);
 * 
 * you can print the string with printf() as there is an implicit \0 at the 
 * end of the string. However the string is binary safe and can contain
 * \0 characters in the middle, as the length is stored in the sds header.
 * 
 */
sds sdsnewlen(const void *init, size_t initlen)
{
    void *sh;
    sds s;
    char type = sdsReqType(initlen);
    /*Empty strings are usually create in order to append. Use type 8
      since type 5 is not good at this.*/
    if (type == SDS_TYPE_5 && initlen == 0)
        type = SDS_TYPE_8;

    int hdrlen = sdsHdrSize(type);
    unsigned char *fp //flags pointer

        //动态分配内存
        // todo 了解malloc memset 函数，以及内存分配流程
        sh = s_malloc(hdrlen + initlen + 1);
    if (!init)
    {
        //为sh设置内存
        memset(sh, 0, hdrlen + initlen + 1);
    }

    //分配空间失败，返回null
    if (sh == NULL)
        return NULL;
    //使指针指向buf
    s = (char *)sh + hdrlen;
    fp = ((unsigned char *)s) - 1; //flags pointer
    switch (type)
    {
    case SDS_TYPE_5:
    {
        *fp = type | (initlen << SDS_TYPE_BITS);
        break;
    }
    case SDS_TYPE_8:
    {
        SDS_HDR_VAR(8, s);
        sh->len = initlen;
        sh->alloc = initlen;
        *fp = type;
        break;
    }
    case SDS_TYPE_16:
    {
        SDS_HDR_VAR(16, s);
        sh->len = initlen;
        sh->alloc = initlen;
        *fp = type;
        break;
    }
    case SDS_TYPE_32:
    {
        SDS_HDR_VAR(32, s);
        sh->len = initlen;
        sh->alloc = initlen;
        *fp = type;
        break;
    }
    case SDS_TYPE_64:
    {
        SDS_HDR_VAR(64, s);
        sh->len = initlen;
        sh->alloc = initlen;
        *fp = type;
        break;
    }
    }

    //如果init 存在，则进行内存copy
    if (initlen && init)
        memcpy(s, init, initlen);
    //设置结束符号
    s[initlen] = '\0';

    return s;
}

/* Crreate an empty(zero length) sds string. Even in this case the string
 * always ahs an implicit null term. */
sds sdsempty(void)
{
    return sdsnewlen("", 0);
}

/* Create a new sds string starting from a null terminated C string.*/
sds sdsnew(const char *init)
{
    size_t initlen = (init == NULL) ? 0 : sizeof(init);
    return sdsnewlen(init, initlen);
}

sds sdsdup(const sds s)
{
    return sdsnew(s, sdslen(s));
}

void sdsfree(sds s)
{
    if(s == NULL) return;
    s_free( (char*)s -sdsHdrSize(s[-1]) );
}


/**
 * Set the sds string length to the length as obtained with strlen(), so
 * considering as content only up to the first null term character.
 * 
 * 下列函数调用之后，由于是使用strlen(); 那么 长度计算为第一个 
 * null-term character "\0"的长度
 * 
 * This function is useful when the sds string is hacked manually in some
 * way, like in the following example;
 * 这个函数对于手工修改 sds string 是非常有用的
 * 
 * s = sdsnew("foobar");
 * s[2] = "\0";
 * sdsupdatelen(s);
 * printf("%d\n",sdslen(s));
 * 
 * The output will be 2, but if we comment out the call to sdsupdatelen()
 * the output will be 6 as the string was modified but the logical length
 * remains 6 bytes.
 * 
 * 如果注释掉 sdsupdatelen(s) 尽管字符串已经被修改了，长度依然会输出 6
 * 
 * ?? 但是这样不就不能保证二进制安全了么 
 */
void sdsupdatelen(sds s)
{
    int reallen = strlen(s);
    sdssetlen(s, reallen);
}

void sdsclear(sds s){
    sdssetlen(s, 0);
    s[0] = '\0';
}