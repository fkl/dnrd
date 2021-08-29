/*

    File: infnode.c
    
    Copyright (C) 2010 by fayyazlodhi <fayyazkl@gmail.com>

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

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <time.h>
#include <assert.h>
#include <sys/ioctl.h>
#include <linux/if.h>


#include "lib.h"
#include "common.h"
#include "udp.h"
#include "infnode.h"

/* Allocate an interface node */
infnode_t *alloc_infnode(void) {
  infnode_t *p = allocate(sizeof(infnode_t));
  p->inf=NULL;
  p->srvlist=alloc_srvnode();
  /* actually we return a new emty list... */
  return p->next=p;
}

/* search for an interface. returns the node if found or NULL if not */
infnode_t *search_infnode(infnode_t *head, const char *name) {
  infnode_t *i=head;
  assert((head != NULL) && (name != NULL));
  /* the list head is pointing to the default interface */
  //  if ((name == NULL) || (d == NULL)) return head;
  while ((i=i->next) != head) {
    if (strcmp(i->inf, name) == 0) return i;
  }
  return NULL;
}

/* validatese if an interface actually exists on a machine. Returns error otherwise */
int validate_interface(char *inf_name)
{
    int status = -1;
    
    struct ifreq *ifr;
    struct ifconf ifc;
    int s, rc, i;
    int numif;

    // find number of interfaces.
    memset(&ifc,0,sizeof(ifc));
    ifc.ifc_ifcu.ifcu_req = NULL;
    ifc.ifc_len = 0;

    if ((s = socket(AF_INET, SOCK_DGRAM, 0)) < 0)
    {
        perror("socket");
    }

    if ((rc = ioctl(s, SIOCGIFCONF, &ifc)) < 0)
    {
        perror("ioctl");
    }

    numif = ifc.ifc_len / sizeof(struct ifreq);
    
    if ((ifr = malloc(ifc.ifc_len)) == NULL) 
    {
        perror("malloc");
    }
    
    ifc.ifc_ifcu.ifcu_req = ifr;

    if ((rc = ioctl(s, SIOCGIFCONF, &ifc)) < 0)
    {
        perror("ioctl2");
    }
    
    close(s);
    
    for(i = 0; i < numif; i++)
    {
        struct ifreq *r = &ifr[i];
        struct sockaddr_in *sin = (struct sockaddr_in *)&r->ifr_addr;

        log_debug(4, "System interface - %-8s : Interface name passed on commandline - %s\n", r->ifr_name, inf_name);//inet_ntoa(sin->sin_addr));
        if (strncmp(r->ifr_name, inf_name, 8) == 0)
        {
            status = 0;
            break;
        }
    }

    return status;
}

/* insert infnode in the list 
 * returns the new node
 */
 // This is basically insert at head in circular list so nodes inserted wll be in LIFO order
infnode_t *ins_infnode(infnode_t *list, infnode_t *i) {
  assert((list != NULL) && (i != NULL));
  i->next = list->next;
  list->next = i;
  return i;
} 

/* add an interface */
/* note: cname must be allocated! */
infnode_t *add_interface(infnode_t *list, char *cname, const int maxlen)
{
  infnode_t *i;
  assert(list != NULL);
  i = alloc_infnode();
  i->inf = cname;
  ins_infnode(list, i);
  return i;
}

/* wrapper for setting current server */
srvnode_t *set_current(infnode_t *i, srvnode_t *s) {
  assert(i!=NULL);
  if (i == NULL) return NULL;
  if (s) {
      log_msg(LOG_NOTICE, "Setting server %s for interface %s",
		inet_ntoa(s->addr.sin_addr), (i->inf));
  } else 
    log_msg(LOG_WARNING, "No active servers for inf %s", 
	    (i->inf));
  return (i->current = s);
}

/* next active server
   Returns: NULL - if there are no active servers in list
            the next active server otherwise
*/
srvnode_t *next_active(infnode_t *i) {
  srvnode_t *s, *start;
  assert(i!=NULL);
  if (i->current) {
    start=i->current;
  } else { /* previously deactivated everything, lets check from start */
    start=i->srvlist;
  }
  for (s=start->next; s->inactive && s != start; s = s->next);
  if (s->inactive) s=NULL;
  return (s);
}

/* deactivate current server
   Returns: next active server, NULL if there are none 
*/
srvnode_t *deactivate_current(infnode_t *i) {
  assert(i!=NULL);
  if (i->current) {
    i->current->inactive = time(NULL);
    log_msg(LOG_NOTICE, "Deactivating DNS server %s",
	      inet_ntoa(i->current->addr.sin_addr));
  }
  return set_current(i, next_active(i));
}


/* reactivate all dns servers */
void reactivate_srvlist(infnode_t *i) {
  srvnode_t *s;
  assert(i!=NULL); /* should never be called with NULL */ 
  s = i->srvlist;
  while ((s = s->next) && (s != i->srvlist)){
    s->inactive = 0;
    s->send_time = 0;
  }
}

/* reactivate servers that have been inactive for delay seconds */
void retry_srvlist(infnode_t *i, const int delay) {
  time_t now = time(NULL);
  srvnode_t *s;
  assert(i!=NULL); /* should never happen */
  s = i->srvlist;
  while ((s = s->next) && (s != i->srvlist))
    if (s->inactive && (now - s->inactive) >= delay ) {
      s->inactive=now;
      udp_send_dummy(s);
    }
}

/* removes a node from the list.
 * returns the deleted node 
 */
infnode_t *del_infnode(infnode_t *list) {
  infnode_t *i = list->next;
  assert((list != NULL));
  list->next = i->next;
  return i;
}

/* destroys the server list and frees the mem */
infnode_t *destroy_infnode(infnode_t *i) {
  if (i==NULL) {
    log_debug(1, "tried to destroy a NULL infnode"); 
    return NULL;
  }
  assert((i != NULL));
  if (i->srvlist) destroy_srvlist(i->srvlist);
  free(i);
  return NULL;
}

/* empties a linked server list. returns the head */
infnode_t *empty_inflist(infnode_t *head) {
  infnode_t *i=head;
  assert(i!=NULL);
  while (i->next != head) {
    destroy_infnode(del_infnode(i));
  }
  return (head);
}

/* destroys the interface list, including the head */
infnode_t *destroy_inflist(infnode_t *head) {
  assert(head!=NULL);
  empty_inflist(head);
  destroy_infnode(head);
  return NULL;
}

//#if 0
#define MAX_GROUPS 100 // UNIQUE IPs
unsigned int hash_array[MAX_GROUPS];
int current_index;

int is_duplicate(unsigned int ip)
{
    int i;
    for(i=0; i<current_index; i++)
    {
        if (ip == hash_array[i])
	    return i;            
    }
    
    return -1;
}

char sortd_str[100][20];
char group[100][40]; /* Array of unique ip addresses */
int group_cnt;      /* Count of unique IP's */
char curr_if[10][10];
int if_index;

char *is_if_duplicate( char *if_name)
{
    int c=0;
    for(; c<if_index; c++)
        if(strcmp(curr_if[c],if_name) == 0)
            return curr_if[c];
            
    return NULL;
}

/* Sort IP:Interfaces */
void sort (void)
{
    /* Loop through to find all unique ip's */
    int group_index = 0;
    memset(hash_array, 0, MAX_GROUPS);
    group_cnt = 0;
    
    char temp_arr[100];
    memset(temp_arr, 0, 100);
         
    infnode_t *i = inf_list->next;
    do
    {
        srvnode_t *s = i->srvlist->next;
        do
        {
            unsigned int ip_value = *((int *) &(s->addr.sin_addr));
            
            if ( (group_index = is_duplicate(ip_value)) == -1) // if it's a new ip
            {
                //log_msg(LOG_NOTICE, "DNS server from inf name %s %s and integer value %d",i->inf, inet_ntoa(s->addr.sin_addr), s->addr.sin_addr);
                
                sprintf(group[group_cnt], "%s : %s", inet_ntoa(s->addr.sin_addr), i->inf);
                //printf("SORTED %s\n", group[current_index]);
                hash_array[group_cnt++] = ip_value;
                
                //break;                // we picked one ip from current interface so proceed to next interface
                //strcpy(sortd_str[current_index], inet_ntoa(s->addr.sin_addr));
                //strcat(sortd_str[current_index], i->inf);
            }
            
            else
	        {
		        //printf("Duplicate ip %u\n", ip_value);
		        sprintf(group[group_index], "%s - %s : %s", group[group_index], inet_ntoa(s->addr.sin_addr), i->inf);	
	        }
            
            s = s->next;
        }while (s != i->srvlist);
        
        i = i->next;
    } while (i != inf_list);
    
    //printf("Current Indx (Unique IP's) : %d \n", group_cnt);
}
    #if 0
    int c;
    for (c=0; c<current_index; c++)
 	    printf("%s\n", group[c]);
    printf("PLAIN PRINTING END\n\n");
	
	int pass=0;    
	for(; pass < 2; pass++)
	{    
	    //  Now pick server interface pair from each group while avoiding same interface twice in a single iteration
	    printf("Now pick server interface pair from each group\n");
	    int s; // count of pairs picked up in the current iteration
        if_index = 0;
    	
	    for(c=0, s=0; c<current_index && s<3; c++)
	    {
	        //printf(" ORIG group[c] before token %s\n", group[c]);
	        char *tok = strtok(group[c], "-\0");
	        if(tok == NULL)
	            continue;
            //printf(" tok %s\n", tok);
            
            char *e = strstr(group[c], "eth"); // get pointer to where interface name exists
            //printf(" intrface name %s\n", e);
        
            if(is_if_duplicate(e) == NULL)   // if the same interface occurs again, we don't include it in this round
            {
                strncpy(curr_if[if_index++], e, 8);
                strcpy(sortd_str[c], tok);
	            
	            while (tok != NULL)
	            {
	                tok = strtok(NULL, "-\0");
	                if(tok == NULL)
	                    break;
	                    
	                strcat(temp_arr, tok);
	                //printf("TOK in middle %s\n", tok);
       	        }
       	        
       	        strcpy(group[c], temp_arr);
       	        //printf("TOTAL rem string %s\n", group[c]);
                s++;      
            }
            printf(" Sorted %s\n", sortd_str[c]);	        
	    }
	}
}
#endif


#if 0
/* init the linked domain list
 * returns ptr to the head/tail dummy node in an empty list
 */

domnode_t *init_domainlist(void) {
  domnode_t *p = alloc_domnode();
  
  return p;
}

/* search for the domnode that has the domain. Returns domnode if
   domain is a subdomain under domnode */

domnode_t *search_subdomnode(domnode_t *head, const char *name, 
			     const int maxlen) {
  domnode_t *d=head, *curr=head;
  int h,n, maxfound=0;
  const char *p;
  assert( (head != NULL) && (name != NULL));
  /* the list head is pointing to the default domain */
  if ((name == NULL) || (d == NULL)) return head;
  while ((d=d->next) != head) {
    if ( (n = strnlen(name, maxlen)) > (h = strnlen(d->domain, maxlen))) {
      p = name + n - h;
    } else p=name;

    /* this works because the domain names are in cname format so
       hayes.org will nor appear as a subdomain under yes.org yes.org
       will be encoded as "\3yes\3org" while hayes.org will be encoded
       as "\5hayes\3org"
    */
    if ((strncmp(d->domain, p, maxlen - (p - name)) == 0) && (h > maxfound)) {
      maxfound = h; /* max length found */
      curr = d;
    }
  }
  return curr;
}

#endif
