/**
 * 整数集合（intset）是集合键底层实现之一。集合键另一实现是值为空的散列表（hash table），
 * 虽然使用散列表对集合的加入删除元素，判断元素是否存在等等操作时间复杂度为O(1)，
 * 但是当存储的元素是整型且元素数目较少时，如果使用散列表存储，就会比较浪费内存，
 * 因此整数集合（intset）类型因为节约内存就存在
 */

#ifndef __INTSET_H
#define __INTSET_H
#include <stdint.h>

typedef struct intset{
    uint32_t encoding;
    uint32_t length;
    int8_t content[]; //保存元素的数组 ，元素类型不一定是int8_t的类型，
                      //柔型数组不占用intset结构体大小，并且数组元素从小到大排列
} intset;

