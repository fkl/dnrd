#include <iostream>
#include <cstring>
using namespace std;

#define MAX_GROUPS 100 // UNIQUE IPs
unsigned int hash_array[MAX_GROUPS];
int current_index;

char sortd_str[100][20];
char group[100][40];
char curr_if[10][10];
int if_index;

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
    cout << "testing sorting" << endl;

    /* Loop through to find all unique ip's */
    int group_index = 0;
    memset(hash_array, 0, MAX_GROUPS);
    current_index = 4;
    
    char temp_arr[100];
    memset(temp_arr, 0, 100);

//-s 1.1.1.1:eth2 -s 2.2.2.2:eth2 -s 1.1.1.1:eth2:1
//-s 2.2.2.2:eth2:1 -s 3.3.3.3:eth2 -s 4.4.4.4:eth2:2

    strcpy(group[0], "1.1.1.1:eth2 - 1.1.1.1:eth2:1");
    strcpy(group[1], "2.2.2.2:eth2 - 2.2.2.2:eth2:1");
    strcpy(group[2], "3.3.3.3:eth2");
    strcpy(group[3], "4.4.4.4:eth2:2");
    
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
	        char *tok = strtok(group[c], "-\n");
            //printf(" tok %s\n", tok);
 	        //if(tok == NULL)
	        //    continue;

            while (tok != NULL)
            {
                char *e = strstr(group[c], "eth"); // get pointer to where interface name exists

                if(is_if_duplicate(e) == NULL)   // if the same interface occurs again, we don't include it in this round
                {
                    strncpy(curr_if[if_index++], e, 8);
                    strcpy(sortd_str[c], tok);

                    // Remove the chosen token from array
                    strcpy(group[c], group[c + strlen(tok)]);
                    break;
                }

                tok = strtok(NULL, "-\0");
            }

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

            else // this interface is already taken in current turn, try any other ones
            {
                
            }

            printf(" Sorted %s\n", sortd_str[c]);	        
	    }
	}

    for(int i=0; i<6; i++)
        cout << sortd_str[i] << endl;
    return 0;
}