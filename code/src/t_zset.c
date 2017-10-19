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
            rank[i] += x->level[i].span;
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
}