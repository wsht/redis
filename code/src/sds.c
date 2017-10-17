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

/**
 * Append the specified binary-safe string pointerd by 't' of 'len' bytes to the
 * end of the specified ss string 's'.
 * 
 * After the call, the passed sds string is no longer valid and all the
 * references must be substitued with the new pointer returned by the call.
 */
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

/**
 * Append the specified null termianted C string to the sds string 's'.
 * 
 * After the call, the passed sds string is no longer valid and all the
 * references must be substitued with the new pointer returned by the call.
 */
sds sdscat(sds s, const chat *t){
    return sdscatlen(s, t, strlen(t));
}

/**
 * Append the specified sds 't' to the existing sds 's'
 * 
 * After the call, the passed sds string is no longer valid and all the
 * references must be substitued with the new pointer returned by the call.
 */
sds sdscatsds(sds s, const sds t)
{
    return sdscatlen(s, t, strlen(t));
}

/**
 * Destructively modify the sds string 's' to hold the specified binary
 * safe string pointed by 't' of length 'len' bytes.
 */
sds sdscpylen(sds s, const chat *t, size_t len)
{
    if(sdsalloc(s) < len){
        s = sdsMakeRoomFor(s, len - sdslen(s));
        if(s == NULL) return NULL;
    }

    memcpy(s,t,len);
    s[len] = '\0';
    sdssetlen(s, len);

    return s;
}

/**
 * Like sdscpylen(), but 't' must be a null-termined string so that the length
 * of the string is obtained with strlen(). 
 */
sds sdscpy(sds s, const char *t)
{
    return sdscpylen(s, t, strlen(s));
}

/**
 * Helper for sdscatlonglong() doing the actual number -> string
 * conversion. 's' must point to a string with room for at last
 * SDS_LLSTR_SIZE bytes.
 * 
 * The function returns the length of the null-terminated string
 * representation stored at 's'.
 * 
 */
#define SDS_LLSTR_SIZE 21
int sdsll2str(char *s, long long value)
{
    char *p, aux;
    unsigned long long v;
    size_t l;

    //Generate the string representation, this method produces
    // an reversed string.
    /**
     * 例如 value = 10086 则 buf保存的数据为 |6|8|0|0|1|\0|
     */
    v = (value < 0) ? -value : value;
    p=s;
    do{
        *p++ = '0' + (v%10);
        v /= 10;
    }while(v);

    if(value < 0) *p++ = '-';

    l = p - s;
    *p = '\0';

    /*Reverse the string.*/
    /*将 |6|8|0|0|1|\0| 转换为 |1|0|0|8|6|\0|*/
    p--;
    while(s<p)
    {
        aux = *s;
        *s = *p;
        *p = aux;
        s++;
        p--;
    }

    return l;
}

/**
 * Identical sdsll2str(), but for unsigned long long type.
 */
int sdsull2str(char *s, unsigned long long value)
{
    char *p, aux;
    site_t l;

    p=s;
    do{
        *p++ = '0'+(value%10);
        v /= 10;
    }while(value);

    l = p-s;
    *p = '\0';

    p--;

    while(s < p){
        aux = *s;
        *s = *p;
        *p = aux;
        s++;
        p--;
    }
    
    return l;
}

/**
 * Create an sds string from a long long value. It is must faster than:
 * sdscatprintf(sdsempty(),"%lld\n", value);
 */
sds sdsfromlonglong(long long value)
{
    char buf[SDS_LLSTR_SIZE];
    int len = sdsll2str(value);

    return sdsnewlen(buf, len);
}

sds sdscatvprintf(sds s, const char *fmt, va_list ap)
{
    va_list cpy;
    char staticbuf[1024], *buf = staticbuf, *t;
    size_t buflen = strlen(fmt)*2;

    /**
     *  We try to start using a static buffer for speed.
     * if not possible we revert to heap allocation
     * 
     * 尝试以staticbuf 作为char 来进行加速，如果行不通，则
     * 重新分配空间 
     */
    if(buflen > sizeof(staticbuf))
    {
        buf = s_malloc(buflen);
        if(buf == NULL) return NULL;
    }
    else
    {
        buflen = sizeof(staticbuf);
    }

    /**
     * Try with buffers two times bigger every time we fail to
     * fit the string in the current buffer size.
     */
    while(1){
        buf[buflen-2] = '\0';
        va_copy(cpy, ap);
        vsnprintf(buf, buflen, fmt, cpy);
        va_end(cpy);
        if(buf[buflen-2] != '\0')
        {
            if(buf != staticbuf) s_free(buf);
            buflen *=2;
            buf = s_malloc(buflen);
            if(buf != NULL) return NULL;
            
            continue;
        }
        break;
    }

    t = sdscat(s,buf);
    if(buf != staticbuf) s_free(buf);

    return t;
}

/**
 * Append to the sds string 's' a string obtained using prinf-alike format
 * specifier
 * 
 * After the call, the passed sds string is no longer valid and all the
 * references must be substitued with the new pointer returned by the call.
 * 
 * Example 
 * s = sdsnew("Sum is :");
 * s = sdscatprintf(s,"%d+%d=%d",a,b,a+b).
 * 
 * Often you need to create a string from scratch with the printf-alike
 * format. When this is the need, just use sdsempty() as the target string:
 * 
 * s = sdscatprintf(sdsempty(), "...you format ...", args);
 */
sds sdscatprintf(sds s, const char *fmt, ...){
    va_list ap;
    char *t;
    t = sdscatvprintf(s,fmt, ap);
    va_end(ap);
    return t;
}

/**
 * This function is similar to sdscatprintf, but much faster as it does
 * not rely on sprintf() family functions implementd(实现) by the libc that
 * are often very slow. Moveover directly handling the sds string as 
 * new data is concatenated provides a performance improvement.
 * 
 * However this function only handles an incompatible subset of printf-alike
 * format specifiers:
 * %s - C string
 * %S - SDS string
 * %i - signed int
 * %I - 64 bit signed integer(long long, int64_t)
 * %u - unsigned int
 * %U - 64 bit unsigned integer(long long, uint64_t)
 * %% - Verbatim "%"cahracter.
 */
sds sdscatfmt(sds s, char const *fmt, ...)
{
    size_t initlen = sdslen(s);
    const char *f = fmt;
    int i;
    va_list ap;

    va_start(ap, fmt);
    f = fmt;    /* Next format specifier byte to process.*/
    i = initlen;/* Position of the next bute to write to dest str.*/

    while(*f)
    {
        char next, *str;
        size_t l;
        long long num;
        unsigned long long unum;

        /*Make sure there is always space for at least 1 char*/
        if(sdsavail(s)==0)
        {
            s = sdsMakeRoomFor(s,1);
        }

        switch(*f){
            case '%':
                next = *(f+1);
                f++;
                switch(next){
                    case's':
                    case'S':
                        str = va_arg(ap, char*);
                        l = (next == 's') ? strlen(str) : sdslen(str);
                        if(sdsavail(s) < l){
                            s = sdsMakeRoomFor(s,l);
                        }
                        memcpy(s+i, str,l);
                        sdsinclen(s,l);
                        i +=l;
                        break;
                    case'i':
                    case'I':
                        if(next == 'i'){
                            num = va_arg(ap, int);
                        }
                        else
                        {
                            num = va_arg(ap, long long);
                        }

                        {
                            char buf[SDS_LLSTR_SIZE];
                            l = sdsll2str(buf,num);
                            if(sdsavail(s) < l)
                            {
                                s = sdsMakeRoomFor(s,l);
                            }
                            memcpy(s+i, buf,l);
                            sdsinclen(s,l);
                            i += l;
                        }
                        break;
                    case 'u':
                    case 'U':
                        if(next == 'u')
                        {
                            num = va_arg(ap, unsigned int);
                        }
                        else
                        {
                            num = va_arg(ap, unsigned long long);
                        }

                        {
                             char buf[SDS_LLSTR_SIZE];
                            l = sdsll2str(buf,num);
                            if(sdsavail(s) < l)
                            {
                                s = sdsMakeRoomFor(s,l);
                            }
                            memcpy(s+i, buf,l);
                            sdsinclen(s,l);
                            i += l;
                        }
                        break;
                    default: /*handle %% and generally %<unknow>.*/
                        s[i++] = next;
                        sdsinclen(s,1);
                        break;
                }
                break;
            default:
                s[i++] = *f;
                sdsinclen(s,1);
                break;    
        }
        f++
    }

    va_end(ap);
    
    /*Add null-term*/
    s[i] = '\0';

    return s;
}

/**
 * Remove the part of the string from left and from right composed just of
 * contiguous character found in 'cset', this is a null terminted C string.
 * After the call, the passed sds string is no longer valid and all the
 * references must be substitued with the new pointer returned by the call.
 * 
 * Example:
 * 
 * s = sdsnew("AA...AA.a.aa.a.HelloWorld      :::");
 * s = sdstrim(s,"Aa. :");
 * printf("%s\n", s);
 * 
 * output willbe just "HelloWorld"
 */
sds sdstrim(sds s, const char *cset)
{
    char *start, *end, *sp, *cp;
    size_t len;

    sp = start = s;
    ep = end = s + sdslen(s)-1;
    //从头开始遍历 s的字符串，如果s中的字符串出现在cset中，那么指针右移
    while(sp <= end && strchr(cset, *sp)) sp++;
    //从尾遍历字符串, 如果字符出现在cset中，那么尾指针左移
    while(ep > sp && strchr(cset, *ep)) ep--;

    //获取截取后字符串长度
    len = (sp > ep) ? 0 : ((ep - sp) + 1);
    //如果头部左侧被截取，那么将sp为起始点长度为len的内存中的值，赋给s指针地址为起始点内存的值 
    if(s != sp) memmove(s, sp, len);
    s[len] = '\0';
    sdssetlen(s,len);
    return s;
}

/**
 * Turn the string into a smaller(or equal) string containing only the 
 * substring specified by the 'start' and 'end' indexes.
 * 
 * start and end can be negative (负数的), where -1 means the last character of
 * the string , -2  the penultimate character, and so forth.
 * 
 * The itnerval is inclusive, so the start and end characters will be part
 * of the resulting string.
 * 
 * The string is modified in-place
 * 
 * Example:
 * 
 * s = sdsnew("hello word");
 * sdsrange(s,1,-1); => ello world
 */
void sdsrange(sds s, int start, int end)
{
    size_t newlen, len = sdslen(s);

    if(len == 0)return;
    if(start < 0)
    {
        start = len + start;
        //超出范围以后，算作起始位置
        if(start < 0) start = 0;
    }

    if(end < 0)
    {
        end = len + end;
        if(end < 0) end = 0;
    }

    newlen = (start > end) ? 0 : (end - start) + 1;

    if(newlen != 0)
    {
        //start 越界 截取长度为0
        if(start >= (signed) len)
        {
            newlen = 0;
        }
        //end越界，算作最后一个字符
        else if(end >=(signed) len)
        {
            end = len - 1;
            newlen = (start > end) ? 0 : (end - start) + 1;
        }
    }
    else
    {
        start = 0;
    }

    if(start && newlen) memmove(s, s+start, newlen);

    // why there is set 0 not a '\0';
    s[newlen] = 0;
    // s[newlen] = '\0';
    sdssetlen(s,newlen);
}

/* Apply tolower() to every character of the sds string 's'. */
void sdstolower(sds s)
{
    int len = sdslen(s), j;
    for(j=0;j<len;j++) s[j] = tolower(s[j]);
}

void sdstoupper(sds s)
{
    int len = sdslen(s), j;
    for(j=0;j<len;j++) s[j] = toupper(s[j]);
}

/**
 * Compare tow sds string s1 and s2 with memcmp
 * 
 * Return value:
 *      
 *      positive  if s1 > s2.
 *      negative  if s1 < s2.
 *      0 if s1 and s2 are exactly the same binary string.
 * 
 * If two strings share exactly the same prefix, but one of the two has
 * additional characters, th longer string is considered to the greater than
 * the smaller one
 */
int sdscmp(const sds s1, const sds s2){
    size_t l1, l2, minlen;
    int cmp;

    l1 = sdslen(s1);
    l2 = sdslen(s2);
    minlen = (l1 < l2) ? li : l2;
    cmp = memcmp(s1,s2,minlen);
    if(cmp == 0) return l1-l2;
    return cmp;
}

/**
 * Split 's' with separator in 'sep'. An array
 * of sds string is returned. *count will be set
 * by reference to the number of tokens returned.
 * 
 * On out of memory, zero length string, zero length
 * separator, NULL is returned.
 * 
 * Note that 'sep' is able to split a string using
 * a multi-character separator. For example
 * sdssplit("foo_-_bar","_-_"); will return two element 'foo' and bar.
 * 
 * This version of the function is binary-safe but 
 * requires length arguments. sdssplit() is just the 
 * same function but for zero-termianted strrings.
 * 
 */
sds *sdssplitlen(const char *s, int len, const char *sep, int seplen, int *count){
    int elements = 0, slots = 5, start = 0, j;
    sds *tokens;

    if(seplen < 1 || len < 0) return NULL;

    // 预先分配出5个数组的空间
    tokens = s_malloc(sizeof(sds) * slots);
    if(tokens == NULL) return NULL;

    if(len == 0)
    {
        *count = 0;
        return tokens;
    }

    for(j=0; j<(len-(seplen-1)); j++)
    {
        /*make sure there is room for the next element and the final one*/
        if(slots < elements + 2){
            sds *newtokens;
            
            //再次分配五个空间
            slots *= 2;
            newtokens = s_realloc(tokens, sizeof(sds)*slots);
            if(newtokens == NULL) goto cleanup;
            tokens = newtokens; 
        }

        /*serarch the separator*/
        if((seplen == 1 && *(s+j) == sep[0]) || (memcmp(s+j,sep,setlen) == 0))
        {
            tokens[elements] = sdsnewlen(s+start, j-start);
            if(tokens[elements] == NULL) goto cleanup;
            elements ++;
            start = j + seplen;
            //setlen - 1 可以使指针指向 separator的最后一个字符，然后j++
            //以后，跳过separator.
            j = j + setlen -1; /*skip the separator*/
        }
    }

    /*Add the final element. We are sure there is room in the tokens array. */
    tokens[elements] = sdsnewlen(s+start, len-start);
    if(tokens[elements] == NULL) goto cleanup;
    elements++;
    *count = elements;

    return tokens;

cleanup:
    {
        int i;
        for(i=0;i<elements;i++) sdsfree(tokens[i]);
        s_free(tokens);
        *count = 0;
        return NULL;
    }
}

/**
 * Free the result returned by sdssplitlen(), or do nothing if 'tokens' is NULL
 */
void sdsfreesplitres(sds *tokens, int count){
    if(!tokens) return;
    while(count--)
    {
        sdsfree(tokens[count]);
    }
    s_free(tokens);
};

//TODO test 