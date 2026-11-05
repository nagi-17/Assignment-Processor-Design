// memory_system.hpp
// Data memory part of the project: L1 cache (tag array + data array), L2, L3, L4 and main memory.
// Used by both the 4 stage and the 6 stage pipeline.
//
// Everything is stored in global arrays and used through normal functions (no classes).
//
// How it works:
//  - L1 is checked first (the tag array tells us hit or miss)
//  - on a miss we go down L2 -> L3 -> L4 -> main memory
//  - the block comes back up and gets saved in every level on the way, so L1 ends up with it
//  - stores are write-back + write-allocate (the dirty bit is set in L1)
//  - hazard control is not done here, we only give the hit signal and the number of stall cycles
//
// Functions the pipeline stages use :
//   resetMemorySystem()  - clears all caches and memory (call once at the start)
//   memProbe(...)        - MA1 work: MAR + tag check -> hit signal
//   memAccess(...)       - MA2 work: data array / lower memory, gives the load value + stall cycles

#ifndef MEMORY_SYSTEM_HPP
#define MEMORY_SYSTEM_HPP

#include <cstdio>
#include <cstdint>
#include <cstring>
#include <cassert>
#include <map>

// ---------------- settings (change these to try different cache sizes) ----------------
const int BLOCK_SIZE = 16;     // bytes in one cache block
const int WORD_SIZE = 4;       // bytes in one word

const int L1_SIZE = 1024;      // bytes
const int L1_ASSOC = 2;        // ways
const int L1_SETS = L1_SIZE / (L1_ASSOC * BLOCK_SIZE);

const int NUM_LOWER = 3;       // L2, L3, L4
const char LOWER_NAME[NUM_LOWER][4] = {"L2", "L3", "L4"};
const int LOWER_SIZE[NUM_LOWER] = {4096, 16384, 65536};
const int LOWER_ASSOC[NUM_LOWER] = {4, 8, 8};
const int LOWER_LATENCY[NUM_LOWER] = {4, 12, 30};   // cycles
const int MEM_LATENCY = 100;                        // cycles

// array sizes for the lower caches (must be >= the real number of sets / ways)
const int MAX_LOW_SETS = 512;
const int MAX_LOW_WAYS = 8;

static bool showTrace = true;  // set to false if you dont want the hit/miss messages

// ---------------- data ----------------

struct Block
{
    uint8_t b[BLOCK_SIZE];
};

// L1
static bool l1Valid[L1_SETS][L1_ASSOC];
static bool l1Dirty[L1_SETS][L1_ASSOC];
static uint32_t l1Tag[L1_SETS][L1_ASSOC];
static long long l1Used[L1_SETS][L1_ASSOC];   // last used time, for LRU
static Block l1Data[L1_SETS][L1_ASSOC];
static int l1Hits, l1Misses, l1Writebacks;

static uint32_t marReg;   // memory address register
static uint32_t mdrReg;   // memory data register

// lower caches: first index is the level (0 = L2, 1 = L3, 2 = L4)

static bool lowValid[NUM_LOWER][MAX_LOW_SETS][MAX_LOW_WAYS];
static bool lowDirty[NUM_LOWER][MAX_LOW_SETS][MAX_LOW_WAYS];
static uint32_t lowTag[NUM_LOWER][MAX_LOW_SETS][MAX_LOW_WAYS];
static long long lowUsed[NUM_LOWER][MAX_LOW_SETS][MAX_LOW_WAYS];
static Block lowData[NUM_LOWER][MAX_LOW_SETS][MAX_LOW_WAYS];
static int lowHits[NUM_LOWER], lowMisses[NUM_LOWER], lowWritebacks[NUM_LOWER];

// main memory: only the blocks that were written are stored, everything else reads as 0
static std::map<uint32_t, Block> mainMem;
static int memReads, memWrites;

static long long useClock;   // goes up on every access, used for LRU

// ---------------- small helpers ----------------

// get a 4 byte word out of a block (little endian)
inline uint32_t getWord(const Block &blk, int offset)
{
    uint32_t value = 0;
    for (int i = 0; i < WORD_SIZE; i++)
        value = value | ((uint32_t)blk.b[offset + i] << (8 * i));
    return value;
}

// put a 4 byte word into a block (little endian)
inline void setWord(Block &blk, int offset, uint32_t value)
{
    for (int i = 0; i < WORD_SIZE; i++)
        blk.b[offset + i] = (value >> (8 * i)) & 0xFF;
}

// name of a lower level, only used for the messages
inline const char *levelName(int level)
{
    if (level == NUM_LOWER)
        return "MEM";
    return LOWER_NAME[level];
}

inline void resetMemorySystem()
{
    memset(l1Valid, 0, sizeof(l1Valid));
    memset(l1Dirty, 0, sizeof(l1Dirty));
    memset(l1Tag, 0, sizeof(l1Tag));
    memset(l1Used, 0, sizeof(l1Used));
    memset(l1Data, 0, sizeof(l1Data));
    l1Hits = l1Misses = l1Writebacks = 0;
    marReg = 0;
    mdrReg = 0;

    memset(lowValid, 0, sizeof(lowValid));
    memset(lowDirty, 0, sizeof(lowDirty));
    memset(lowTag, 0, sizeof(lowTag));
    memset(lowUsed, 0, sizeof(lowUsed));
    memset(lowData, 0, sizeof(lowData));
    memset(lowHits, 0, sizeof(lowHits));
    memset(lowMisses, 0, sizeof(lowMisses));
    memset(lowWritebacks, 0, sizeof(lowWritebacks));

    mainMem.clear();
    memReads = memWrites = 0;
    useClock = 0;

    // check that the settings fit in the arrays
    for (int i = 0; i < NUM_LOWER; i++)
    {
        assert(LOWER_ASSOC[i] <= MAX_LOW_WAYS);
        assert(LOWER_SIZE[i] / (LOWER_ASSOC[i] * BLOCK_SIZE) <= MAX_LOW_SETS);
    }
}

// =====================================================================
//  MAIN MEMORY
// =====================================================================

// returns the cycles taken
inline int memReadBlock(uint32_t blockAddr, Block &out)
{
    memReads++;
    if (showTrace)
        printf("  MEM: read block 0x%x (%d cyc)\n", blockAddr, MEM_LATENCY);

    std::map<uint32_t, Block>::iterator it = mainMem.find(blockAddr);
    if (it == mainMem.end())
        memset(&out, 0, sizeof(out));
    else
        out = it->second;
    return MEM_LATENCY;
}

inline void memWriteBlock(uint32_t blockAddr, const Block &data)
{
    memWrites++;
    if (showTrace)
        printf("  MEM: writeback block 0x%x\n", blockAddr);
    mainMem[blockAddr] = data;
}

// =====================================================================
//  LOWER MEMORY SUBSYSTEM  (L2 = level 0, L3 = level 1, L4 = level 2, then main memory)
// =====================================================================

inline int lowSets(int level)
{
    return LOWER_SIZE[level] / (LOWER_ASSOC[level] * BLOCK_SIZE);
}

inline int lowSetOf(int level, uint32_t blockAddr)
{
    return (blockAddr / BLOCK_SIZE) % lowSets(level);
}

inline uint32_t lowTagOf(int level, uint32_t blockAddr)
{
    return (blockAddr / BLOCK_SIZE) / lowSets(level);
}

// way where the block is, -1 if it is not in this level
inline int lowFindWay(int level, uint32_t blockAddr)
{
    int set = lowSetOf(level, blockAddr);
    uint32_t tag = lowTagOf(level, blockAddr);
    for (int w = 0; w < LOWER_ASSOC[level]; w++)
    {
        if (lowValid[level][set][w] && lowTag[level][set][w] == tag)
            return w;
    }
    return -1;
}
 
// empty way if there is one, otherwise the least recently used way
inline int lowChooseVictim(int level, int set)
{
    for (int w = 0; w < LOWER_ASSOC[level]; w++)
    {
        if (!lowValid[level][set][w])
            return w;
    }
    int victim = 0;
    for (int w = 1; w < LOWER_ASSOC[level]; w++)
    {
        if (lowUsed[level][set][w] < lowUsed[level][set][victim])
            victim = w;
    }
    return victim;
}

// address of the block that is stored in (set, way)
inline uint32_t lowBlockAddrOf(int level, int set, int way)
{
    return (lowTag[level][set][way] * lowSets(level) + set) * BLOCK_SIZE;
}

// lowInstall and lowWrite call each other, so one of them is declared first
inline void lowWrite(int level, uint32_t blockAddr, const Block &data);

// put a block in this level. If the victim is dirty it is written to the next level first.
// returns the way where the block was placed
inline int lowInstall(int level, uint32_t blockAddr, const Block &data)
{
    int set = lowSetOf(level, blockAddr);
    int way = lowChooseVictim(level, set);

    if (lowValid[level][set][way])
    {
        if (lowDirty[level][set][way])
        {
            uint32_t victimAddr = lowBlockAddrOf(level, set, way);
            lowWritebacks[level]++;
            if (showTrace)
                printf("  %s: evict DIRTY block 0x%x -> %s\n", levelName(level), victimAddr, levelName(level + 1));
            lowWrite(level + 1, victimAddr, lowData[level][set][way]);
        }
        else
        {
            if (showTrace)
                printf("  %s: evict clean block (set %d)\n", levelName(level), set);
        }
    }

    lowValid[level][set][way] = true;
    lowDirty[level][set][way] = false;
    lowTag[level][set][way] = lowTagOf(level, blockAddr);
    lowData[level][set][way] = data;
    useClock++;
    lowUsed[level][set][way] = useClock;
    return way;
}

// a dirty block coming from the level above is written here
inline void lowWrite(int level, uint32_t blockAddr, const Block &data)
{
    if (level == NUM_LOWER)
    {
        memWriteBlock(blockAddr, data);
        return;
    }

    int set = lowSetOf(level, blockAddr);
    int way = lowFindWay(level, blockAddr);

    if (way == -1)
        way = lowInstall(level, blockAddr, data);   // whole block is overwritten so no need to fetch it
    else
    {
        lowData[level][set][way] = data;
        useClock++;
        lowUsed[level][set][way] = useClock;
    }
    lowDirty[level][set][way] = true;
}

// read a block starting from the given level. Returns the cycles taken (this level + the ones below)
inline int lowRead(int level, uint32_t blockAddr, Block &out)
{
    if (level == NUM_LOWER)
        return memReadBlock(blockAddr, out);

    int set = lowSetOf(level, blockAddr);
    int way = lowFindWay(level, blockAddr);

    if (way != -1)   // hit
    {
        lowHits[level]++;
        useClock++;
        lowUsed[level][set][way] = useClock;
        out = lowData[level][set][way];
        if (showTrace)
            printf("  %s: HIT  block 0x%x (%d cyc)\n", levelName(level), blockAddr, LOWER_LATENCY[level]);
        return LOWER_LATENCY[level];
    }

    // miss: ask the next level first, then save the block here on the way back up
    lowMisses[level]++;
    if (showTrace)
        printf("  %s: MISS block 0x%x (%d cyc tag check)\n", levelName(level), blockAddr, LOWER_LATENCY[level]);
    int below = lowRead(level + 1, blockAddr, out);
    lowInstall(level, blockAddr, out);
    if (showTrace)
        printf("  %s: filled block 0x%x\n", levelName(level), blockAddr);
    return LOWER_LATENCY[level] + below;
}

// =====================================================================
//  L1 : tag subsystem, hit signal generator, data array
// =====================================================================

inline int l1SetOf(uint32_t addr)
{
    return (addr / BLOCK_SIZE) % L1_SETS;
}

inline uint32_t l1TagOf(uint32_t addr)
{
    return (addr / BLOCK_SIZE) / L1_SETS;
}

inline int l1OffsetOf(uint32_t addr)
{
    return addr % BLOCK_SIZE;
}

inline uint32_t l1BlockAddr(uint32_t addr)
{
    return addr - (addr % BLOCK_SIZE);
}

// L1 tag subsystem: compare the tag with every way of the set (nothing is changed)
// wayMatch[w] becomes true if way w is valid and has the same tag
inline void l1LookupTags(uint32_t addr, bool wayMatch[])
{
    int set = l1SetOf(addr);
    uint32_t tag = l1TagOf(addr);
    for (int w = 0; w < L1_ASSOC; w++)
        wayMatch[w] = (l1Valid[set][w] && l1Tag[set][w] == tag);
}

// hit signal generator: hit if any way matched
inline bool generateHitSignal(const bool wayMatch[])
{
    for (int w = 0; w < L1_ASSOC; w++)
    {
        if (wayMatch[w])
            return true;
    }
    return false;
}

// which way matched (-1 if none)
inline int matchedWay(const bool wayMatch[])
{
    for (int w = 0; w < L1_ASSOC; w++)
    {
        if (wayMatch[w])
            return w;
    }
    return -1;
}

// empty way if there is one, otherwise the least recently used way
inline int l1ChooseVictim(int set)
{
    for (int w = 0; w < L1_ASSOC; w++)
    {
        if (!l1Valid[set][w])
            return w;
    }
    int victim = 0;
    for (int w = 1; w < L1_ASSOC; w++)
    {
        if (l1Used[set][w] < l1Used[set][victim])
            victim = w;
    }
    return victim;
}

// "fill missing line": choose a victim, save it if it is dirty, get the block from the lower
// memory and put it in L1. way = where it was placed, blk = the fetched block.
// returns the cycles the lower memory took
inline int l1Refill(uint32_t addr, int &way, Block &blk)
{
    int set = l1SetOf(addr);
    way = l1ChooseVictim(set);

    if (l1Valid[set][way])
    {
        if (l1Dirty[set][way])
        {
            uint32_t victimAddr = (l1Tag[set][way] * L1_SETS + set) * BLOCK_SIZE;
            l1Writebacks++;
            if (showTrace)
                printf("  L1: evict DIRTY block 0x%x -> L2\n", victimAddr);
            lowWrite(0, victimAddr, l1Data[set][way]);
        }
        else
        {
            if (showTrace)
                printf("  L1: evict clean block (set %d)\n", set);
        }
    }

    int cycles = lowRead(0, l1BlockAddr(addr), blk);

    l1Data[set][way] = blk;
    l1Valid[set][way] = true;
    l1Dirty[set][way] = false;
    l1Tag[set][way] = l1TagOf(addr);
    if (showTrace)
        printf("  L1: filled block 0x%x\n", l1BlockAddr(addr));
    return cycles;
}

// =====================================================================
//  What the pipeline stages call
// =====================================================================

// MA1 work: put the address in MAR, compare the tags, make the hit signal.
// Nothing in the cache is changed here.
// Instructions that are not load/store dont use the cache, so we just return true (no hazard).
inline bool memProbe(uint32_t addr, bool is_mem_op)
{
    if (!is_mem_op)
        return true;

    marReg = addr;
    bool wayMatch[L1_ASSOC];
    l1LookupTags(marReg, wayMatch);
    return generateHitSignal(wayMatch);
}

// MA2 work: read/write the L1 data array and fill the MDR. On a miss go to the lower memory.
// m1_hit is the hit signal that memProbe() gave for this same instruction.
// stall_cycles = extra cycles the pipeline has to wait (0 on a hit)
// returns the MDR value (this is the load result for a load)
inline uint32_t memAccess(uint32_t addr, bool is_ld, bool is_st, uint32_t st_data, bool m1_hit, int &stall_cycles)
{
    assert(addr % WORD_SIZE == 0);
    stall_cycles = 0;

    if (showTrace)
        printf("%s 0x%x\n", is_ld ? "LOAD " : "STORE", addr);

    int set = l1SetOf(addr);
    int offset = l1OffsetOf(addr);
    int way = 0;

    if (m1_hit)
    {
        l1Hits++;
        bool wayMatch[L1_ASSOC];
        l1LookupTags(addr, wayMatch);
        way = matchedWay(wayMatch);
        assert(way != -1);   // if this fails the hit signal is old, run MA1 again after a stall
        if (showTrace)
            printf("  L1: HIT  addr 0x%x (set %d, way %d)\n", addr, set, way);
        if (is_ld)
            mdrReg = getWord(l1Data[set][way], offset);
    }
    else
    {
        l1Misses++;
        if (showTrace)
            printf("  L1: MISS addr 0x%x (set %d)\n", addr, set);
        Block blk;
        stall_cycles = l1Refill(addr, way, blk);   // fill missing line
        if (is_ld)
            mdrReg = getWord(blk, offset);         // direct load fill
    }

    if (is_st)   // write-allocate, so the line is already in L1 at this point
    {
        setWord(l1Data[set][way], offset, st_data);
        l1Dirty[set][way] = true;
    }

    useClock++;
    l1Used[set][way] = useClock;
    return mdrReg;
}

inline void printMemStats()
{
    printf("L1: hits=%d misses=%d writebacks=%d\n", l1Hits, l1Misses, l1Writebacks);
    for (int i = 0; i < NUM_LOWER; i++)
        printf("%s: hits=%d misses=%d writebacks=%d\n", LOWER_NAME[i], lowHits[i], lowMisses[i], lowWritebacks[i]);
    printf("MEM: reads=%d writes=%d\n", memReads, memWrites);
}

#endif