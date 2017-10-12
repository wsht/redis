#ifndef __ADLIST_H__
#define __ADLIST_H__


/**
 * redis 中链表的引用非常广泛，例如列表键的底层实现之一就是链表。而且redis中的链表结构被实现为双向链表，因此，在头部和尾部进行的操作非常的快
 * LPUSH
 * RPUSH
 * LRANGE
 */

/* Node, list, and Iteraotr are the only data structures used currently. */

// 每个连表节点由listNode来表示
//listNode结构通过prev和next指针就组成了双向链表
//使用双向链表，prev以及next指针能保证获取某个节点的前驱节点和后继节点的复杂度为O(1)
typedef struct listNode {
	struct listNode *prev;//前驱节点，如果是list的头节点，则prev指向NULL
	struct listNode *next;//后继节点，如果是list尾部节点，则next指向NULL
	void *value; //万能指针，能够存放任何信息
}listNode;

//redis 还提供了一个表头，用于存放上面的双向链表的信息
//head 指针指向 listNode的头节点
//tail 指针指向 listNode的尾节点

//head指针和tail指针：对于链表的头结点和为节点操作的复杂度为O(1);
//len链表长度计数器：获取链表中节点数量的复杂度为O(1)
//dup, free, match 指针：实现多态，链表节点listNode使用万能指针void *保存节点的值，而表头list使用dup,free,match指针来针对
//	链表中存放的不同对象从而实现不同的方法
typedef struct list{
	listNode *head;  //链表头节点指针
	listNode *tail;  //链表尾节点指针

	//下面三个函数指针就像对象的成员函数一样
	void *(*dup)(void *ptr); //复制链表节点保存的值
	void (*free)(void *ptr);//释放链表节点保存的值
	int (*match)(void *ptr, void *key); //比较链表节点所保存的节点值和另外一个输入值是否相等
	unsigned long len;  //链表长度计数器
} list;

//链表迭代器
// 优点
// 1.提供一种方法顺序的访问一个聚合对象中的各个元素，而又不需要暴露该对象的内部实现
// 2.将指针操作进行封装，代码可读性增强
typedef struct listIter{
	listNode *next; //迭代器指向当前节点
	int direacion; //迭代方向，可以取意向两个值:AL_START_HEAD和 AL_START_TAIL
}listIter;

#define AL_START_HEAD 0;//正向迭代，从表头向表尾进行迭代
#define AL_START_TAIL 1;//反向迭代，从表尾向表头进行迭代


//针对list结构和listNode结构的赋值和查询操作使用宏进行封装,其操作复杂度为O(1)
#define listLength(l) ((l)->len) //返回链表l 节点的数量
#define listFirst(l) ((l)->head) //返回链表l 的头结点地址
#define listLast(l) ((l)->tail) //返回链表l 的尾部节点地址
#define listPrevNode(n) ((n)->prev) // 返回节点n 的前驱节点地址
#define listNextNode(n) ((n)->next) // 返回节点n 的后继节点地址
#define listNodeValue(n) ((n)->value) //返回节点n 的节点值

#define listSetDupMethod(l,m) ((l)->dup = (m)) //设置链表l的 赋值函数为m方法
#define listSetFreeMethod(l,m) ((l)->free = (m))//设置链表l的 释放函数为m方法
#define listSetMatchMethod(l,m) ((l)->match = (m))//这只链表l的 比较函数为m方法

#define listGetDupMethod(l) ((l)->dup) //返回链表l的 赋值函数
#define listGetFreeMethod(l) ((l)->free) //返回链表l的 释放函数
#define listGetMatchMethod(l) ((l)->match)//返回链表l的 比较函数

//链表操作的函数原型（prototypes）
list *listCreate(void);
void listRelease(list *list);//释放list的表头和链表
void listEmpty(list *list); //??
list *listAddNodeHead(list *list, void *value);
list *listAddNodeTail(list *list, void *value);
list *listInsertNode(list *list, listNode *old_node, void *value, int after);//在list中，根据after在old_node节点前后插入值为value的节点
void listDelNode(list *list, listNode *node);

listIter *listGetIterator(list *list, int direction); //为list创建一个迭代器 iterator
listNode *listNext(listIter *iter); //迭代返回list节点
void listReleaseIterator(listIter *iter); //释放iter迭代器
void listRewind(list *list, listIter *li); //将迭代器li重置为list的头节点，并且设置为正向迭代
void listRewindTail(list *list, listIter *li);//将迭代器li充值未list的尾节点，并且设置为反向迭代

list *listDup(list *orig); //拷贝表头为orig的链表并返回
listNode *listSearchKey(list *list, void *key); //在list中查找value为key的节点并返回
listNode *listIndex(list *list, long index); //返回小标为index的节点地址

void listRotate(list *list); //将为节点插到头结点
void listJoin(list *l, list *o);
#endif /* __ADLIST_H__*/