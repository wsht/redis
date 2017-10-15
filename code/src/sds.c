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

/**
 * Modify an sds string in-place to make it empty(zero length).
 * However all the existing buffer is not discarded but set as free space
 * so that next append operations will not require allocations up to the
 * number of bytes previously available.
 * 
 * 消除sds 字符串，仅仅是将其长度置为零，其余保存的buffer 没有被收回，仅仅是作为空余空间
 * 以便于下次的append操作不在需要进行扩容
 */
void sdsclear(sds s){
    sdssetlen(s, 0);
    s[0] = '\0';
}

/**
 * Enlarge the free space at the end of the sds string so that
 * the caller is sure that after calling this function can 
 * overwrite up to addlen bytes after the end of the string,
 * plus one more byte for null term.
 * 
 * Note: this does not change the *length* of the sds string
 * as returned by stslen(), but only the free buffer space 
 * we have.
 */
sds sdsMakeRoomFor(sds s, size_t addlen){
    void *sh, *newsh;
    //获取当前sds字符串可用空间
    size_t avail = sdsavail(s);
    size_t len, newlen;
    char type, oldtype = s[-1] & SDS_TYPE_MASK;
    int hdrlen; //定义头部长度

    //如果扩容空间小于等于当前可用空间 则无需扩容
    if(avail >= addlen) return s;

    len = sdslen(s);
    sh = (char*)s - sdsHdrSize(oldtype);
    newlen = (len + addlen);
    //分配新的长度，如果新的长度小于预分配内存，那么则两倍分配
    //否则 newlen + 预分配内存空间
    if(newlen < SDS_MAX_PREALLOC)
        newlen *= 2;
    else
        newlen +=SDS_MAX_PREALLOC;

    type = sdsReqType(newlen);

    /**
     * Don't use type 5: the user is appending to the string 
     * and type 5 is not able to remember empty space, so
     * sdsMakeRoomFor() must be called at every appending
     * operation. 
     */
    if(type == SDS_TYPE_5) type=SDS_TYPE_8;

    //获得新的类型的头长度
    hdrlen = sdsHdrSize(type);
    if(oldtype == type){
        newsh = s_realloc(sh, hdrlen + newlen + 1);
        if(newsh == NULL) return NULL;
        s = (char*)newsh + hdrlen;
    }else
    {
        /**
         * Since the header size changes, need to move the
         * string forward, and can't use realloc
         */ 
        newsh = s_malloc(hdrlen + newlen + 1);
        if(newsh == NULL) return NULL;
        //将以前的buf内存中的值 copy到扩容后的地址里面去
        //由于获取的字符串长度len 是不包含结束符\0的 所以需要+1
        memcpy((char*)newsh+hdrlen, s, len+1);
        s_free(sh);
        //重定向
        s = (char*)newsh + hdrlen;
        s[-1] = type;
        sdssetlen(s,len);
    }

    sdssetalloc(s,newlen);
    return s;
}

/**
 * Reallocate the sds string so that it has no free space at
 * the end. The contained string remains not altered, but next
 * concatenation operations will require a reallocation.
 * 
 * After the call, the passed sds string is no longer valid
 * and all the references must be substituted with new pointer
 * returned by the call.
 */ 
sds sdsRemoveFreeSpace(sds s)
{
    void *sh, *newsh;
    char type, oldtype = s[-1] & SDS_TYPE_MASK;
    int hdrlen;
    size_t len = sdslen(s);
    sh = (char*)s-sdsHdrSize(oldtype);

    type = sdsReqType(len);
    hdrlen = sdsHdrSize(type);
    if(oldtype == type)
    {
        newsh = s_realloc(sh,hdrlen + len + 1);
        if(newsh == NULL) return NULL;
        s=(char*)newsh+hdrlen;
    }else{
        newsh = s_malloc(hdrlen + len + 1);
        if(newsh == NULL) return NULL;
        memcpy((char*)newsh +hdrlen, s, len + 1);
        s_free(sh);
        s=(char *)newsh + hdrlen;
        s[-1] = type;
        sdssetlen(s,len);
    }
    sdssetalloc(s,len);

    return s;
}

/**
 * Return the total size of the allocation of the specifed
 * sds string, including:
 * 1 The sds header before the pointer.
 * 2 The string.
 * 3 The free buffer at the end if any.
 * 4 The implicit null term
 */
size_t sdsAllocSize(sds s)
{
    size_t alloc = sdsalloc(s);

    return sdsHdrSize(s[-1]) + alloc + 1;
}

/**
 * Return the pointer of the actual SDS allocation                 
 * (normally SDS strings are referenced by the start
 * of the string buffer).
 */
void *sdsAllocPtr(sds s)
{
    return (void*) (s - sdsHdrSize(s[-1]));
}

/**
 * Increment the sds length and decrements the left free space at the
 * end of the string according to 'incr'. Also set the null term
 * in the new end of the string
 * 
 * 根据incr的值 增加sds字符串长度 或者减少剩余空间
 * 
 * Tis function is used in order to fix the string length after the
 * user calls sdsMakeRoomFor(), wtites something after the end of the
 * current string, and finally needs to set the new length.
 * 修正sds s字符串增加incr长度后字符串末尾NULL和sds相关数据结构的操作
 * 
 * Usage example:
 * Using sdsIncrLen() and sdsMakeRoomFor() it is possible to mount the
 * following schema, to cat bytes coming from the kernel to the end of an
 * sds string without copying into an intermediate buffer:
 * 
 * oldlen = sdslen(s);
 * s = sdsMakeRoomFor(s, BUFFER_SIZE);
 * nread = read(fd, s+oldlen, BUFFER_SIZE);
 * ... check for nerad <=0 and handle it...
 * sdsIncrlen(s, nread);
 */
void sdsIncrLen(sds s, int incr)
{
    unsigned char flags = s[-1];
    size_t len;
    switch(flag & SDS_TYPE_MASK)
    {
        case SDS_TYPE_5:{
            unsigned char *fp = ((unsigned char*)s) -1;
            unsigned char oldlen = SDS_TYPE_5_LEN(flags);
            assert( (incr>0 && oldlen+incr<32) || (incr<0 && oldlen >=(unsigned int)(-incr)) );
            *fp = SDS_TYPE_5 | ((oldlen + incr) << SDS_TYPE_BITS);
            len = oldlen + incr;
            break;
        }
        case SDS_TYPE_8:{
            SDS_HDR_VAR(8, s);
            //增加的要小于分配剩余空间，或者减少的要>字符串长度
            assert( (incr >= 0 && sh->alloc - sh->len >= incr) || (incr < 0 && sh->len >= (unsigned int)(-incr)) );
            len = (sh->len +=incr);
            break;
        }
        case SDS_TYPE_16: {
            SDS_HDR_VAR(16,s);
            assert((incr >= 0 && sh->alloc-sh->len >= incr) || (incr < 0 && sh->len >= (unsigned int)(-incr)));
            len = (sh->len += incr);
            break;
        }
        case SDS_TYPE_32: {
            SDS_HDR_VAR(32,s);
            assert((incr >= 0 && sh->alloc-sh->len >= (unsigned int)incr) || (incr < 0 && sh->len >= (unsigned int)(-incr)));
            len = (sh->len += incr);
            break;
        }
        case SDS_TYPE_64: {
            SDS_HDR_VAR(64,s);
            assert((incr >= 0 && sh->alloc-sh->len >= (uint64_t)incr) || (incr < 0 && sh->len >= (uint64_t)(-incr)));
            len = (sh->len += incr);
            break;
        }
        default: len=0;//just to avoid compilation warnings.
    }

    s[len] = '\0';
}

/**
 * Grow the sds to ahve the specified length. Bytes that were not part of
 * the original lngth of the sds will be set to zero.
 * 
 * if eht specified length is smaller than the current length, no operation
 * is performed.
 */ 
sds sdsgrowzero(sds s, size_t len){
    size_t curlen = sdslen(s);
    if(len <= curlen) return s;

    s = sdsMakeRoomFor(s, len-curlen);
    if(s==NULL) return NULL;

    /*Make sure added region doesn't contain garbage(无用的数据)*/
    memset(s+curlen,0,(len-curlen+1)); /*also set trailing \0 byte*/
    sdssetlen(s, len);

    return s;
}

sds sdscatlen(sds s,const void *t, size_t len)
{
    size_t curlen = sdslen(s);

    s = sdsMakeRoomFor(s, len);
    if(s==NULL) reutrn NULL;

    memcpy(s+curlen, t, len);
    sdssetlen(s, curlen+len);
    s[curlen+len] = '\0';
    
    return s;
}

sds sdscat(sds s, const chat *t){
    return sdscatlen(s, t, strlen(t));
}