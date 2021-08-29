/*
 * udp.c - handle upd connections
 *
 * Copyright (C) 1999 Brad M. Garcia <garsh@home.com>
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

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif
#include <errno.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <net/if.h>
#include <arpa/inet.h>
#include <syslog.h>
#include <string.h>
#include <time.h>
#include <assert.h>
#include <stdio.h>
#include <sys/param.h>
#include "common.h"
#include "relay.h"
#include "cache.h"
#include "query.h"
#include "check.h"
#include "dns.h"

#ifndef EXCLUDE_MASTER
#include "master.h"
#endif

/* TEMP matched special host array of interfaces */
char matched_intf[5][10];
int matched_intf_cnt;


/*
 * dnssend()						22OCT99wzk
 *
 * Abstract: A small wrapper for send()/sendto().  If an error occurs a
 *           message is written to syslog.
 *
 * Returns:  The return code from sendto().
 */
static int udp_send(int sock, srvnode_t *srv, void *msg, int len)
{
    int	rc;
    time_t now = time(NULL);
    rc = sendto(sock, msg, len, 0,
		(const struct sockaddr *) &srv->addr,
		sizeof(struct sockaddr_in));

    if (rc != len) {
	log_msg(LOG_ERR, "sendto error: %s: ",
		inet_ntoa(srv->addr.sin_addr), strerror(errno));
	return (rc);
    }
    if ((srv->send_time == 0)) srv->send_time = now;
    srv->send_count++;
    
    log_msg(LOG_NOTICE, "Request forwarded to DNS server %s", inet_ntoa(srv->addr.sin_addr));
    
    return (rc);
}

int bind_sock2inf(int sock, char *inf_name)
{
    int status = -1;
    struct ifreq ifr;

    memset(&ifr, 0, sizeof(ifr));
    snprintf(ifr.ifr_name, sizeof(ifr.ifr_name), inf_name);
    if ( (status = setsockopt(sock, SOL_SOCKET, SO_BINDTODEVICE,
                (void *)&ifr, sizeof(ifr))) < 0) {
	    log_debug(4, "Failed binding to interface named %s", inf_name);
    }
    
    return status;
}

/* returns 1 if passed interface name is added as a default one, 0 otherwise */
int is_curr_inf_default(char *i_name)
{
        int i;
	for(i=0; i<def_inf_count; i++)
	{
		if (strncmp(i_name,def_inf_list[i],9) == 0)
			return 1;
	}
	
	return 0;
}

int send2current(query_t *q, void *msg, const int len) {
    /* If we have interface associated with our servers, send it to the
       appropriate server as determined by srvr */
  infnode_t *i;
  assert(q != NULL);


  // extract current query host
  // look for host in host_array
  // if found, add to local matched interface array.
  // This array is looked up latter while sending through the interface

  if(sp_hosts_count > 0)
  {
	char query_host[30];
	snprintf_cname(msg, len, 12, query_host, sizeof(query_host));
        log_debug(3, "Query for host %s", query_host); 
	matched_intf_cnt = 0;

        int c;
	for(c=0; c<sp_hosts_count; c++)
	{
		if (strncmp(sp_hosts[c].host_name, query_host, strlen(query_host)) == 0)
		{
			strncpy(matched_intf[matched_intf_cnt], sp_hosts[c].inf_name, 9);
			matched_intf_cnt++;
		}
	}
  }

  // The actual interface node starts from the second
  i = inf_list->next;

  int c=0; // socket index

  while(i != inf_list && c<3)
  {

    /* If we have matched interfaces for the current query host specified with -H then only forward
     * queries through those interfaces.
     */
	  if(matched_intf_cnt > 0)
	  {
		  int a;
		  int is_special = 0;
		  for(a=0; a<matched_intf_cnt; a++)
			  if(strncmp(i->inf, matched_intf[a], 9) == 0)
				  is_special = 1;

		  if(is_special == 0) /* This is not one of the specified interface */
		  {
			  i = i->next;
			  continue;
		  }

	  }


	  /* If default interfaces have been specified then only send through current interface if it is included in default list */
	  else if(def_inf_count > 0)
		  if (!is_curr_inf_default(i->inf))
		  {
			  i = i->next;
			  continue;
		  }

	  log_debug(3, "Binding to interface %s", i->inf);
	  bind_sock2inf(q->sock_arr[c],i->inf);

	  /* Try sending if current server is not null. Break as soon as current message is successfully sent. */
	  while ((i->current != NULL) && (udp_send(q->sock_arr[c], i->current, msg, len) != len)) {
	  if (reactivate_interval)
		  deactivate_current(i);
	  }

    /* Store pointer to server to which we sent request */
    if(i->current != NULL)
    {
    	q->srv_list[c] = i->current;
    	//printf("Server to which we sent %s\n", inet_ntoa(q->srv_list[c]->addr.sin_addr));
    }

    /* Keep track of how many servers this query has been sent to. This will be used to decide when to delete the query. */
    q->serv_sent_cnt++;
    
    i = i->next;

    if(q->is_dummy == 1) /* Send activation request to only one server for a dummy query */
	break;

    c++;
  }
  
  if (i->current != NULL) {
    return len;
  } else return 0;
  
}

/*
 * This function handles udp DNS requests by either replying to them (if we
 * know the correct reply via master, caching, etc.), or forwarding them to
 * an appropriate DNS server.
 */
query_t *udp_handle_request()
{
    unsigned           addr_len;
    int                len;
    const int          maxsize = UDP_MAXSIZE;
    static char        msg[UDP_MAXSIZE+4];
    struct sockaddr_in from_addr;
    int                fwd;
    infnode_t          *inf_ptr;
    query_t *q, *prev;

    /* Read in the message */
    addr_len = sizeof(struct sockaddr_in);
    len = recvfrom(isock, msg, maxsize, 0,
		   (struct sockaddr *)&from_addr, &addr_len);
    if (len < 0) {
	log_debug(1, "recvfrom error %s", strerror(errno));
	return NULL;
    }

    /* do some basic checking */
    if (check_query(msg, len) < 0) return NULL;

    /* Determine how query should be handled */
    if ((fwd = handle_query(&from_addr, msg, &len, &inf_ptr)) < 0)
      return NULL; /* if its bogus, just ignore it */

    /* If we already know the answer, send it and we're done */
    if (fwd == 0) {
	    if (sendto(isock, msg, len, 0, (const struct sockaddr *)&from_addr,
		   addr_len) != len) {
	        log_debug(1, "sendto error %s", strerror(errno));
	    }
	    
        return NULL;
    }

    /* rewrite msg, get id and add to list*/
    if ((prev=query_add(inf_ptr, inf_ptr->current, &from_addr, msg, len)) == NULL){
       /* of some reason we could not get any new queries. we have to drop this packet */
        return NULL;
    }
    q = prev->next;
    
    if (send2current(q, msg, len) > 0) {
        //log_debug(1, "Successfully sent query");

      /* add to query list etc etc */
      return q;
    } else {

      /* we couldn't send the query */
#ifndef EXCLUDE_MASTER
      int	packetlen;
      char	packet[maxsize+4];

      /*
       * If we couldn't send the packet to our DNS servers,
       * perhaps the `network is unreachable', we tell the
       * client that we are unable to process his request
       * now.  This will show a `No address (etc.) records
       * available for host' in nslookup.  With this the
       * client won't wait hang around till he gets his
       * timeout.
       * For this feature dnrd has to run on the gateway
       * machine.
       */
      
      if ((packetlen = master_dontknow(msg, len, packet)) > 0) {
	query_delete_next(prev);
	return NULL;
	if (sendto(isock, msg, len, 0, (const struct sockaddr *)&from_addr,
		   addr_len) != len) {
	  log_debug(1, "sendto error %s", strerror(errno));
	  return NULL;
	}
      }
#endif
    }
    return q;
}

int get_interface_name(struct msghdr *mh, char *inf_name)
{
  int status = -1;
  struct cmsghdr *cmsg;

  for ( // iterate through all the control headers
    cmsg = CMSG_FIRSTHDR(mh);
    cmsg != NULL;
    cmsg = CMSG_NXTHDR(mh, cmsg))
  {
    // ignore the control headers that don't match what we want
    if (cmsg->cmsg_level != IPPROTO_IP ||
        cmsg->cmsg_type != IP_PKTINFO)
    {
	printf("not valid level type\n");
        continue;
    }

    struct in_pktinfo *pi = (struct in_pktinfo *)CMSG_DATA(cmsg);
    // at this point, peeraddr is the source sockaddr
    // pi->ipi_spec_dst is the destination in_addr
    // pi->ipi_addr is the receiving interface in_addr

    if (if_indextoname(pi->ipi_ifindex, inf_name) != NULL)
    {
	    log_debug(4, "Rcvd interface index %d and name %s", pi->ipi_ifindex, inf_name);
	    status = 0;
    }
    else
	    log_debug(4, "Error: Failed reading interface index %d", pi->ipi_ifindex);
  }

  return status;
}

srvnode_t * search_server(struct sockaddr_in *resp_ip, char * inf_name)
{
    /* Loop through to find all server ip's */
        
    infnode_t *i = inf_list->next;
    do
    {
        /* Only match if the correct interface was found too. This is needed because
         * same ip is configured on multiple interfaces and we keep setting the first one only
         * as active.
         */ 

	//printf("Current looking inf %s inf that received packet %s\n", i->inf, inf_name);

        if (strcmp(i->inf, inf_name) == 0)
        {
	  srvnode_t *s = i->srvlist->next;
          do
          {
            unsigned int ip_value = *((int *) &(s->addr.sin_addr));
            unsigned int resp_ip_val = *((int *) &(resp_ip->sin_addr));
            
            if (ip_value == resp_ip_val) // if it matches
            {
                return s;
            }
            
            s = s->next;
          }while (s != i->srvlist);
        }
        i = i->next;
    } while (i != inf_list);
    
    log_debug(4, "Failed locating interface and ip to reset");
    return NULL;

}

/*
 * dnsrecv()							22OCT99wzk
 *
 * Abstract: A small wrapper for recv()/recvfrom() with output of an
 *           error message if needed.
 *
 * Returns:  A positove number indicating of the bytes received, -1 on a
 *           recvfrom error and 0 if the received message is too large.
 */
static int reply_recv(query_t *q, int socket_indx, void *msg, int len)
{
    int	rc;
    struct sockaddr_in from;
    //struct sockaddr_in peeraddr;

    // the control data is dumped here
    char cmbuf[0x100];
    
   // if you want access to the data you need to init the msg_iovec fields
    //char buffer[10]; // BUFFER IS COMMENTED BECAUSE MSG AND LEN IS USED
    struct msghdr mh;

    memset(&mh, 0, sizeof(mh));

    struct iovec iov[1];
    iov[0].iov_base = msg;
    iov[0].iov_len = len;

    mh.msg_name = &from;//&peeraddr;
    mh.msg_namelen = sizeof(from);
    mh.msg_control = cmbuf;
    mh.msg_controllen = sizeof(cmbuf);
    mh.msg_flags = 0;
    mh.msg_iov = iov;
    mh.msg_iovlen = 1;

    rc = recvmsg(q->sock_arr[socket_indx], &mh, 0);

    /* recvfrom is replaced with recvmsg to be able to read interface of arriving packet too. */  
    //rc = recvfrom(q->sock_arr[socket_indx], msg, len, 0,
	//	  (struct sockaddr *) &from, &fromlen);

    if (rc == -1) {
	log_msg(LOG_ERR, "recvfrom error: %s",
		inet_ntoa(q->srv->addr.sin_addr));
	return (-1);
    }
    else if (rc > len) {
	log_msg(LOG_NOTICE, "packet too large: %s",
		inet_ntoa(q->srv->addr.sin_addr));
	return (0);
    }
    
    //from = peeraddr;

    // Here we used to check if the same server we requested has responded and log warning if not.
    // However, with the new multiple possible servers, we only print the server from which response is received
    
    // Commenting out the check of testing if the response came from the server currently pointed to by the query
    //else if (memcmp(&from.sin_addr, &q->srv->addr.sin_addr,
    //		    sizeof(from.sin_addr)) != 0) {
    log_msg(LOG_NOTICE, "Response came from server : %s",
		inet_ntoa(from.sin_addr));
    //log_msg(LOG_WARNING, "unexpected server: %s",
    //	inet_ntoa(from.sin_addr));
    //return (0);
    //}
    //

    char inf_name[30];
    if (get_interface_name(&mh, inf_name) != -1)
    {
      srvnode_t * srv_rsp = search_server(&from, inf_name);
    
      if (srv_rsp != NULL)
      {
        srv_rsp->send_time = 0;
        
        if (srv_rsp->inactive)
            log_debug(1, "Reactivating server %s", inet_ntoa(srv_rsp->addr.sin_addr));
        srv_rsp->inactive = 0;
      }
    }
    
    return (rc);
}

/*
 * handle_udpreply()
 *
 * This function handles udp DNS requests by either replying to them (if we
 * know the correct reply via master, caching, etc.), or forwarding them to
 * an appropriate DNS server.
 *
 * Note that the mached query is prev->next and not prev.
 */
void udp_handle_reply(query_t *prev, int sock_indx)
{
  //    const int          maxsize = 512; /* According to RFC 1035 */
    static char        msg[UDP_MAXSIZE+4];
    int                len;
    unsigned           addr_len;
    query_t *q = prev->next;
    
    log_debug(3, "handling socket %i", q->sock_arr[sock_indx]);
    if ((len = reply_recv(q, sock_indx, msg, UDP_MAXSIZE)) < 0)
    {
	    log_debug(1, "dnsrecv failed: %i", len);
	    
        if(q->serv_sent_cnt == 1)	    
	        query_delete_next(prev);
	    else    
    	    q->serv_sent_cnt--;
	    return; /* recv error */
    }

    /* do basic checking */
    if (check_reply(q->srv, msg, len) < 0) {
      log_debug(1, "check_reply failed");

      if(q->serv_sent_cnt == 1)	    
          query_delete_next(prev);
      else    
          q->serv_sent_cnt--;
      
      return;
    }

    if (opt_debug) {
	  char buf[256];
	  snprintf_cname(msg, len, 12, buf, sizeof(buf));
	
	  if(q->is_dummy == 0)
	  {
		log_debug(3, "Received DNS reply for \"%s\"", buf);
      	  }
    }
    
    dump_dnspacket("reply", msg, len);
    addr_len = sizeof(struct sockaddr_in);

    /* was this a dummy reactivate query? If no, have we already sent a response */
    if (q->is_dummy == 0 && q->resp_sent == 0) {
    
      int rcode = check_replycode(msg,len);    
      log_debug(3, "Received reply code is %d (non zero value indicates unsuccessfull response)", rcode);
      
      if(rcode == 0 || q->serv_sent_cnt == 1) // If it is a successful response or there are no others queries to be waited for
      {
          /* no, lets cache the reply and send it to client */
          cache_dnspacket(msg, len, q->srv);
          
          /* set the client qid */
          *((unsigned short *)msg) = q->client_qid;
          log_debug(3, "Forwarding the reply to the host %s",
		    inet_ntoa(q->client.sin_addr));
          if (sendto(isock, msg, len, 0,
		    (const struct sockaddr *)&q->client,
		    addr_len) != len) {
	        log_debug(1, "sendto error %s", strerror(errno));
          }
          
          q->resp_sent = 1; /* set query flag that we have forwarded a successful response to client */
      }
       
      else {
         
         if (q->fail_msg_len == 0)//if(q->cached_fail_msg == NULL)
         {
             log_debug(2, "It is not a successful response and we wait for responses from other servers while caching current one");

             q->cached_fail_msg = (char *)allocate(len);
         
            if(q->cached_fail_msg != NULL)
            {
                q->fail_msg_len = len;
                memcpy(q->cached_fail_msg, msg, len);
                log_debug (5, "MSG length is %d\n", q->fail_msg_len);
            
                /*int i;
                for (i=0; i<len; i++)
                    printf("%x",q->cached_fail_msg[i]);
                printf("\n");*/
            }
            else
             	log_debug(1, "Failed allocating memory for failure response");
         }
         
         else
            log_debug(1, "Failure message already cached, ignoring current one");
      }
    }
       
    else {
      log_debug(2, "Either we got a reactivation dummy reply OR we have already responded to this query (and will only keep the server alive). Cool!");
    }
    

    /* HERE IS THE PROBLEM OF TIMING OUT
     * WHEN WE SENT REQUEST TO ONE SERVER ONLY, IT WORKED FINE
     * and query->srv was the current server to which request is sent, So we have
     * to track which server responded and shouldnt be timed out.
     * This has now been fixed by searching for the server from which reply
     * is received and resetting that server's timeout value in side reply_recv function.
     */
       
    /* this server is obviously alive, we reset the counters */
#if 0
    q->srv->send_time = 0;
    if (q->srv->inactive) log_debug(1, "Reactivating server %s",
				 inet_ntoa(q->srv->addr.sin_addr));
    q->srv->inactive = 0;
#endif     
    
    
    /* Remove query from list and destroy it 
     * IF no other server requests are pending for this one.
     * Otherwise, just decrement the counter.
     */
    if(q->serv_sent_cnt == 1 /*|| q->resp_sent == 1 */)
    {
        log_debug(5, "deleting query after handling reply successfully");
        
	int a=0;
	for(;a<3;a++)
	{	if(q->srv_list[a] != NULL)
		{
			srvnode_t * srv = q->srv_list[a];
    			//printf("Server BEFORE QUERY DELETE %s\n", inet_ntoa(srv->addr.sin_addr));
			srv->inactive = 0;
			srv->send_time = 0;
		} 
	}
        query_delete_next(prev);
    }
        
    else
    {
        /* Code reaches here only when there are multiple servers to which query is sent and we received
         * response from first. We reset is_dummy flag (if we have already sent a sucessful response),
         * so that upon receiving further responses, we only reset server timeouts and DON'T sent query
         * response multiple times. 
         */
        q->serv_sent_cnt--;
        
        //if(q->resp_sent == 1) /* Only reset dummy flag if we have already sent a response */
        //    q->is_dummy = 1;        
    }
}


/* send a dummy packet to a deactivated server to check if its back*/
int udp_send_dummy(srvnode_t *s) {
  static unsigned char dnsbuf[] = {
  /* HEADER */
    /* we send a lookup for localhost */
    /* will this work on a big endian system? */
    0x00, 0x00, /* ID */
    0x01, 0x00, /* QR|OC|AA|TC|RD -  RA|Z|RCODE  */
    0x00, 0x01, /* QDCOUNT */
    0x00, 0x00, /* ANCOUNT */
    0x00, 0x00, /* NSCOUNT */
    0x00, 0x00, /* ARCOUNT */

    9, 'l','o','c','a','l','h','o','s','t',0,  /* QNAME */
    0x00,0x01,   /* QTYPE A record */
    0x00,0x01   /* QCLASS: IN */

    /* in case you want to lookup root servers instead, use this: */
    /*    0x00,       */ /* QNAME:  empty */
    /*    0x00, 0x02, */ /* QTYPE:  a authorative name server */
    /*    0x00, 0x01  */ /* QCLASS: IN */
  };
  query_t *q;
  struct sockaddr_in srcaddr;

  /* should not happen */
  assert(s != NULL);

  if ((q=query_add(NULL, s, &srcaddr, dnsbuf, sizeof(dnsbuf))) != NULL) {
    int rc;
    q = q->next; /* query add returned the query 1 before in list */
    /* don't let those queries live too long */
    q->ttl = reactivate_interval;
    memset(&srcaddr, 0, sizeof(srcaddr));
    log_debug(2, "Sending dummy id=%i to %s", ((unsigned short *)dnsbuf)[0], 
	      inet_ntoa(s->addr.sin_addr));
    /*  return dnssend(s, &dnsbuf, sizeof(dnsbuf)); */

    // For a dummy query only 0th index socket is valid
    rc=udp_send(q->sock_arr[0], s, dnsbuf, sizeof(dnsbuf));
    ((unsigned short *)dnsbuf)[0]++;
    return rc;
  }
  return -1;
}
