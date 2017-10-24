#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "intset.h"
#include "zmalloc.h"
#include "endianconv.h"


/**
 * Note that these encodings are ordered,
 * so INSER_ENC_INT16 < INTSET_ENC_INT32 < INTSET_ENC_INT64
 */
#define INTSET_ENC_INT16 (sizeof(int16_t))
#define INTSET_ENC_INT32 (sizeof(int32_t))
#define INTSET_ENC_INT64 (sizeof(int64_t))


static uint8_t _intsetValueEncoding(int64_t v)
{
    if(v < INT32_MIN || v > INT32_MAX)
        return INTSET_ENC_INT64;
    else if(v < INT16_MIN || V > INT16_MAX)
        return INTSET_ENC_INT32;
    else
        return INTSET_ENC_INT16;
}

/**
 * Return the value at pos, given an encoding.
 */
static int64_t _intsetGetEncoded(intset *is, int pos, uint8_t enc)
{
    int64_t v64;
    int32_t v32;
    int16_t v16;

    if(enc == INTSET_ENC_INT64)
    {
        memcpy(&v64, ((int64_t*)is->content) + pos, sizeof(v64));
        memrev64ifbe(&v64);
        return v64;
    }
    else if(enc == INTSET_ENC_INT32)
    {
        memcpy(&v32, ((int32_t*)is->content) + pos, sizeof(v32));
        memrev32ifbe(&v32);
        return v32;
    }
    else
    {
        memcpy(&v16, ((int16_t*)is->content) + pos, sizeof(v16));
        memrev16ifbe(&v16);
        
        return v16;
    }
}


/**
 * Return the value at pos, using the configured encoding.
 */
static int64_t _intsetGet(intset *is, int pos)
{
    return _intsetGetEncoded(is, pos, intrev32ifbe(is->encoding));
}

/**
 * Set the value at pos, using the configured encoding.
 */
static void _intsetSet(intset *is, int pos, int64_t value) {
    uint32_t encoding = intrev32ifbe(is->encoding);

    if (encoding == INTSET_ENC_INT64) {
        ((int64_t*)is->contents)[pos] = value;
        memrev64ifbe(((int64_t*)is->contents)+pos);
    } else if (encoding == INTSET_ENC_INT32) {
        ((int32_t*)is->contents)[pos] = value;
        memrev32ifbe(((int32_t*)is->contents)+pos);
    } else {
        ((int16_t*)is->contents)[pos] = value;
        memrev16ifbe(((int16_t*)is->contents)+pos);
    }
}

/**
 * Create an empty intset.
 */
intset *intsetNew(void)
{
    intset *is = zmalloc(sizeof(intset));
    is->encoding = intrev32ifbe(INTSET_ENC_INT16);
    is->length = 0;

    return is;
}

/**
 * Redize the intset
 */
static intset *intsetResize(intset *is, uint32_t len)
{
    uint32_t size = len * intrev32ifbe(is->encoding);
    is = zrealloc(is, sizeof(intset) + size);

    return is;
}

/**
 * 升级的特点
 * 1.提高灵活性:通过自动审计底层数组来适应不同类型的新元素
 * 2.节约内存：整数集合既可以让集合保存三种不同类型的值，又可以确保升级操作只在有需要的时候进行，这样就节省了内存 
 * 3.不支持降级
 */
static intset *intsetUpgradeAndAdd(intset *is, int64_t value)
{
    uint8_t curenc = intrev32ifbe(is->encoding);
    uint8_t newenc = _intsetValueEncoding(value);
    int length = intrev32ifbe(is->length);
    int prepend = value < 0 ? 1 : 0; //如果value<0,则要将value添加到数组最前端

    /* First set new encoding and resize */
    is->encoding = intrev32ifbe(newenc);
    //根据新的编码方式进行空间分配
    is = intsetResize(is, intrev32ifbe(is->length)+1);

    /**
     * Upgrade back-to-front so we don't overwrite values.
     * Note that the "prepend" variable is used to make sure we have an empty
     * space at either the beginning or the end of the intset.
     */
    while(length --)
        _intsetSet(is, length+prepend, _intsetGetEncoded(is, length, curenc));
    
    /*Set the value at the beginning or the end*/
    if(prepend)
    {
        _intsetSet(is, 0, value);
    }
    else
    {
        _intsetSet(is, intrev32ifbe(is->length), value);
    }

    is->length = intrev32ifbe(intrev32ifbe(is->length) + 1);

    return is;
}