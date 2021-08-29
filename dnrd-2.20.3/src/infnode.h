/*

    File: infnode.c
    
    Copyright (C) 2004 by Natanael Copa <n@tanael.org>

    This source is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2, or (at your option)
    any later version.

    This source is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.

*/


#ifndef INFNODE_H
#define INFNODE_H

#include <sys/types.h>
#include "srvnode.h"

typedef struct _infnode {
  char            *inf;  /* the interface name */
  srvnode_t       *srvlist; /* linked list of servers */
  srvnode_t       *current;
  int             roundrobin; /* load balance the servers */
  int             retrydelay; /* delay before reactivating the servers */
  struct _infnode *next;    /* ptr to next server */
} infnode_t;

extern char group[100][40];
extern int group_cnt;

infnode_t *alloc_infnode(void);
infnode_t *search_infnode(infnode_t *head, const char *name);
int validate_interface(char *inf_name);
infnode_t *add_interface(infnode_t *list, char *name, const int maxlen);
srvnode_t *set_current(infnode_t *i, srvnode_t *s);
srvnode_t *next_active(infnode_t *i);
srvnode_t *deactivate_current(infnode_t *i);

infnode_t *ins_infnode (infnode_t *list, infnode_t *p);
infnode_t *del_infnode(infnode_t *list);
infnode_t *destroy_infnode(infnode_t *p);
infnode_t *empty_inflist(infnode_t *head);
infnode_t *destroy_inflist(infnode_t *head);
//infnode_t *search_subinfnode(infnode_t *head, const char *name, 
//			     const int maxlen);

void reactivate_srvlist(infnode_t *d);
void retry_srvlist(infnode_t *i, const int delay);


#endif




