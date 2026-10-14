// test_memory_system.cpp
// Tests for memory_system.hpp only (no pipeline files are needed).
//
// compile:  g++ -std=c++11 -Wall test_memory_system.cpp -o test_memory
// run:      ./test_memory
//
// The addresses below are made for the default settings at the top of memory_system.hpp
// (16 byte blocks, L1 = 1KB 2-way). The cycle numbers are calculated from the latency constants,
// so changing the latencies is fine, but changing sizes/ways may need different addresses.

#include <cstdio>
#include <cstdlib>
#include "memory_system.hpp"

int passed = 0;
int failed = 0;

void check(const char *name, bool ok)
{
    if (ok)
    {
        passed++;
        printf("PASS  %s\n", name);
    }
    else
    {
        failed++;
        printf("FAIL  %s\n", name);
    }
}

void checkEq(const char *name, long long got, long long expected)
{
    if (got == expected)
    {
        passed++;
        printf("PASS  %s\n", name);
    }
    else
    {
        failed++;
        printf("FAIL  %s (got %lld, expected %lld)\n", name, got, expected);
    }
}

// ---------- helpers: a full load / store (MA1 part + MA2 part) ----------

uint32_t doLoad(uint32_t addr, bool &hit, int &stall)
{
    hit = memProbe(addr, true);
    return memAccess(addr, true, false, 0, hit, stall);
}

void doStore(uint32_t addr, uint32_t value, bool &hit, int &stall)
{
    hit = memProbe(addr, true);
    memAccess(addr, false, true, value, hit, stall);
}

// cycles needed when the block is found in lower level "level" (0 = L2, 1 = L3, 2 = L4, 3 = main memory)
int stallWhenFoundIn(int level)
{
    int total = 0;
    for (int i = 0; i <= level && i < NUM_LOWER; i++)
        total = total + LOWER_LATENCY[i];
    if (level == NUM_LOWER)
        total = total + MEM_LATENCY;
    return total;
}

// two addresses with this distance between them are in the same set of that cache
uint32_t L1_STRIDE = L1_SETS * BLOCK_SIZE;                                          // 0x200
uint32_t L2_STRIDE = (LOWER_SIZE[0] / (LOWER_ASSOC[0] * BLOCK_SIZE)) * BLOCK_SIZE;  // 0x400
uint32_t L3_STRIDE = (LOWER_SIZE[1] / (LOWER_ASSOC[1] * BLOCK_SIZE)) * BLOCK_SIZE;  // 0x800
uint32_t L4_STRIDE = (LOWER_SIZE[2] / (LOWER_ASSOC[2] * BLOCK_SIZE)) * BLOCK_SIZE;  // 0x2000

// ---------- test cases ----------

void testColdLoad()
{
    printf("\n-- cold load\n");
    resetMemorySystem();
    bool hit;
    int stall;
    uint32_t v = doLoad(0x100, hit, stall);
    checkEq("untouched memory reads as 0", v, 0);
    check("first access is an L1 miss", hit == false);
    checkEq("miss goes all the way down (L2+L3+L4+MEM)", stall, stallWhenFoundIn(NUM_LOWER));
    checkEq("L1 miss counter", l1Misses, 1);
    checkEq("memory was read once", memReads, 1);
}

void testStoreThenLoad()
{
    printf("\n-- store then load\n");
    resetMemorySystem();
    bool hit;
    int stall;
    doStore(0x40, 0xCAFEBABE, hit, stall);
    check("store to a new address is a miss", hit == false);
    checkEq("store miss costs the full walk", stall, stallWhenFoundIn(NUM_LOWER));

    uint32_t v = doLoad(0x40, hit, stall);
    checkEq("load gives back the stored value", v, 0xCAFEBABE);
    check("load after store is an L1 hit", hit == true);
    checkEq("L1 hit has no stall", stall, 0);
}

void testSameBlock()
{
    printf("\n-- words in the same block\n");
    resetMemorySystem();
    bool hit;
    int stall;
    doStore(0x20, 0x11111111, hit, stall);

    // 0x24, 0x28, 0x2C are in the same 16 byte block as 0x20
    uint32_t v = doLoad(0x24, hit, stall);
    check("neighbour word in same block is a hit (spatial locality)", hit == true);
    checkEq("neighbour word is still 0", v, 0);

    doStore(0x28, 0x22222222, hit, stall);
    check("store to the same block is a hit", hit == true);

    checkEq("word 0x20 not changed by store to 0x28", doLoad(0x20, hit, stall), 0x11111111);
    checkEq("word 0x28 has its own value", doLoad(0x28, hit, stall), 0x22222222);
    checkEq("word 0x2C is still 0", doLoad(0x2C, hit, stall), 0);
}

void testWriteAllocate()
{
    printf("\n-- write allocate\n");
    resetMemorySystem();
    bool hit;
    int stall;
    doStore(0x300, 0xAAAA5555, hit, stall);
    check("store miss brings the block into L1", doLoad(0x300, hit, stall) == 0xAAAA5555 && hit);
    check("dirty bit is set after the store", l1Dirty[l1SetOf(0x300)][0] || l1Dirty[l1SetOf(0x300)][1]);
}

void testLRU()
{
    printf("\n-- LRU replacement in L1 (2 ways)\n");
    resetMemorySystem();
    bool hit;
    int stall;
    uint32_t A = 0;
    uint32_t B = L1_STRIDE;        // same L1 set as A
    uint32_t C = 2 * L1_STRIDE;    // same L1 set again

    doLoad(A, hit, stall);
    doLoad(B, hit, stall);
    doLoad(A, hit, stall);         // A is now more recent than B
    doLoad(C, hit, stall);         // set is full, B should be thrown out

    doLoad(A, hit, stall);
    check("A (recently used) is still in L1", hit == true);
    doLoad(B, hit, stall);
    check("B (least recently used) was evicted", hit == false);
}

void testDirtyEviction()
{
    printf("\n-- dirty eviction goes to L2\n");
    resetMemorySystem();
    bool hit;
    int stall;
    doStore(0, 0xDEADBEEF, hit, stall);
    doStore(L1_STRIDE, 0x11111111, hit, stall);
    doStore(2 * L1_STRIDE, 0x22222222, hit, stall);   // pushes block 0 out of L1

    checkEq("one dirty writeback from L1", l1Writebacks, 1);

    uint32_t v = doLoad(0, hit, stall);
    checkEq("value survived the eviction", v, 0xDEADBEEF);
    check("reload is an L1 miss", hit == false);
    checkEq("block is found in L2", stall, stallWhenFoundIn(0));
}

void testCleanEviction()
{
    printf("\n-- clean eviction\n");
    resetMemorySystem();
    bool hit;
    int stall;
    doLoad(0, hit, stall);
    doLoad(L1_STRIDE, hit, stall);
    doLoad(2 * L1_STRIDE, hit, stall);
    doLoad(3 * L1_STRIDE, hit, stall);
    checkEq("loads only, so no writebacks", l1Writebacks, 0);
}

void testL3Hit()
{
    printf("\n-- block found in L3\n");
    resetMemorySystem();
    bool hit;
    int stall;
    doLoad(0, hit, stall);
    // fill the L2 set of address 0 so that block 0 is pushed out of L2 (and L1) but stays in L3
    for (int k = 1; k <= LOWER_ASSOC[0]; k++)
        doLoad(k * L2_STRIDE, hit, stall);

    doLoad(0, hit, stall);
    check("L1 miss", hit == false);
    checkEq("found in L3 (L2 latency + L3 latency)", stall, stallWhenFoundIn(1));
}

void testL4Hit()
{
    printf("\n-- block found in L4\n");
    resetMemorySystem();
    bool hit;
    int stall;
    doLoad(0, hit, stall);
    // push block 0 out of L1, L2 and L3 but not out of L4
    for (int k = 1; k <= LOWER_ASSOC[1]; k++)
        doLoad(k * L3_STRIDE, hit, stall);

    doLoad(0, hit, stall);
    check("L1 miss", hit == false);
    checkEq("found in L4 (L2 + L3 + L4 latency)", stall, stallWhenFoundIn(2));
}

void testReachesMainMemory()
{
    printf("\n-- dirty data is pushed all the way down to main memory\n");
    resetMemorySystem();
    bool hit;
    int stall;
    // all these addresses fall in the same set of every cache level
    for (int k = 0; k < 40; k++)
        doStore(k * L4_STRIDE, 100 + k, hit, stall);

    check("main memory got at least one writeback", memWrites > 0);

    uint32_t v = doLoad(0, hit, stall);
    checkEq("block 0 still has the right value", v, 100);
    checkEq("it had to come from main memory", stall, stallWhenFoundIn(NUM_LOWER));
}

void testNonMemoryInstruction()
{
    printf("\n-- instructions that are not load/store\n");
    resetMemorySystem();
    int before = l1Hits + l1Misses;
    uint32_t marBefore = marReg;
    bool hit = memProbe(0x1234, false);
    check("hit signal is true (no hazard)", hit == true);
    checkEq("cache not touched", l1Hits + l1Misses, before);
    checkEq("MAR not changed", marReg, marBefore);
}

void testRandomAgainstReference()
{
    printf("\n-- random loads/stores compared with a simple array\n");
    resetMemorySystem();
    showTrace = false;

    static uint32_t ref[1 << 17];          // reference memory (512 KB worth of words)
    for (int i = 0; i < (1 << 17); i++)
        ref[i] = 0;

    srand(12345);
    int wrong = 0;
    int total = 50000;

    for (int i = 0; i < total; i++)
    {
        uint32_t addr;
        if (rand() % 2 == 0)
            addr = (rand() % (1 << 17)) * 4;           // anywhere in 512 KB
        else
            addr = (rand() % 64) * L4_STRIDE;          // many addresses that fight for the same set

        bool hit;
        int stall;
        if (rand() % 2 == 0)
        {
            uint32_t value = rand();
            doStore(addr, value, hit, stall);
            ref[addr / 4] = value;
        }
        else
        {
            uint32_t got = doLoad(addr, hit, stall);
            if (got != ref[addr / 4])
                wrong++;
        }
    }
    checkEq("every load returned the same value as the reference array", wrong, 0);
    showTrace = true;
}

int main()
{
    showTrace = false;   // change to true to see every hit/miss message

    testColdLoad();
    testStoreThenLoad();
    testSameBlock();
    testWriteAllocate();
    testLRU();
    testDirtyEviction();
    testCleanEviction();
    testL3Hit();
    testL4Hit();
    testReachesMainMemory();
    testNonMemoryInstruction();
    testRandomAgainstReference();

    printf("\n==== %d passed, %d failed ====\n", passed, failed);
    return failed;
}