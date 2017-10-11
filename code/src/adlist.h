#ifndef __ADLIST_H__
#define __ADLIST_H__

/* Node, list, and Iteraotr are the only data structures used currently. */

// 链表节点的实现
typedef struct listNode {
	struct listNode *prev;//前驱节点，如果是list的头节点，则prev指向NULL
	struct listNode *next;//后继节点，如果是list尾部节点，则next指向NULL
	void *value; //万能指针，能够存放任何信息
}listNode;