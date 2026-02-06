#ifndef __DLIST_H_
#define __DLIST_H_

#define container_of(ptr, type, member) ({          \
        const typeof( ((type *)0)->member ) *__mptr = (const typeof( ((type *)0)->member ) *)(ptr); \
        (type *)( (char *)__mptr - offsetof(type,member) );})

struct DLIST_NODE {
	struct DLIST_NODE *prev;
	struct DLIST_NODE *next;
	int type;
};

/*
 * 双向链表添加节点
 * param: 
 * 	head: 链表头
 * 	node: 要插入的节点
 *
 * */
struct DLIST_NODE* dlist_node_append(struct DLIST_NODE* head, struct DLIST_NODE *node);

/* 双向链表初始化链表头 */
#define DLIST_INIT(name) {name.prev=NULL; name.next=NULL;}

static inline void INIT_LIST_HEAD(struct DLIST_NODE* name) 
{
	name->prev = NULL;
	name->next = NULL;
}

/* 双向链表，遍历整个链表 */
#define DLIST_FOREACH(head, node, node_type, list) \
	for (struct DLIST_NODE* lnode=(head)->next; lnode && (node=container_of(lnode, node_type, list)); lnode=lnode->next)

#define DLIST_FREE(head, node_type, list) do { \
	struct DLIST_NODE* next_lnode = NULL;\
	struct DLIST_NODE* curr_lnode = head->next;\
	for (;curr_lnode && ((next_lnode=curr_lnode->next) || free(curr_lnode)); curr_lnode=next_lnode){free(curr_lnode)} \
} while(0)
#endif
