/*
 * query.h
 *
 * This is a complete rewrite of Brad garcias query.h
 *
 * This file contains the data definitions, function definitions, and
 * variables used to implement our DNS query list.
 *
 * Assumptions: No multithreading.
 *
 * Copyright (C) Natanael Copa <ncopa@users.sourceforge.net>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */

#ifndef QUERY_H
#define QUERY_H

#include <sys/socket.h>
#include "srvnode.h"
#include "infnode.h"

typedef struct _query {
  int sock_arr[3]; /* the communication socket array - one for each of the three simultaneously sent queries */
  srvnode_t *srv; /* the upstream server */
  int is_dummy; /* To differentiate between actual queries from clients or health check dummy queries */
  
  unsigned short my_qid; /* the local qid */
  unsigned short client_qid; /* the qid from the client */
  struct sockaddr_in client; /* */

  /* Count of how many servers this query currently has been sent to. This is used to keep track
   * of when to delete the query i.e. we delete when this count is zero. When we query multiple servers
   * simultaneously, their responses are received only on the listening socket. So it is important
   * not to free that lisetning socket and hence query until other servers also respond or time out.
   */
  int serv_sent_cnt;
  
  /* This is needed when we received a single failure response, waited for others to respond. But they timeout.
   * Eventually we should be sending the first failure back to client. 
   */
  char *cached_fail_msg;
  int  fail_msg_len;
  
  /* Flag keeps track of whether we have responsed to client or not */
  int resp_sent;

  /*  int send_count; * number of retries */
  /*  time_t send_time; * time of last sent packet */
  time_t client_time; /* last time we got this query from client */
  int client_count; /* number of times we got this same request */

  time_t ttl; /* time to live for this query */

  srvnode_t *srv_list[3]; /* array of pointers to point to servers we send requests */

  struct _query     *next; /* ptr to next query */

} query_t;

extern query_t qlist;
extern unsigned long total_queries;
extern unsigned long total_timeouts;


void query_init(void);
query_t *query_create(infnode_t *i, srvnode_t *s);
//query_t *query_create(domnode_t *d, srvnode_t *s);
query_t *query_destroy(query_t *q);
query_t *query_get_new(infnode_t *inf, srvnode_t *srv);
//query_t *query_get_new(domnode_t *dom, srvnode_t *srv);
query_t *query_add(infnode_t *inf, srvnode_t *srv, const struct sockaddr_in* client, char* msg, 
		   unsigned len);
//query_t *query_add(domnode_t *dom, srvnode_t *srv, const struct sockaddr_in* client, char* msg, 
//		   unsigned len);
query_t *query_delete_next(query_t *q);
void query_timeout(time_t age);
void query_stats(time_t interval);


#endif
