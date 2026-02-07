#include <math.h>
#include <stdio.h>
#include <stdlib.h>

#define MAX_CACHE_SIZE 256
#define MAX_BLOCK_SIZE 256

#define UNINITIALIZED_TAG -1

// **Note** this is a preprocessor macro. This is not the same as a function.
// Powers of 2 have exactly one 1 and the rest 0's, and 0 isn't a power of 2.
#define is_power_of_2(val) (val && !(val & (val - 1)))


/*
 * Accesses 1 word of memory.
 * addr is a 16-bit LC2K word address.
 * write_flag is 0 for reads and 1 for writes.
 * write_data is a word, and is only valid if write_flag is 1.
 * If write flag is 1, mem_access does: state.mem[addr] = write_data.
 * The return of mem_access is state.mem[addr].
 */
extern int mem_access(int addr, int write_flag, int write_data);

/*
 * Returns the number of times mem_access has been called.
 */
extern int get_num_mem_accesses(void);

//Use this when calling printAction. Do not modify the enumerated type below.
enum actionType
{
    cacheToProcessor,
    processorToCache,
    memoryToCache,
    cacheToMemory,
    cacheToNowhere
};

/* You may add or remove variables from these structs */
typedef struct blockStruct
{
    int data[MAX_BLOCK_SIZE];
    int dirty;
    int lruLabel;
    int tag;
    int valid;
} blockStruct;

typedef struct cacheStruct
{
    blockStruct blocks[MAX_CACHE_SIZE];
    int blockSize;
    int numSets;
    int blocksPerSet;
    // add any variables for end-of-run stats
} cacheStruct;

/* Global Cache variable */
cacheStruct cache;

typedef struct {
    int block_bits;
    int index_bits;
    int block_offset;
    int set_index;
    int tag;
    int base;
} decoded_address;

void printAction(int, int, enum actionType);

// Cache helpers
void printCache(void);
void reset_cache();

// Bit helpers
int create_mask(int);
int extract_bits(int, int, int);
decoded_address decode(int);

// Block helpers
int block_index(decoded_address*);
int find_first_invalid(decoded_address*);
int find_highest_LRU(decoded_address*);
int find_block_to_replace(decoded_address*);
void update_LRUs(decoded_address*, int);
void evict(int, int);
void touch_block(blockStruct*, int);


/*
 * Set up the cache with given command line parameters. This is
 * called once in main(). You must implement this function.
 */
void cache_init(int blockSize, int numSets, int blocksPerSet)
{
    if (blockSize <= 0 || numSets <= 0 || blocksPerSet <= 0) {
        printf("error: input parameters must be positive numbers\n");
        exit(1);
    }
    if (blocksPerSet * numSets > MAX_CACHE_SIZE) {
        printf("error: cache must be no larger than %d blocks\n", MAX_CACHE_SIZE);
        exit(1);
    }
    if (blockSize > MAX_BLOCK_SIZE) {
        printf("error: blocks must be no larger than %d words\n", MAX_BLOCK_SIZE);
        exit(1);
    }
    if (!is_power_of_2(blockSize)) {
        printf("warning: blockSize %d is not a power of 2\n", blockSize);
    }
    if (!is_power_of_2(numSets)) {
        printf("warning: numSets %d is not a power of 2\n", numSets);
    }
    printf("Simulating a cache with %d total lines; each line has %d words\n",
        numSets * blocksPerSet, blockSize);
    printf("Each set in the cache contains %d lines; there are %d sets\n",
        blocksPerSet, numSets);

    /********************* Initialize Cache *********************/
    cache.blockSize = blockSize;
    cache.numSets = numSets;
    cache.blocksPerSet = blocksPerSet;

    reset_cache(); // Set all the blocks' dirty to 0, lruLabel to 0, valid to 0, and tag to -1.

    return;
}

/*
 * Access the cache. This is the main part of the project,
 * and should call printAction as is appropriate.
 * It should only call mem_access when absolutely necessary.
 * addr is a 16-bit LC2K word address.
 * write_flag is 0 for reads (fetch/lw) and 1 for writes (sw).
 * write_data is a word, and is only valid if write_flag is 1.
 * The return of mem_access is undefined if write_flag is 1.
 * Thus the return of cache_access is undefined if write_flag is 1.
 */
int cache_access(int addr, int write_flag, int write_data)
{

    decoded_address decoded = decode(addr);
    // We have now extracted all the bits we need to do our checks for the blocks


    int open_block = block_index(&decoded);
    // <open_block> is the base index for our open block

    if(open_block == -1){
        // Cache miss, lets find either LRU or empty block and update <open_block>
        open_block = find_block_to_replace(&decoded);

        int evicted = (cache.blocks[open_block].tag * cache.numSets + decoded.set_index) * cache.blockSize;

        evict(evicted, open_block);

        touch_block(cache.blocks + open_block, decoded.tag);

        int start = addr - (addr % cache.blockSize);

        for (int block = 0; block < cache.blockSize; block++) {
            cache.blocks[open_block].data[block] = mem_access(start + block, 0, 0);
        }

        printAction(start, cache.blockSize, memoryToCache);
    }

    // At this point, our <open_block> is an index to the block we want to work with, so lets also update LRUs    

    update_LRUs(&decoded, open_block);

    if(write_flag){
        // Write data
        cache.blocks[open_block].data[decoded.block_offset] = write_data;
        cache.blocks[open_block].dirty = 1;
    }

    printAction(addr, 1, write_flag ? processorToCache : cacheToProcessor);

    return write_flag ? 0 : cache.blocks[open_block].data[decoded.block_offset];
}


/*
 * print end of run statistics like in the spec. **This is not required**,
 * but is very helpful in debugging.
 * This should be called once a halt is reached.
 * DO NOT delete this function, or else it won't compile.
 * DO NOT print $$$ in this function
 */
void printStats(void)
{
    printf("End of run statistics:\n");
    return;
}

/*
 * Log the specifics of each cache action.
 *
 *DO NOT modify the content below.
 * address is the starting word address of the range of data being transferred.
 * size is the size of the range of data being transferred.
 * type specifies the source and destination of the data being transferred.
 *  -    cacheToProcessor: reading data from the cache to the processor
 *  -    processorToCache: writing data from the processor to the cache
 *  -    memoryToCache: reading data from the memory to the cache
 *  -    cacheToMemory: evicting cache data and writing it to the memory
 *  -    cacheToNowhere: evicting cache data and throwing it away
 */
void printAction(int address, int size, enum actionType type)
{
    printf("$$$ transferring word [%d-%d] ", address, address + size - 1);

    if (type == cacheToProcessor) {
        printf("from the cache to the processor\n");
    }
    else if (type == processorToCache) {
        printf("from the processor to the cache\n");
    }
    else if (type == memoryToCache) {
        printf("from the memory to the cache\n");
    }
    else if (type == cacheToMemory) {
        printf("from the cache to the memory\n");
    }
    else if (type == cacheToNowhere) {
        printf("from the cache to nowhere\n");
    }
    else {
        printf("Error: unrecognized action\n");
        exit(1);
    }

}

/*
 * Prints the cache based on the configurations of the struct
 * This is for debugging only and is not graded, so you may
 * modify it, but that is not recommended.
 */
void printCache(void)
{
    int blockIdx;
    int decimalDigitsForWaysInSet = (cache.blocksPerSet == 1) ? 1 : (int)ceil(log10((double)cache.blocksPerSet));
    printf("\ncache:\n");
    for (int set = 0; set < cache.numSets; ++set) {
        printf("\tset %i:\n", set);
        for (int block = 0; block < cache.blocksPerSet; ++block) {
            blockIdx = set * cache.blocksPerSet + block;
            if(cache.blocks[set * cache.blocksPerSet + block].valid) {
                printf("\t\t[ %0*i ] : ( V:T | D:%c | LRU:%-*i | T:%i )\n\t\t%*s{",
                    decimalDigitsForWaysInSet, block,
                    (cache.blocks[blockIdx].dirty) ? 'T' : 'F',
                    decimalDigitsForWaysInSet, cache.blocks[blockIdx].lruLabel,
                    cache.blocks[blockIdx].tag,
                    7+decimalDigitsForWaysInSet, "");
                for (int index = 0; index < cache.blockSize; ++index) {
                    printf(" 0x%08X", cache.blocks[blockIdx].data[index]);
                }
                printf(" }\n");
            }
            else {
                printf("\t\t[ %0*i ] : (V:F)\n\t\t%*s{  }\n", decimalDigitsForWaysInSet, block, 7+decimalDigitsForWaysInSet, "");
            }
        }
    }
    printf("end cache\n");
}


/*
  _    _ ______ _      _____  ______ _____   _____ 
 | |  | |  ____| |    |  __ \|  ____|  __ \ / ____|
 | |__| | |__  | |    | |__) | |__  | |__) | (___  
 |  __  |  __| | |    |  ___/|  __| |  _  / \___ \ 
 | |  | | |____| |____| |    | |____| | \ \ ____) |
 |_|  |_|______|______|_|    |______|_|  \_\_____/ 

*/


int create_mask(int bits){
    return ((1 << bits) - 1);
}

int extract_bits(int original, int shift_num, int bits){
    return ((original >> shift_num) & create_mask(bits));
}

void reset_cache(){
    
    for(int block = 0; block < MAX_CACHE_SIZE; block++){
        for(int block_data = 0; block_data < MAX_BLOCK_SIZE; block_data++){
            cache.blocks[block].data[block_data] = 0;
        }
        cache.blocks[block].dirty = 0;
        cache.blocks[block].lruLabel = 0;
        cache.blocks[block].valid = 0;
        cache.blocks[block].tag = UNINITIALIZED_TAG;
    }
}

decoded_address decode(int addr){
    decoded_address addy;
    addy.block_bits = log2(cache.blockSize); // Take the log2 of the blockSize to calculate how many bits are needed to represent offset
    addy.index_bits = log2(cache.numSets); // log2 of the number of sets to determine the # of bits for index
    addy.block_offset = extract_bits(addr, 0, addy.block_bits);
    addy.set_index = extract_bits(addr, addy.block_bits, addy.index_bits);
    addy.tag = (addr >> (addy.block_bits + addy.index_bits));
    addy.base = addy.set_index * cache.blocksPerSet;

    return addy;
}

int block_index(decoded_address* addy){
    // Find the block with tag <tag>
    for(int block = 0; block < cache.blocksPerSet; block++){
        // Lets find the block if it exists, loop through blocks 0-blocksPerSet
        blockStruct check_block = cache.blocks[addy->base + block];
        if(check_block.valid && check_block.tag == addy->tag){
            return (addy->base + block); // Index to the block (indexed off set_index)
        }
    }

    return -1; // Not found
}

int find_first_invalid(decoded_address* addy){
    for(int block = 0; block < cache.blocksPerSet; block++){
        if(!cache.blocks[addy->base + block].valid){
            return block;
        }
    }

    return -1;
}

int find_highest_LRU(decoded_address* addy){
    int max = cache.blocks[addy->base].lruLabel, index = 0;
    for(int block = 1; block < cache.blocksPerSet; block++){
        if(cache.blocks[addy->base + block].lruLabel > max){
            max = cache.blocks[addy->base + block].lruLabel;
            index = block;
        }
    }

    return index;
}

int find_block_to_replace(decoded_address* addy){
    // Loop through and find the offset of the next open block or the LRU
    int first_index = find_first_invalid(addy);
    if(first_index == -1){
        // Replace first_index with the index of the highest LRU block
        first_index = find_highest_LRU(addy);
    }
    
    return (addy->base + first_index);
}

void update_LRUs(decoded_address* addy, int read_block){
    // Increment ALL blocks' LRU labels (including read block)
    for(int block = 0; block < cache.blocksPerSet; block++){
        if(cache.blocks[addy->base + block].valid){
            cache.blocks[addy->base + block].lruLabel++;
        }
    }

    // THEN reset the read block's LRU label
    cache.blocks[read_block].lruLabel = 0;
}

void evict(int evicted_addr, int open_block){
    if(!cache.blocks[open_block].valid) return;
    printAction(evicted_addr, cache.blockSize, cache.blocks[open_block].dirty ? cacheToMemory : cacheToNowhere);
    if (cache.blocks[open_block].dirty) {
        for (int block = 0; block < cache.blockSize; block++) {
            mem_access(evicted_addr + block, 1, cache.blocks[open_block].data[block]);
        }
    }
}

void touch_block(blockStruct* block, int tag){
    // Update a block (set valid to 1, dirty to 0, and tag to tag)
    block->dirty = 0;
    block->valid = 1;
    block->tag = tag;
}
