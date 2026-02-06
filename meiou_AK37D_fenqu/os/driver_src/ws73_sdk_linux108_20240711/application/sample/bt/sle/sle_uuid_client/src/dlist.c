/*
 * Copyright (c) @CompanyNameMagicTag. 2023. All rights reserved.
 * Description: Adding a node to a doubly linked list.
 */
#include <stdio.h>
#include "dlist.h"

struct DLIST_NODE* dlist_node_append(struct DLIST_NODE* head, struct DLIST_NODE *node)
{
	struct DLIST_NODE *tmp_node;
	if ((!head) || (!node)) {
		printf("Err: Null pointer(head=%p,node=%p)", head, node);
		return NULL;	
	}
	for (tmp_node = head; tmp_node && tmp_node->next; tmp_node=tmp_node->next);
	tmp_node->next = node;
	return head;
}