#include <iostream>
#include <cstring>
using namespace std;

#define MAX_UNIQUE_IP 50 // UNIQUE IPs
#define UNIQUE_IP_LEN 30 // One ip can have multiple entries for each interface
#define TOTAL_NODES 100
#define EACH_NODE_LEN 25
#define INTF_PER_IP 10
#define TOTAL_INTF 10 // with in a single round

unsigned int hash_array[MAX_UNIQUE_IP];
char sortd_str[TOTAL_NODES][EACH_NODE_LEN];
char group[MAX_UNIQUE_IP][INTF_PER_IP][EACH_NODE_LEN];
char curr_if[TOTAL_INTF][EACH_NODE_LEN];
int if_index;

int is_ip_duplicate(unsigned int ip)
{
    int i;
    for(i=0; i<5; i++)
    {
        if (ip == hash_array[i])
	    return i;            
    }
    
    return -1;
}

char *is_if_duplicate( char *if_name)
{
    int c=0;
    for(; c<if_index; c++)
        if(strcmp(curr_if[c],if_name) == 0)
            return curr_if[c];
            
    return NULL;
}

int main()
{
    /* Loop through to find all unique ip's */
    int group_index = 0;
    memset(hash_array, 0, MAX_UNIQUE_IP);

//-s 1.1.1.1:eth2 -s 2.2.2.2:eth2 -s 1.1.1.1:eth2:1
//-s 2.2.2.2:eth2:1 -s 3.3.3.3:eth2 -s 4.4.4.4:eth2:2

    strcpy(group[0][0], "1.1.1.1:eth2");
    strcpy(group[0][1], "1.1.1.1:eth2:1");

    strcpy(group[1][0], "2.2.2.2:eth2");
    strcpy(group[1][1], "2.2.2.2:eth2:1");

    strcpy(group[2][0], "3.3.3.3:eth2");
    //strcpy(group[3][0], "4.4.4.4:eth2:2");

    //strcpy(group[4][0], "5.5.5.5:eth2:1");

    //  Now pick server interface pair from each group while avoiding same interface twice in a single iteration
    printf("Now pick server interface pair from each group\n");
    int s; // count of pairs picked up in the current pass
    int ip, intf;
    int c = 0;
    int look_more = 1;
    int pass;
    for (pass=0; look_more ;pass++)
    {
        // pass variable and it's use is only for debugging to the round number
        //if (pass == 3)
          //  break;

        // clear the currently chosen interface array.
        if_index = 0;
        look_more = 0;
        s = 0;

        // for each unique ip
        for(ip=0; ip<5 /* max unique ips*/ ; ip++)
        {
            // for each interface with in an ip
            for(intf=0; intf<5 /* MAX INTERFACES */; intf++)
            {
                char *e = strstr(group[ip][intf], "eth"); // get pointer to where interface name exists

                if (e==NULL)
                    continue;

                if(is_if_duplicate(e) == NULL)   // if the same interface as picked up earlier
                {
                    look_more = 1;

                    strncpy(curr_if[if_index++], e, 8);
                    strcpy(sortd_str[c], group[ip][intf]);

                    // Remove the selected ip/intf from array
                    strcpy(group[ip][intf], "");
                    c++;
                    s++;
                    break; /* Move to next ip */
                }
            }

            //if(s == 3) // if we have picked up 3 already in this turn then look no further
            //    break; // move to next round
        }
    }

    for(int i=0; i<c; i++)
        cout << sortd_str[i] << endl;
    return 0;
}