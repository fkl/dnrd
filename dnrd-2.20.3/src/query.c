/*
 * query.c
 *
 * This is a complete rewrite of Brad garcias query.c
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

#include <syslog.h>
#include <assert.h>
#include <unistd.h>
#include <time.h>
#include <string.h>
#include <errno.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <fcntl.h>

#include "lib.h"
#include "common.h"
#include "query.h"
#include "qid.h"


query_t qlist; /* the active query list */
static query_t *qlist_tail;
unsigned long total_queries=0;
unsigned long total_timeouts=0;

int upstream_sockets = 0; /* number of upstream sockets */

static int dropping = 0; /* dropping new packets */

/* init the query list */
void query_init() {
  qlist_tail = (qlist.next = &qlist);
}

/* Returns 1 if port excluded and zero otherwise. 
 * The port number passed is already in big endian,
 * so appropriate conversion is required before comparison
 * with the exclusion ports configured from cli
 */
int is_port_excluded(int port)
{
	int c=0;
	for(; c<exc_port_ofst; c++)
		if(port == htons(exc_port[c]))
			return 1;
	return 0;
}

/* create a new query, and open a socket to the server */
query_t *query_create(infnode_t *i, srvnode_t *s) {
  query_t *q;
#ifdef RANDOM_SRC
  struct sockaddr_in my_addr;
#endif

  /* should never be called with no server */
  assert(s != NULL);

  /* check if we have reached maximum of sockets */
  if (upstream_sockets >= max_sockets) {
    if (!dropping)
      log_msg(LOG_WARNING, "Socket limit reached. Dropping new queries");
    return NULL;
  }

  dropping=0;
  /* allocate */
  if ((q=(query_t *) allocate(sizeof(query_t))) == NULL)
    return NULL;

  /* return an emtpy circular list */
  q->next = (struct _query *)q;

  /* Set flag if we are creating a dummy query or or a real one */
  if (!i)
    q->is_dummy = 1;
  else
    q->is_dummy = 0;
  
  q->srv = s;

  /* set the default time to live value */
  q->ttl = forward_timeout;
  
  /* open all of 3 new sockets */
  // TODO: Currently we are creating 3 sockets and random ports irrespective of if we
  // have at least 3 servers or not to forward queries to and hence use these 3 sockets.
  // This could be fixed latter.
  int c;
  for(c=0; c<3; ++c)
  {
  	if ((q->sock_arr[c] = socket(AF_INET, SOCK_DGRAM, 0)) < 0)
        {
    		log_msg(LOG_ERR, "query_create: Couldn't open socket");
    	        free(q);
                return NULL;
  	} 
        else upstream_sockets++;
  
  // UPDATE: COMMENTING OUT SOCKET REUSE FOR NOW BECAUSE OF HAVING 3 SEPARATE SOCKETS
  /* Change socket option so that a different socket can be bound to the same local port and used for sending
   * outgoing requests. This is needed since we use a different socket for listening from the one from sending.
   * The sending socket is bound to different interfaces one after the other since it sends dns queries to multiple
   * servers. If we use the same socket, we risk losing packets for instance we bound to eth0, but a response arrived
   * on eth2.
   */
  //int opt = 1;
  //setsockopt(q->sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    int opt = 1;

    /* IP_PKTINFO is set to be able to read using recvmsg, the interface name on which a packet is received latter. */ 
    // include struct in_pktinfo in the message "ancilliary" control data
    setsockopt(q->sock_arr[c], IPPROTO_IP, IP_PKTINFO, &opt, sizeof(opt));

  /* bind to random source port */
#ifdef RANDOM_SRC
  	memset(&my_addr, 0, sizeof(my_addr));
  	my_addr.sin_family = AF_INET;
  	my_addr.sin_addr.s_addr = INADDR_ANY;

        do
        {
  		my_addr.sin_port = htons( myrand(65536-1026)+1025 );
	}
	while(is_port_excluded(my_addr.sin_port));
	
  	if (bind(q->sock_arr[c], (struct sockaddr *)&my_addr, 
	   sizeof(struct sockaddr)) == -1) {
    	   log_msg(LOG_WARNING, "bind: while creating query %s", strerror(errno));
  	}
#endif

  	/* Make the socket non-blocking */
  	fcntl(q->sock_arr[c], F_SETFL, O_NONBLOCK);

  	/* add the socket to the master FD set */
  	FD_SET(q->sock_arr[c], &fdmaster);

  	if (q->sock_arr[c] > maxsock)
		 maxsock = q->sock_arr[c];

	if(q->is_dummy == 1) /* Allocate only a single socket for dummy queries */
		break;
  }

  /* get an unused QID */
  q->my_qid = qid_get();
  return q;
}

query_t *query_destroy(query_t *q) {
  /* close the socket and return mem */
  qid_return(q->my_qid);

  /* unset the sockets */
  int i;
  for(i=0; i<3; ++i)
  {
  	FD_CLR(q->sock_arr[i], &fdmaster);
  	close(q->sock_arr[i]);
  	upstream_sockets--;

	if(q->is_dummy == 1) /* dummy queries only have a single socket */
		break;
  }

  total_queries++;
  
  if(q->fail_msg_len > 0)//q->cached_fail_msg)
  {
    free(q->cached_fail_msg);
  }
  
  free(q);
  return NULL;
}

/* Get a new query */
query_t *query_get_new(infnode_t *inf, srvnode_t *srv) {
  query_t *q;
  assert(srv != NULL);
  /* if there are no prepared queries waiting for us, lets create one */
  if ((q=srv->newquery) == NULL) {
    if ((q=query_create(inf, srv)) == NULL) return NULL;
  }
  srv->newquery = NULL;
  //q->domain=dom;
  q->srv = srv;
  return q;
}


/* get qid, rewrite and add to list. Retruns the query before the added  */
// TODO: Modified parame domnode_t
query_t *query_add(infnode_t *inf, srvnode_t *srv, 
		   const struct sockaddr_in* client, char* msg, 
		   unsigned len) {

  query_t *q, *p, *oldtail;
  unsigned short client_qid = *((unsigned short *)msg);
  time_t now = time(NULL);

  /* 
     look if the query are in the list 
     if it is, don't add it again. 
  */
  for (p=&qlist; p->next != &qlist; p = p->next) {
    if (p->next->client_qid == client_qid) {
      /* we found the qid in the list */
      *((unsigned short *)msg) = p->next->my_qid;
      p->next->client_time = now;
      log_debug(2, "Query %i from client already in list. Count=%i", 
		client_qid, p->next->client_count++);

  //printf("Query qid %d and client qid %d\n", q->my_qid, q->client_qid);

      return p;
    }
  }

  if ((q=query_get_new(inf, srv))==NULL) {
    /* if we could not allocate any new query, return with NULL */
    return NULL;
  }

  q->client_qid = client_qid;
  memcpy(&(q->client), client, sizeof(struct sockaddr_in));
  q->client_time = now;
  q->client_count = 1;

  /* set new qid from random generator */
  *((unsigned short *)msg) = htons(q->my_qid);

  //printf("Query qid %d and client qid %d\n", q->my_qid, q->client_qid);

  /* add the query to the list */
  q->next = qlist_tail->next;
  qlist_tail->next = q;

  /* new query is new tail */
  oldtail = qlist_tail;
  qlist_tail = q;
  return oldtail;

}

/* remove query after */
query_t *query_delete_next(query_t *q) {
  query_t *tmp = q->next;

  /* unlink tmp */
  q->next = q->next->next;
 
  /* if this was the last query in the list, we need to update the tail */
  if (qlist_tail == tmp) {
    qlist_tail = q;
  }

  /* destroy query */
  query_destroy(tmp);
  return q;
}


/* remove old unanswered queries */
void query_timeout(time_t age) {
  int count=0;
  time_t now = time(NULL);
  query_t *q;
  
  /* NOTE: q->next is the current query */
  for (q=&qlist; q->next != &qlist; q = q->next) {
    if (q->next->client_time < (now - q->next->ttl)) {
      count++;
      
      query_t *curr_q = q->next;
      log_debug(3, "curr_q->resp_sent %d msg len %d", curr_q->resp_sent, curr_q->fail_msg_len);
       
      if(curr_q->fail_msg_len > 0 && curr_q->resp_sent == 0)
      {
        /* set the client qid */
        *((unsigned short *)curr_q->cached_fail_msg) = curr_q->client_qid;
        log_debug(3, "Forwarding the failed reply to host %s since no successfull response received", inet_ntoa(curr_q->client.sin_addr));
      
        if (sendto(isock, curr_q->cached_fail_msg, curr_q->fail_msg_len, 0, (const struct sockaddr *)&curr_q->client,
          sizeof(struct sockaddr_in)) != curr_q->fail_msg_len) {
	        log_debug(1, "sendto error %s", strerror(errno));
	    }
	  }
	     
      query_delete_next(q);
    }
  }
  if (count) log_debug(1, "query_timeout: removed %d entries", count);
  total_timeouts += count;
}

int query_count(void) {
  int count=0;
  query_t *q;
  
  for (q=&qlist; q->next != &qlist; q = q->next) {
    count++;
  }
  return count;
}



void query_dump_list(void) {
  query_t *p;
  for (p=&qlist; p->next != &qlist; p=p->next) {
    log_debug(2, "srv=%s, myqid=%i, client_qid=%i", 
	      inet_ntoa(p->next->srv->addr.sin_addr), p->next->my_qid, 
	      p->next->client_qid);
  }
}
