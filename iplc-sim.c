#define _CRT_SECURE_NO_WARNINGS
/***********************************************************************/
/***********************************************************************
 Pipeline Cache Simulator
 ***********************************************************************/
/***********************************************************************/
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h> //Unix function not on windows...
#include <string.h>
#include <math.h>
#include <assert.h>

#define MAX_CACHE_SIZE 10240
#define CACHE_MISS_DELAY 10 // 10 cycle cache miss penalty
#define MAX_STAGES 5
#define TRUE 1
#define FALSE 0

typedef short int BIT;
typedef unsigned int uint;
typedef unsigned char BYTE;

// init the simulator
void iplc_sim_init(int index, int blocksize, int assoc);

// Cache simulator functions
void iplc_sim_LRU_replace_on_miss(int index, int tag);
void iplc_sim_LRU_update_on_hit(int index, int assoc);
int iplc_sim_trap_address(unsigned int address);

// Pipeline functions
unsigned int iplc_sim_parse_reg(char *reg_str);
void iplc_sim_parse_instruction(char *buffer);
void iplc_sim_push_pipeline_stage();
void iplc_sim_process_pipeline_rtype(char *instruction, int dest_reg,
                                     int reg1, int reg2_or_constant);
void iplc_sim_process_pipeline_lw(int dest_reg, int base_reg, unsigned int data_address);
void iplc_sim_process_pipeline_sw(int src_reg, int base_reg, unsigned int data_address);
void iplc_sim_process_pipeline_branch(int reg1, int reg2);
void iplc_sim_process_pipeline_jump(char *instruction);
void iplc_sim_process_pipeline_syscall();
void iplc_sim_process_pipeline_nop();

// Outout performance results
void iplc_sim_finalize();

typedef struct cache_line
{
	int validBit;
    // Your data structures for implementing your cache should include:
    // a valid bit
    // a tag
    // a method for handling varying levels of associativity
    // a method for selecting which item in the cache is going to be replaced
	BIT valid_bit;
	uint tag;
	struct cache_line *prev, *next, *set_head, *set_tail;



} cache_line_t;

cache_line_t *cache=NULL;
int cache_index=0;
int cache_blocksize=0;
int cache_blockoffsetbits = 0;
int cache_assoc=0;
long cache_miss=0;
long cache_access=0;
long cache_hit=0;

char instruction[16];
char reg1[16];
char reg2[16];
char offsetwithreg[16];
unsigned int data_address=0;
unsigned int instruction_address=0;
unsigned int pipeline_cycles=0;   // how many cycles did you pipeline consume
unsigned int instruction_count=0; // home many real instructions ran thru the pipeline
unsigned int branch_predict_taken=0;
unsigned int branch_count=0;
unsigned int correct_branch_predictions=0;

unsigned int debug=0;
unsigned int dump_pipeline=1;

enum instruction_type {NOP, RTYPE, LW, SW, BRANCH, JUMP, JAL, SYSCALL};

typedef struct rtype
{
    char instruction[16];
    int reg1;
    int reg2_or_constant;
    int dest_reg;
    
} rtype_t;

typedef struct load_word
{
    unsigned int data_address;
    int dest_reg;
    int base_reg;
    
} lw_t;

typedef struct store_word
{
    unsigned int data_address;
    int src_reg;
    int base_reg;
} sw_t;

typedef struct branch
{
    int reg1;
    int reg2;
    
} branch_t;


typedef struct jump
{
    char instruction[16];
    
} jump_t;

typedef struct pipeline
{
    enum instruction_type itype;
    unsigned int instruction_address;
    union
    {
        rtype_t   rtype;
        lw_t      lw;
        sw_t      sw;
        branch_t  branch;
        jump_t    jump;
    }
    stage;
    
} pipeline_t;

enum pipeline_stages {FETCH, DECODE, ALU, MEM, WRITEBACK};

pipeline_t pipeline[MAX_STAGES];

/************************************************************************************************/
/* Cache Functions ******************************************************************************/
/************************************************************************************************/
/*
 * Correctly configure the cache.
 */
void iplc_sim_init(int index, int blocksize, int assoc)
{
    int i=0, j=0;
    unsigned long cache_size = 0;
    cache_index = index;
    cache_blocksize = blocksize;
    cache_assoc = assoc;
    
    
    cache_blockoffsetbits =
    (int) rint((log( (double) (blocksize * 4) )/ log(2)));
    /* Note: rint function rounds the result up prior to casting */
    
    cache_size = assoc * ( 1 << index ) * ((32 * blocksize) + 33 - index - cache_blockoffsetbits);
    
    printf("Cache Configuration \n");
    printf("   Index: %d bits or %d lines \n", cache_index, (1<<cache_index) );
    printf("   BlockSize: %d \n", cache_blocksize );
    printf("   Associativity: %d \n", cache_assoc );
    printf("   BlockOffSetBits: %d \n", cache_blockoffsetbits );
    printf("   CacheSize: %lu \n", cache_size );
    
    if (cache_size > MAX_CACHE_SIZE ) {
        printf("Cache too big. Great than MAX SIZE of %d .... \n", MAX_CACHE_SIZE);
        exit(-1);
    }
    
    cache = (cache_line_t *) malloc((sizeof(cache_line_t) * 1<<index));
    
	// Dynamically create our cache based on the information the user entered
	if (cache_assoc > 1) {
		for (i = 0; i < (1 << index); i++) {
			//If current cache_line index is first in set, set as set_head
			if (i % assoc == 0) {
				//Set the current cache_line as the head for the rest of the set...
                //And connect the set with pointers...
				for (j = 0; j < assoc; j++) {
					cache[i + j].set_head = &cache[i];
                    //If head of set next ptr, but not prev. (no prev for head)
                    if(j == 0){
                        cache[i+j].next = &cache[i+j+1];
                        cache[i+j].prev = NULL;
                    }
                    //If at the last item in set, set prev, but not next ptr
                    else if(j == assoc-1){
                        cache[i+j].prev = &cache[i+j-1];
                        cache[i+j].next = NULL;
                    }
                    //If not tail or head, set both prev and next ptrs
                    else{
                        cache[i+j].prev = &cache[i+j-1];
                        cache[i+j].next = &cache[i+j+1];
                    }
				}

			}
			//Else if last item in set mark as tail
			else if (i % assoc == assoc - 1) {
				//Set the current cache_line as the tail for the set...
				for (j = 0; j < assoc; j++) {
					cache[i - j].set_tail = &cache[i];
				}
			}
			// //Set other struct members  NULL
			// cache[i].prev = cache[i].next = NULL;
            cache[i].valid_bit = 0;
            cache[i].tag = 0;
		}
	}
	else {
        //printf("We get here\n");
		cache_line_t* head = NULL;
		cache_line_t* tail = NULL;
		for (i = 0; i < (1 << index); i++) {
            //If head
			if (i == 0) {
				head = &cache[i];
                cache[i].next = &cache[i+1];
                cache[i].prev = NULL;
			}
            //If at tail
			else if (i == (1 << index) - 1) {
                //printf("we get here 2\n");
				tail = &cache[i];
                cache[i].prev = &cache[i-1];
                cache[i].next = NULL;
			}
            //Set head for all items in cache
			cache[i].set_head = head;
            cache[i].valid_bit = 0;
            cache[i].tag = 0;
			// //Set other struct members to NULL
			// cache[i].prev = cache[i].next = NULL;
		}
        //set the tail for all items in cache and connect pointers
        for (j = 0; j < (1 << index); j++) {
            cache[j].set_tail = tail;
            //Excluding head and tail bc already set next and prev ptrs abovev for loop
            if (j == 0){
                continue;
            }
            else if (j == (1 << index) - 1){
                continue;
            }
            cache[j].next = &cache[j+1];
            cache[j].prev = &cache[j-1];
        }
	}
    //printf("It get's set correctly\n");
    // init the pipeline -- set all data to zero and instructions to NOP
    for (i = 0; i < MAX_STAGES; i++) {
        // itype is set to O which is NOP type instruction
        bzero(&(pipeline[i]), sizeof(pipeline_t));
    }
}

/*
 * iplc_sim_trap_address() determined this is not in our cache.  Put it there
 * and make sure that is now our Most Recently Used (MRU) entry.
 */
void iplc_sim_LRU_replace_on_miss(int index, int tag)
{
    /* You must implement this function */


    //PROBABLY NEEDS SOME MODIFICATION FOR THE DIFFERENT ASSOCIATIVITES, IE if its 1 at least.



    //finds the head and tail of the sets with an iterator at ptr
    cache_line_t* head = cache[index].set_head;
    cache_line_t* tail = cache[index].set_tail;
    cache_line_t* ptr = head;
    cache_line_t* temp = NULL;
    cache_line_t* temp2 = NULL;

    //looks through the list to see if there are any empty spaces, and if there is not it goes to the last space
    while(ptr){
        //If there is an empty space
        //There are three cases (head, tail, or body of set)
        if (ptr->valid_bit == 0){
            ptr->valid_bit = 1;
            ptr->tag = tag;
            //If the empty space is the head of the set
            if (ptr == head){
                //Do nothing (already set)
                return;
            }
            //If empty space is the end of set
            else if (ptr == tail){
               ptr->next = head;
               head->prev = ptr;
               tail = ptr->prev;
               ptr->prev->next = NULL; //New tail!
               ptr->prev = NULL; //New head MRU; updated!
               head = ptr;
               //update set_tail and set_head
               while(ptr){
                ptr->set_head = head;
                ptr->set_tail = tail;
                ptr = ptr->next;
               }
               return;
            }
            //If empty space is within the set but not the head or tail
            else{
                head-> prev = ptr;
                temp = ptr->next;
                ptr->next = head;
                temp2 = ptr->prev;
                ptr->prev = NULL;
                head = ptr; //New head!
                temp->prev = temp2;
                temp2->next = temp;
                //Tail is not updated, but set_head is
                while(ptr){
                    ptr->set_head = head;
                    ptr = ptr->next;
                }
                return;

            }
        }
        ptr = ptr->next;
    }
    //Went through the set, but no empty spaces...
    ptr = tail;
    
    while(ptr && ptr->prev){
        //Move tags and valid bits one down
        ptr->tag = ptr->prev->tag;
        ptr = ptr->prev;
    }
    head->valid_bit = 1;
    head->tag = tag;
   
}

/*
 * iplc_sim_trap_address() determined the entry is in our cache.  Update its
 * information in the cache.
 */
void iplc_sim_LRU_update_on_hit(int index, int assoc_entry)
{
    /* You must implement this function */
	int i = 0;
    int set_size = cache_assoc;
	cache_line_t* head = cache[index].set_head;
	cache_line_t* tail = cache[index].set_tail;
	cache_line_t* ptr = head;

	//Find the entry within the set that hit
    if (cache_assoc > 1){
        while ((i < assoc_entry) && ptr) {
        ptr = ptr->next;
        i++;
        }
    }
    else{
        while((i < assoc_entry) && ptr){
            ptr = ptr->next;
            i++;
        }
        set_size = (cache_index << 1); //if direct mapped
    }
    
	
	assert(ptr->valid_bit);

	//If entry that hit is already the head/MRU of set
	if (ptr->set_head == head) {
		//do nothing
		return;
	}
	//If entry that hit is the tail
	else if (ptr->set_tail == ptr) {
		ptr->prev->next = NULL;
	}
	else {
		//Remove entry that hit from place in queue 
		ptr->prev->next = ptr->next;
		ptr->next->prev = ptr->prev;
	}

	//Move entry that hit to MRU
	head->prev = ptr;
	ptr->next = head;
	ptr->prev = NULL;
	head = ptr;

	//Get new set_tail
	while (ptr->next) {
		ptr = ptr->next;
	}
	tail = ptr;

	//Update the head_set & tail_set pointer for all items in set
    ptr = head;
    while(ptr){
        ptr->set_head = head;
        ptr->set_tail = tail;
        ptr = ptr->next;
    }
	// for (i = 0; i < set_size; i++) {
	// 	ptr->set_head = head;
	// 	ptr->set_tail = tail;
	// 	ptr = ptr->next;
	// }

}

/*
 * Check if the address is in our cache.  Update our counter statistics 
 * for cache_access, cache_hit, etc.  If our configuration supports
 * associativity we may need to check through multiple entries for our
 * desired index.  In that case we will also need to call the LRU functions.
 */
int iplc_sim_trap_address(unsigned int address)
{
	int i = 0, index = 0;
	int tag = 0;
	int hit = 0;

	cache_access++; //Cache is accessed

	uint other_bits = cache_blockoffsetbits + cache_index;
	uint bit_mask = ((1 << cache_index) - 1) << cache_blockoffsetbits;

	tag = address >> other_bits;
	index = (bit_mask & address) >> cache_blockoffsetbits; //if index represents actual lines and not sets, will have to fix
	cache_line_t* head = cache[index].set_head;
	cache_line_t* tail = cache[index].set_tail;
	cache_line_t* ptr = head;


	// if (cache_assoc > 1) { 
	// 	index = index % cache_assoc;
	// }
    //printf("It works here!!\n");
    
	while (ptr) {
		if ((ptr->valid_bit) && (ptr->tag == tag)) {
			//hit
			hit = 1;
			cache_hit++;
            //printf("We get here 3\n");
			iplc_sim_LRU_update_on_hit(index, i);
			return hit;
		}
        //printf("i: %d\n", i);
        //printf("ptr->tag: %d; ptr->valid_bit: %d\n", ptr->tag, ptr->valid_bit);
		i++;
       
		ptr = ptr->next;
	}
	//For loop ends; address is not yet stored
	//miss
    //printf("We get here 4\n");
	cache_miss++;
	iplc_sim_LRU_replace_on_miss(index, tag);
	// if (cache[index].valid_bit){
	//     if (cache[index].tag == tag){
	//         //Hit
	//         hit = 1;
	//         cache_hit++;
	//         iplc_sim_LRU_update_on_hit(index, i);
	//     }
	//     else{

	//     }
	// }




	// Call the appropriate function for a miss or hit

	/* expects you to return 1 for hit, 0 for miss */
	return hit;

/*
    int i=0, index=0; //I - i is the empty set value, index is the index in our cache
    int tag=0;//I - the tag is the data being stowed in the cache
    int hit=0;//I - if there was a hit
    
    //finding the index via bi twiddling
    int temp = address;
    temp = temp >> cache_blockoffsetbits;
    index = 1;
    index = index << cache_index+1;
    index = index -1;
    index = index & temp;

    //finding the tag
    //doesn't work yet, I think I'm close though, working on using bit shifting to find it
    tag = address >> (cache_blockoffsetbits+cache_index);

    //if()

    // Call the appropriate function for a miss or hit

    if(hit){
        iplc_sim_LRU_update_on_hit(index,tag);//I - I presume we store tag an not address?
    }
    else{
        iplc_sim_LRU_replace_on_miss(index,tag);
    }
    // expects you to return 1 for hit, 0 for miss
    return hit;*/
}

/*
 * Just output our summary statistics.
 */
void iplc_sim_finalize()
{
    /* Finish processing all instructions in the Pipeline */
    while (pipeline[FETCH].itype != NOP  ||
           pipeline[DECODE].itype != NOP ||
           pipeline[ALU].itype != NOP    ||
           pipeline[MEM].itype != NOP    ||
           pipeline[WRITEBACK].itype != NOP) {
        iplc_sim_push_pipeline_stage();
    }
    printf(" Cache Performance \n");
    printf("\t Number of Cache Accesses is %ld \n", cache_access);
    printf("\t Number of Cache Misses is %ld \n", cache_miss);
    printf("\t Number of Cache Hits is %ld \n", cache_hit);
    printf("\t Cache Miss Rate is %f \n\n", (double)cache_miss / (double)cache_access);
    printf("Pipeline Performance \n");
    printf("\t Total Cycles is %u \n", pipeline_cycles);
    printf("\t Total Instructions is %u \n", instruction_count);
    printf("\t Total Branch Instructions is %u \n", branch_count);
    printf("\t Total Correct Branch Predictions is %u \n", correct_branch_predictions);
    printf("\t CPI is %f \n\n", (double)pipeline_cycles / (double)instruction_count);
}

/************************************************************************************************/
/* Pipeline Functions ***************************************************************************/
/************************************************************************************************/

/*
 * Dump the current contents of our pipeline.
 */
void iplc_sim_dump_pipeline()
{
    int i;
    
    for (i = 0; i < MAX_STAGES; i++) {
        switch(i) {
            case FETCH:
                printf("(cyc: %u) FETCH:\t %d: 0x%x \t", pipeline_cycles, pipeline[i].itype, pipeline[i].instruction_address);
                break;
            case DECODE:
                printf("DECODE:\t %d: 0x%x \t", pipeline[i].itype, pipeline[i].instruction_address);
                break;
            case ALU:
                printf("ALU:\t %d: 0x%x \t", pipeline[i].itype, pipeline[i].instruction_address);
                break;
            case MEM:
                printf("MEM:\t %d: 0x%x \t", pipeline[i].itype, pipeline[i].instruction_address);
                break;
            case WRITEBACK:
                printf("WB:\t %d: 0x%x \n", pipeline[i].itype, pipeline[i].instruction_address);
                break;
            default:
                printf("DUMP: Bad stage!\n" );
                exit(-1);
        }
    }
}

/*
 * Check if various stages of our pipeline require stalls, forwarding, etc.
 * Then push the contents of our various pipeline stages through the pipeline.
 */
void iplc_sim_push_pipeline_stage()
{
	int i;
	int data_hit = 1;
	int normalProcessing = TRUE;
	int NOP_ToInsert = 0;
	/* 1. Count WRITEBACK stage is "retired" -- This I'm giving you */
	if (pipeline[WRITEBACK].instruction_address) {
		instruction_count++;
		if (debug)
			printf("DEBUG: Retired Instruction at 0x%x, Type %d, at Time %u \n",
				pipeline[WRITEBACK].instruction_address, pipeline[WRITEBACK].itype, pipeline_cycles);
	}

	/* 2. Check for BRANCH and correct/incorrect Branch Prediction */
	if (pipeline[DECODE].itype == BRANCH) {
		int branch_taken = FALSE;
		branch_count++;
		//assuming that the instructions are in order, the next instruction will just be the one in the fetch stage
        if ((pipeline[DECODE].instruction_address + 4) != pipeline[FETCH].instruction_address) {
            //branch was taken
			branch_taken = TRUE;
        }
		if (branch_taken == branch_predict_taken) {
            if(pipeline[FETCH].itype != NOP) { //if the next instruction is a NOP then don't update correct branch predictions
                correct_branch_predictions++;
            }
		}
        else { //prediction was wrong and a NOP needs to be inserted
            if(pipeline[FETCH].itype!=NOP) {                
			    pipeline_cycles+=2; //extra stall cycle
                NOP_ToInsert++;
			    normalProcessing = FALSE;
            }
        }
	}

	/* 3. Check for LW delays due to use in ALU stage and if data hit/miss
	 *    add delay cycles if needed.
	 */
	if (pipeline[MEM].itype == LW) {
		int inserted_nop = 0;
		//check for data miss
		data_hit = iplc_sim_trap_address(pipeline[MEM].stage.lw.data_address);
        if (!data_hit) { //not found in cache, need to add stall
            printf("DATA MISS: ADDRESS 0x%x\n", pipeline[MEM].stage.lw.data_address);
			pipeline_cycles += 10;
			normalProcessing = FALSE;
        }
        else {
            printf("DATA HIT: ADDRESS 0x%x\n", pipeline[MEM].stage.lw.data_address);
        }
		//need to check for the ALU delays
	}

	/* 4. Check for SW mem acess and data miss .. add delay cycles if needed */
	if (pipeline[MEM].itype == SW) {
		data_hit = iplc_sim_trap_address(pipeline[MEM].stage.sw.data_address);
        if (!data_hit) { //not found in cache, need to add stall
            printf("DATA MISS: ADDRESS 0x%x\n", pipeline[MEM].stage.sw.data_address);
			pipeline_cycles += 10;
			normalProcessing = FALSE;
        }
        else {
            printf("DATA HIT: ADDRESS 0x%x\n", pipeline[MEM].stage.sw.data_address);            
        }
	}

	/* 5. Increment pipe_cycles 1 cycle for normal processing */
	if (normalProcessing) {
		pipeline_cycles++; //if normalProcessing is false than the pipeline cycles have already been added
	}
    
	/* 6. push stages thru MEM->WB, ALU->MEM, DECODE->ALU, FETCH->DECODE */
	//MEM-WB
	pipeline[WRITEBACK] = pipeline[MEM];
	//pipeline[WRITEBACK].itype = pipeline[MEM].itype;
	//pipeline[WRITEBACK].instruction_address = pipeline[MEM].instruction_address;
    //ALU->MEM
    pipeline[MEM] = pipeline[ALU];
	//pipeline[MEM].itype = pipeline[ALU].itype;
	//pipeline[MEM].instruction_address = pipeline[ALU].instruction_address;
	//Decode->ALU
    pipeline[ALU]=pipeline[DECODE];
    //pipeline[ALU].itype = pipeline[DECODE].itype;
	//pipeline[ALU].instruction_address = pipeline[DECODE].instruction_address;
	//FETCH->DECODE
    pipeline[DECODE] = pipeline[FETCH];
    //pipeline[DECODE].itype = pipeline[FETCH].itype;
	//pipeline[DECODE].instruction_address = pipeline[FETCH].instruction_address;

    
    // 7. This is a give'me -- Reset the FETCH stage to NOP via bezero */
    bzero(&(pipeline[FETCH]), sizeof(pipeline_t));
}

/*
 * This function is fully implemented.  You should use this as a reference
 * for implementing the remaining instruction types.
 */
void iplc_sim_process_pipeline_rtype(char *instruction, int dest_reg, int reg1, int reg2_or_constant)
{
    /* This is an example of what you need to do for the rest */
    iplc_sim_push_pipeline_stage();
    
    pipeline[FETCH].itype = RTYPE;
    pipeline[FETCH].instruction_address = instruction_address;
    
    strcpy(pipeline[FETCH].stage.rtype.instruction, instruction);
    pipeline[FETCH].stage.rtype.reg1 = reg1;
    pipeline[FETCH].stage.rtype.reg2_or_constant = reg2_or_constant;
    pipeline[FETCH].stage.rtype.dest_reg = dest_reg;
}

void iplc_sim_process_pipeline_lw(int dest_reg, int base_reg, unsigned int data_address)
{
    /* You must implement this function */
	iplc_sim_push_pipeline_stage();

	pipeline[FETCH].itype = LW;
	pipeline[FETCH].instruction_address = instruction_address;

	pipeline[FETCH].stage.lw.dest_reg = dest_reg;
	pipeline[FETCH].stage.lw.base_reg = base_reg;
	pipeline[FETCH].stage.lw.data_address = data_address;
}

void iplc_sim_process_pipeline_sw(int src_reg, int base_reg, unsigned int data_address)
{
    /* You must implement this function */
	iplc_sim_push_pipeline_stage();

	pipeline[FETCH].itype = SW;
	pipeline[FETCH].instruction_address = instruction_address;

	pipeline[FETCH].stage.sw.src_reg = src_reg;
	pipeline[FETCH].stage.sw.base_reg = base_reg;
	pipeline[FETCH].stage.sw.data_address = data_address;
}

void iplc_sim_process_pipeline_branch(int reg1, int reg2)
{
    /* You must implement this function */
	iplc_sim_push_pipeline_stage();

	pipeline[FETCH].itype = BRANCH;
	pipeline[FETCH].instruction_address = instruction_address;

	pipeline[FETCH].stage.branch.reg1 = reg1;
	pipeline[FETCH].stage.branch.reg2 = reg2;
}

void iplc_sim_process_pipeline_jump(char *instruction)
{
    /* You must implement this function */
	iplc_sim_push_pipeline_stage();

	pipeline[FETCH].itype = JUMP;
	pipeline[FETCH].instruction_address = instruction_address;

	strcpy(pipeline[FETCH].stage.jump.instruction, instruction);
}

void iplc_sim_process_pipeline_syscall()
{
    /* You must implement this function */
	iplc_sim_push_pipeline_stage();
	
	pipeline[FETCH].itype = SYSCALL;
	pipeline[FETCH].instruction_address = instruction_address;

}

void iplc_sim_process_pipeline_nop()
{
    /* You must implement this function */
	iplc_sim_push_pipeline_stage();

	pipeline[FETCH].itype = NOP;
	pipeline[FETCH].instruction_address = instruction_address;
}

/************************************************************************************************/
/* parse Function *******************************************************************************/
/************************************************************************************************/

/*
 * Don't touch this function.  It is for parsing the instruction stream.
 */
unsigned int iplc_sim_parse_reg(char *reg_str)
{
    int i;
    // turn comma into \n
    if (reg_str[strlen(reg_str)-1] == ',')
        reg_str[strlen(reg_str)-1] = '\n';
    
    if (reg_str[0] != '$')
        return atoi(reg_str);
    else {
        // copy down over $ character than return atoi
        for (i = 0; i < strlen(reg_str); i++)
            reg_str[i] = reg_str[i+1];
        
        return atoi(reg_str);
    }
}

/*
 * Don't touch this function.  It is for parsing the instruction stream.
 */
void iplc_sim_parse_instruction(char *buffer)
{
    int instruction_hit = 0;
    int i=0, j=0;
    int src_reg=0;
    int src_reg2=0;
    int dest_reg=0;
    char str_src_reg[16];
    char str_src_reg2[16];
    char str_dest_reg[16];
    char str_constant[16];
    
    if (sscanf(buffer, "%x %s", &instruction_address, instruction ) != 2) {
        printf("Malformed instruction \n");
        exit(-1);
    }
    
    instruction_hit = iplc_sim_trap_address( instruction_address );
    
    // if a MISS, then push current instruction thru pipeline
    if (!instruction_hit) {
        // need to subtract 1, since the stage is pushed once more for actual instruction processing
        // also need to allow for a branch miss prediction during the fetch cache miss time -- by
        // counting cycles this allows for these cycles to overlap and not doubly count.
        
        printf("INST MISS:\t Address 0x%x \n", instruction_address);
        
        for (i = pipeline_cycles, j = pipeline_cycles; i < j + CACHE_MISS_DELAY - 1; i++)
            iplc_sim_push_pipeline_stage();
    }
    else
        printf("INST HIT:\t Address 0x%x \n", instruction_address);
    
    // Parse the Instruction
    
    if (strncmp( instruction, "add", 3 ) == 0 ||
        strncmp( instruction, "sll", 3 ) == 0 ||
        strncmp( instruction, "ori", 3 ) == 0) {
        if (sscanf(buffer, "%x %s %s %s %s",
                   &instruction_address,
                   instruction,
                   str_dest_reg,
                   str_src_reg,
                   str_src_reg2 ) != 5) {
            printf("Malformed RTYPE instruction (%s) at address 0x%x \n",
                   instruction, instruction_address);
            exit(-1);
        }
        
        dest_reg = iplc_sim_parse_reg(str_dest_reg);
        src_reg = iplc_sim_parse_reg(str_src_reg);
        src_reg2 = iplc_sim_parse_reg(str_src_reg2);
        
        iplc_sim_process_pipeline_rtype(instruction, dest_reg, src_reg, src_reg2);
    }
    
    else if (strncmp( instruction, "lui", 3 ) == 0) {
        if (sscanf(buffer, "%x %s %s %s",
                   &instruction_address,
                   instruction,
                   str_dest_reg,
                   str_constant ) != 4 ) {
            printf("Malformed RTYPE instruction (%s) at address 0x%x \n",
                   instruction, instruction_address );
            exit(-1);
        }
        
        dest_reg = iplc_sim_parse_reg(str_dest_reg);
        src_reg = -1;
        src_reg2 = -1;
        iplc_sim_process_pipeline_rtype(instruction, dest_reg, src_reg, src_reg2);
    }
    
    else if (strncmp( instruction, "lw", 2 ) == 0 ||
             strncmp( instruction, "sw", 2 ) == 0  ) {
        if ( sscanf( buffer, "%x %s %s %s %x",
                    &instruction_address,
                    instruction,
                    reg1,
                    offsetwithreg,
                    &data_address ) != 5) {
            printf("Bad instruction: %s at address %x \n", instruction, instruction_address);
            exit(-1);
        }
        
        if (strncmp(instruction, "lw", 2 ) == 0) {
            
            dest_reg = iplc_sim_parse_reg(reg1);
            
            // don't need to worry about base regs -- just insert -1 values
            iplc_sim_process_pipeline_lw(dest_reg, -1, data_address);
        }
        if (strncmp( instruction, "sw", 2 ) == 0) {
            src_reg = iplc_sim_parse_reg(reg1);
            
            // don't need to worry about base regs -- just insert -1 values
            iplc_sim_process_pipeline_sw( src_reg, -1, data_address);
        }
    }
    else if (strncmp( instruction, "beq", 3 ) == 0) {
        // don't need to worry about getting regs -- just insert -1 values
        iplc_sim_process_pipeline_branch(-1, -1);
    }
    else if (strncmp( instruction, "jal", 3 ) == 0 ||
             strncmp( instruction, "jr", 2 ) == 0 ||
             strncmp( instruction, "j", 1 ) == 0 ) {
        iplc_sim_process_pipeline_jump( instruction );
    }
    else if (strncmp( instruction, "jal", 3 ) == 0 ||
             strncmp( instruction, "jr", 2 ) == 0 ||
             strncmp( instruction, "j", 1 ) == 0 ) {
        /*
         * Note: no need to worry about forwarding on the jump register
         * we'll let that one go.
         */
        iplc_sim_process_pipeline_jump(instruction);
    }
    else if ( strncmp( instruction, "syscall", 7 ) == 0) {
        iplc_sim_process_pipeline_syscall( );
    }
    else if ( strncmp( instruction, "nop", 3 ) == 0) {
        iplc_sim_process_pipeline_nop( );
    }
    else {
        printf("Do not know how to process instruction: %s at address %x \n",
               instruction, instruction_address );
        exit(-1);
    }
}

/************************************************************************************************/
/* MAIN Function ********************************************************************************/
/************************************************************************************************/

int main()
{
    char trace_file_name[1024];
    FILE *trace_file = NULL;
    char buffer[80];
    int index = 10;
    int blocksize = 1;
    int assoc = 1;
    
    printf("Please enter the tracefile: ");
    scanf("%s", trace_file_name);
    
    trace_file = fopen(trace_file_name, "r");
    
    if ( trace_file == NULL ) {
        printf("fopen failed for %s file\n", trace_file_name);
        exit(-1);
    }
    
    printf("Enter Cache Size (index), Blocksize and Level of Assoc \n");
    scanf( "%d %d %d", &index, &blocksize, &assoc );
    
    printf("Enter Branch Prediction: 0 (NOT taken), 1 (TAKEN): ");
    scanf("%d", &branch_predict_taken );
    
    iplc_sim_init(index, blocksize, assoc);
    
    while (fgets(buffer, 80, trace_file) != NULL) {
        iplc_sim_parse_instruction(buffer);
        if (dump_pipeline)
            iplc_sim_dump_pipeline();
    }
    
    iplc_sim_finalize();
    return 0;
}
