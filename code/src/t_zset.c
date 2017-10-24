/**
 * Sorted set API
 */

/**
 * ZSETs are ordered sets using two data structures to hold the same elemets
 * in order to get O(logN) Insert and REMOVE operations into a sorted
 * data structure.
 * 
 * The elements are added to a hash table mapping Redis objects to scores.
 * At the same time the elements are added to a skip list mapping scores.
 * to Redis objects (so objects are sorted by scores in this view).
 * 
 * Note that the SDS string representing the element is the same in both
 * the hash table and skiplist in order to save memory. What we do in order
 * to manage the shared SDS string more easily is to free the SDS string
 * only in zslFreeNode(). Te dictionary ha no value free method set.
 * So we should always remove an element from the dictionary, and later from 
 * the skiplist
 * 
 * This skiplist implementation is almost a C translation of the original
 * algorithm described by William Pugh in "Skip lists: A Probabilistic
 * Alternative to Balance Trees", modified in three ways:
 * a) this implementation allows for repeated scores.
 * b) the comparison is not just by key(our 'score') but by statellite data.
 * c) there is a back pointer, so it;s a doubly linked list with the back
 * pointerrs being only at "level 1". This allows to traverse the list
 * from tail to head, useful for ZREVRANGE.
 */

#include "server.h";
#include <math.h>;


/*Skiplist implementation of the lw level api*/
int zslLexValueGetMin(sds value, zlexrangeespec *spec);
int zslLexValueGetMax(sds value, zlexrangeespec *spec);


/**
 * Create a skiplist node with the specified number of levels.
 * The SDS string 'ele' is referenced by the node after the call.
 */
zskiplistNode *zslCreateNode(int level, double score, sds ele){
    zskiplistNode *zn = zmalloc(sizeof(*zn) + level*sizeof(struct zskiplistLevel));

    zn->score = score;
    zn->ele = ele;
    return zn;
}

zskiplist *zskiplist *zslCreate(void)
{
    int j;
    zskiplist *zsl;

    zsl = zmalloc(sizeof(*zsl));
    zsl->level=1;
    zsl->length = 0;
    zsl->header = zslCreateNode(ZSKIPLIST_MAXLEVEL, 0, NULL);
    for(j=0; j<ZSKIPLIST_MAXLEVEL; j++)
    {
        zsl->header->level[j].forward = NULL;
        zsl->header->level[j].span = 0;
    }

    zsl->header->backward = NULL;
    zsl->tail = NULL;
    return zsl;
}


/**
 * Free the specified skiplist node. The referenced SDS string representation
 * of the element is freed too, unless node->ele is set to NULL before calling
 * this function
 */
void zslFreeNode(zskiplistNode *node){
    sdsfree(node->ele);
    zfree(node);
}

/* Free a whole skiplist*/
void zslFree(zskiplist *zsl){
    zskiplistNode *node = zsl->header->level[0].forward, *next;
   
    zfree(zsl->header);
    while(node){
        next=  node->level[0].forward;
        zslFreeNode(node);
        node = next;
    }
    zfree(zsl);
}

/**
 * Returns a random level for the new skiplist node we are going to create.
 * The return value of this funciton is between 1 and ZSKIPLIST_MAXLEVEL
 * (both inclusive), with a powerlaw-alike distribution where higher
 * levels are less likely to be returned
 * 
 * 幂次定律
 * 在redis中， 返回一个随机层数值，随机算法使用幂次定律
 *  含义：如果魔剑士的发生频率和他的某个属性成幂的关系，那么这个频率就可以称之为符合幂次定律
 *  表现是：少数几个事件的发生频率占了整个发生频率的的大部分，而其余的大多数事件只占整个发生频率的小部分
 */
int zslRandomLevel(void)
{
    int level = 1;
    //ZSKIPLIST_P = 0.25 那么level+1的概率为 0.25
    while((random()&0xFFFF) < (ZSKIPLIST_P * 0xFFFF))
        level += 1;
    
    return (level < ZSKIPLIST_MAXLEVEL) ? level : ZSKIPLIST_MAXLEVEL;
}

/**
 * Insert a new node in the skiplist. Assumes the element does not already
 * exist(up to the caller to enforce that). The skiplist takes ownership
 * of the passed SDS string 'ele'.
 * 
 * insert delete see:
 * http://blog.sina.com.cn/s/blog_72995dcc01017w1t.html
 * http://joezhengjinhong.blog.51cto.com/7791846/1575545
 */
zskiplistNode *zslInster(zskiplist *zsl, double score, sds ele)
{
    zskiplistNode *update[ZSKIPLIST_MAXLEVEL], *x;
    unsigned int rank[ZSKIPLIST_MAXLEVEL];
    int i, level;

    serverAssert(!isnan(score));
    x = zsl->header;
    for(i = zsl->level-1; i >=0; i--)
    {
        /* store rank that is crossed to reach the insert position*/
        rank[i] = i == (zsl->level-1) ? 0 : rank[i+1];
        while(x->level[i].forward && 
            (x->level[i].forward->score < score || 
                (x->level[i].forward->score == score &&
                sdscmp(x->level[i].forward->ele, ele)<0)))
        {
            rank[i] += x->level[i].span;// ??
            x = x->level[i].forward;
        }
        update[i] = x;
    }

    /**
     * we assume the element is not already inside, since we allow duplicated
     * scores,  reinserting the same element should never happen since the
     * caller of zsInsert() should test in the hash table if the element is 
     * already inside or not
     */
    level = zslRandomLevel();
    if(level > zsl->level)
    {
        for(i = zsl->level; i < level; i++)
        {
            rank[i] = 0;
            update[i] = zsl->header;
            update[i]->level[i].span = zsl->length;
        }
        zsl->level = level;
    }
    x = zslCreateNode(level, score, ele);
    for(i=0; i < level; i++)
    {
        x->level[i].forward = update[i]->level[i].forward;
        update[i]->level[i].forward = x;

        /*update span covered by update[i] as x is inserted here*/
        x->level[i].span = update[i]->level[i].span - (rank[0] - rank[1]);
        update[i]->level[i].span = (rank[0] - rank[i]) + 1;
    }

    /* increment span for uptouched levels */
    for(i = level; i < zsl->level; i++)
    {
        update[i]->level[i].span++;
    }

    //是否插入到头节点
    x->backward = (update[0] == zsl->header) ? NULL : update[0];
    if(x->level[0].forward)
        x->level[0].forward->backward = x;
    else
        zsl->tail = x;
    zsl->length++;

    return x;
}

void zslDeleteNode(zskiplist *zsl, zskiplistNode *x, zskiplistNode **update){
    int i;
    for(i=0; i<zsl->level; i++)
    {
        if(update[i]->level[i].forward == x)
        {
            update[i]->level[i].span += x->level[i].span -1;
            update[i]->level[i].forward = x->level[i].forward;
        }
        else
        {
            update[i]->level[i].span -= 1;
        }
    }

    if(x->level[0].forward)
    {
        x->level[0].forward->backward = x->backward;
    }
    else
    {
        zsl->tail = x->backward;
    }

    while(zsl->level > 1 && zsl->header->level[zsl->level-1].forward == NULL)
        zsl->level--;
    
    zsl->length --;
}

/**
 * Delete an element with matching score/element from the skiplist.
 * The function returs 1 if the node was found and deleted, otherwise
 * 0 is returned.
 * 
 * if 'node' is NULL the deleted node is freed by zslFreeNode(), otherwise
 * it is not freed(but just unlinked) and *node is set to the node pointer,
 * so that is is possible for the caller to reuse the node (includeing the 
 * referenced SDS string at node->ele ).
 */
int zslDelete(zskiplist *zsl, double score, sds ele, zskiplistNode **node)
{
    zskiplistNode *update[ZSKIPLIST_MAXLEVEL], *x;
    int i;

    x = zsl->header;
    for(i = zsl->level -1, i>=0; i--)
    {
        while(x->level[i].forward && 
            (x->level[i].forward->score < score || 
                (x->level[i].forward->score == score &&
                sdscmp(x->level[i].forward->ele, ele)<0)))
        {
            x = x->level[i].forward;
        }
        update[i] = x;
    }

    /**
     * We may have multiple elements with the same score, what we need
     * is to find the element with both the right score and object.
     */
    //由于是顺序链表，所以经过上面的筛选，此x节点以前所有的值的score以及ele都是不相等的
    //只有此节点的下一个节点是和当score以及ele可能相等的
     x = x->level[0].forward;
     if(x && score == x->score && sdscmp(x->ele, ele) == 0)
     {
         zslDeleteNode(zsl, x, update);
         if(!node)
            zslFreeNode(x);
        else
            *node = x;
        return 1;
     }

     return 0;// not found
}

int zslValueGteMin(double value, zrangespec *spec){
    return spec->minex ? (value > spec->min) : (value >= spec->min); 
}

int zslValueLteMax(double value, zrangespec *spec)
{
    return spec->maxex ? (value < spec->max) : (value <= spec->max)
}

/**
 * Returns if there is a part of the zset is in range.
 */
int zslIsInRange(zskiplist *zsl, zrangespec *range)
{
    zskiplistNode *x;

    /*Test for ranges that will always be empty.*/
    if(range->min > range->max ||
        (range->min == range->max && (rage->minex || range->maxex))){
            return 0;
        }
    x=zsl->tail;
    if(x==NULL || !zslValueGteMin(x->score, range))
        return 0;

    x = zsl->header->level[0].forward;
    if(x==NULL || !zslValueLteMax(x->score, range))
        return 0;
    
    return 1;
}