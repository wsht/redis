#include <stdlib.h>
#include "adlist.h"
#include "zmalloc.h"

/**
 * 
 */

/**
 * create a new list. the created list can be freed with
 * AlfreeList(), but private value of every node need to be freed
 * by the user before to ccall AlFreeList().
 *
 * on error , NULL is returned. otherwise the pointer to the new list.
 */
list *listCreate()
{
    struct list *list;
    //分配内存空间
    if((list=zmalloc(sizeof(*list))) == NULL)
    {
        return NULL;
    }

    //初始化表头
    list->head = list->tail = NULL;
    list->len = 0;
    list->dup = NULL;
    list->free = NULL;
    list->match = NULL;

    return list;
}

/* Remove all the elements from the list without destroying the list itself. */
void listEmpty(list *list)
{
    unsigned long len;
    listNode *current, *next;

    current = list->head;
    len = list->len;

    while(len--)//遍历链表
    {
        next = current->next;
        // 如果设置了释放函数，那么则使用该函数进行释放
        if(list->free) list->free(current->value);
        zfree(current);
        current = next;
    }

    list->head = list->tail = NULL;
    list->len = 0;
}

/**
 * Free the whole list
 * 
 * This function can't fail
 */
void listRelease(list *list)
{
    listEmpty(list);
    zfree(list);
}

list *listAddNodeHead(list *list, void * value)
{
    listNode *node;
    if((list=zmalloc(sizeof(*node))) == NULL)
    {
        return NULL;
    }

    node->value = value;
    if(list->len == 0) //插入空的链表
    {
        list->head = list->tail = node;
        node->prev = node->next = NULL;
    }
    else
    {
        node->prev = NULL;
        node->next = list->head;

        list->head->prev = node;
        list->head = node;
    }
    
    list->len ++;

    return list;
}

list *listAddNodeTail(list *list, void *value)
{
    listNode *node;
    if((list=zmalloc(sizeof(*node))) == NULL)
    {
        return NULL;
    }

    node->value = value;
    if(list->len == 0)
    {
        list->head = list->tail = node;
        node->prev = node->next = NULL;
    }
    else
    {
        node->next = NULL;
        node->prev = list->tail;
        
        list->tail->next = node;
        list->tail = node;
    }

    list->len ++;

    return list;
}

list *listInsertNode(list *list, listNode *old_node, void *value, int after)
{
    listNode *node;
    if((list=zmalloc(sizeof(*node))) == NULL)
    {
        return NULL;
    }

    node->value = value;
    
    if(after)
    {
        //向后插入
        node->prev = old_node;
        node->next = old_node->next;
        if(list->tail == old_node)
        {
            list->tail = node;
        }
    }
    else
    {
        //向前插入
        node->next = old_node;
        node->prev = old_node->prev;
        if(list->head == old_node)
        {
            list->head = node;
        }
    }
    
    //释放old_node的前节点的next指针，使其指向当前node
    if(node->prev != NULL)
    {
        node->prev->next = node;
    }

    //释放old_node的后节点的prev指针，使其指向当前node
    if(node->next != NULL)
    {
        node->next->prev = node;
    }
    
    list->len ++;

    return list;
}

void listDelNode(list *list, listNode *node)
{

    if(node->prev)
    {
        node->prev->next = node->next;
    }
    else
    {
        //对于头结点，直接将列表head指针指向节点的下一个点
        list->head = node->next;
    }

    if(node->next)
    {
        node->next->prev = node->prev;
    }
    else
    {
        list->tail = node->prev;
    }

    if(list->free) list->free(node->value);

    zfree(node);

    list->len--;
}

listIter *listGetIterator(list *list, int direction)
{
    listIter *iter;
    if((iter=zmalloc(sizeof(*iter)) == NULL)) return NULL;

    if(direacion == AL_START_HEAD)
        iter->next = list->head;
    else
        iter->next = list->tail;
    
    iter->direacion = direacion;

    return iter;
}

void listReleaseIterator(listIter *iter)
{
    zfree(iter);
}

void listRewind(list *list, listIter *li)
{
    li->next = list->head;
    li->direacion = AL_START_HEAD;
}

void listRewindTail(list *list, listIter *li)
{
    li->next = list->tail;
    li->direacion = AL_START_TAIL;
}

listNode *listNext(listIter *iter)
{
    listNode *current = iter->next;

    if(current != NULL)
    {
        if(iter->direacion==AL_START_HEAD)//根据迭代方向更新迭代指针
        {
            iter->next = current->next;
        }
        else
        {
            iter->next = current->prev;
        }
    }

    return current;
}

/**
 * Duplicate the whole list . On out of memeory NULL is returned.
 * On success a copy of the original list is returned.
 * 
 * The 'Dup' method set with listSetDupMethod() function is used 
 * to copy the node value. Otherwise the same pointer value of
 * the original node is used as value of the copied node.
 * 
 * The original list both on success or error is never modified
 * 
 * */
list *listDup(list *orig)
{
    list * copy;
    listIter iter;
    listNode * node;

    if((copy=listCreate()) ==NULL)
    {
        return NULL;
    }

    copy->dup = orig->dup;
    copy->free = orig->free;
    copy->match = orig->match;
    
    //正向迭代 并且赋值
    listRewind(orig, &iter);

    while((node=listNext(&iter)) != NULL)
    {
        void *value;
        
        if(copy->dup)
        {
            value = copy->dup(node->value);
            if(value == NULL)
            {
                listRelease(copy);
                return NULL;
            }
        }
        else
        {
            value = node->value;
        }

        if(listAddNodeTail(cppy, value) == NULL)
        {
            listRelease(copy);
            return NULL;
        }
    }

    return copy;
}

/**
 * Search the list for a node matchig a given key.
 * The match is performed using the 'match' method
 * set with listSetMatchMethod. if no 'match' method
 * is set, the 'value' pointer of every node is direcly
 * compare with the key pointer
 * 
 * on success thre first matching node pointer is returned
 * (search starts from head). If no matching node exists
 * NULL is returned 
 **/
listNode *listSearchKey(list *list, void *key)
{
    listIter iter;
    listNode *node;
    listRewind(list, &iter);

    while((node = listNext(&iter)) != NULL)
    {
        if(list->match(node->value, key))
        {
            return node;
        }
        else
        {
            if(key == node->value)
            {
                return node;
            }
        }
    }

    return NULL;
}

/**
 * Return the element at the specified zero-base index 
 * where 0 is the head, 1 is the element next to head
 * and so on. Negative integers are used in order to count
 * from the tail, -1 is the last element, -2 the penultimate
 * and so on. if the index is out of range 
 * NULL is returned.
 */
listNode *listIndex(list *list, long index)
{
    listNode *node;
    if(index < 0)
    {
        index = (-index)-1
        node = list->tail;
        while(index-- && node) n=n->prev;
    }
    else
    {
        node = list->head;
        while(index-- && node) n=n->next;
    }

    return node
}

/* Rotate the list removing the tail node and inserting it to the head*/
void listRotate(list *list)
{
    listNode *tail = list->tail;
    if(listLength(list) <=1) return;

    /*Detach current tail*/
    list->tail = tail->prev;
    list->tail->next = NULL;

    /* Move it as head*/
    list->head->prev = tail;
    tail->next = list->head;
    tail->prev = NULL;
    list->head = tail;
}

/**
 * Add all the elements of the list 'o' at the end of the 
 * list 'l'.
 * The list 'other' remains empty but otherwise valid 
 */
void listJoin(list *l, list *o)
{
    //如果o list有节点，那么将o list 的头节点的prev指针，指向l list 的尾节点
    if(o->head)
    {
        o->head->prev = l->tail;
    }

    //如果 l list 有节点 则将 l list的尾节点的next指针，指向 olist 的头结点
    if(l->tail)
    {
        l->tail->next = o->head;
    }
    //l list 不存在节点时候
    else
    {
        l->head = o->head;
    }

    //最终合并
    l->tail = o->tail;
    l->len += o->len;

    o->len = 0;
}