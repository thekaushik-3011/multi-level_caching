#include <iostream>
#include <vector>
#include <queue>
#include <cstdint>
#include <algorithm>

using namespace std;

// Optimized Cache Block with bitfields for reduced memory usage
struct CacheBlock {
    bool valid : 1;
    bool dirty : 1;
    uint32_t tag : 30;  // Assuming 32-bit addresses
    int data;           // Placeholder for actual data
};

// Cache class with Pseudo-LRU, Victim Cache, Prefetching, and Write-Back
class Cache {
    int cacheSize, blockSize, numSets, ways;
    vector<vector<CacheBlock>> sets;
    vector<vector<uint8_t>> pseudoLRU;  // Pseudo-LRU replacement policy tracking
    queue<CacheBlock> victimCache;      // Victim cache for recently evicted blocks
    int victimCacheSize;
    const uint32_t setMask, tagMask;    // Masks for extracting index and tag from address

public:
    Cache(int size, int block, int assoc, int victimSize)
        : cacheSize(size), blockSize(block), ways(assoc), victimCacheSize(victimSize),
          numSets((size / block) / assoc),
          setMask((1 << __builtin_ctz(numSets)) - 1),
          tagMask(~((1 << (__builtin_ctz(numSets) + __builtin_ctz(block))) - 1)) {

        sets.resize(numSets, vector<CacheBlock>(ways));
        pseudoLRU.resize(numSets, vector<uint8_t>(ways, 0));  // Initialize Pseudo-LRU state

        // Initialize cache blocks
        for (int i = 0; i < numSets; i++) {
            for (int j = 0; j < ways; j++) {
                sets[i][j].valid = false;
                sets[i][j].dirty = false;
            }
        }
    }

    // Cache access method (read/write)
    bool accessCache(uint32_t address, bool isWrite = false) {
        uint32_t index = (address >> __builtin_ctz(blockSize)) & setMask;
        uint32_t tag = address & tagMask;

        int predictedWay = predictWay(index, tag);

        // Cache hit
        if (sets[index][predictedWay].valid && sets[index][predictedWay].tag == tag) {
            cout << "Cache hit at set " << index << ", way " << predictedWay << "\n";
            if (isWrite) sets[index][predictedWay].dirty = true;
            updatePseudoLRU(index, predictedWay);  // Update replacement policy
            prefetchNextLine(address);             // Prefetch next block
            return true;
        }

        // Check victim cache before declaring a miss
        if (checkVictimCache(tag)) {
            insertBlockToCache(index, victimCache.front(), isWrite);
            victimCache.pop();
            return true;
        }

        // Cache miss
        cout << "Cache miss, loading data into cache\n";
        int replaceWay = getPseudoLRU(index);

        // Write back dirty block if necessary
        if (sets[index][replaceWay].valid && sets[index][replaceWay].dirty) {
            cout << "Writing back dirty block\n";
            writeBack(sets[index][replaceWay]);
        }

        // Evict block if valid and add to victim cache
        if (sets[index][replaceWay].valid) {
            addToVictimCache(sets[index][replaceWay]);
        }

        // Load new block into cache
        sets[index][replaceWay].valid = true;
        sets[index][replaceWay].tag = tag;
        sets[index][replaceWay].dirty = isWrite;
        updatePseudoLRU(index, replaceWay);  // Update replacement policy
        prefetchNextLine(address);           // Prefetch next block
        return false;
    }

    // Prefetch the next cache line
    void prefetchNextLine(uint32_t address) {
        uint32_t nextAddress = address + blockSize;

        // Check if the next line is already in the cache
        uint32_t index = (nextAddress >> __builtin_ctz(blockSize)) & setMask;
        uint32_t tag = nextAddress & tagMask;

        bool isAlreadyCached = false;
        for (int i = 0; i < ways; i++) {
            if (sets[index][i].valid && sets[index][i].tag == tag) {
                isAlreadyCached = true;
                break;
            }
        }

        if (!isAlreadyCached) {
            cout << "Prefetching next line: " << nextAddress << "\n";
            accessCache(nextAddress);
        }
    }


    // Insert block into cache
    void insertBlockToCache(uint32_t index, CacheBlock block, bool isWrite) {
        int replaceWay = getPseudoLRU(index);
        sets[index][replaceWay] = block;
        sets[index][replaceWay].dirty = isWrite;
        updatePseudoLRU(index, replaceWay);
    }

    // Check if block exists in victim cache
    bool checkVictimCache(uint32_t tag) {
        int currentVictimSize = victimCache.size();
        for (int i = 0; i < currentVictimSize; ++i) {
            CacheBlock victimBlock = victimCache.front();
            victimCache.pop();
            if (victimBlock.tag == tag) {
                cout << "Cache hit in victim cache\n";
                victimCache.push(victimBlock);  // Return block to victim cache if not hit
                return true;
            }
            victimCache.push(victimBlock);  // Rotate block back to victim cache
        }
        return false;
    }

    // Add evicted block to victim cache
    void addToVictimCache(CacheBlock block) {
        if (victimCache.size() >= victimCacheSize) {
            victimCache.pop();  // Remove oldest block if victim cache is full
        }
        victimCache.push(block);
    }

    // Write back block to memory (for write-back policy)
    void writeBack(CacheBlock &block) {
        cout << "Writing back block with tag " << block.tag << " to memory\n";
        block.dirty = false;
    }

    // Get least recently used way based on Pseudo-LRU policy
    int getPseudoLRU(uint32_t setIndex) {
        auto it = find(pseudoLRU[setIndex].begin(), pseudoLRU[setIndex].end(), 0);
        return it - pseudoLRU[setIndex].begin();
    }

    // Update Pseudo-LRU when a block is accessed
    void updatePseudoLRU(uint32_t setIndex, int usedWay) {
        for (int i = 0; i < ways; ++i) {
            pseudoLRU[setIndex][i] = (i != usedWay) ? min(255, pseudoLRU[setIndex][i] + 1) : 0;
        }
    }

    // Predict which way might contain the desired block
    int predictWay(uint32_t setIndex, uint32_t tag) {
        for (int i = 0; i < ways; i++) {
            if (sets[setIndex][i].valid && sets[setIndex][i].tag == tag) {
                return i;
            }
        }
        return 0;  // Default to first way if prediction fails
    }
};

// Multi-Level Cache System (L1 and L2 caches)
class MultiLevelCache {
    Cache l1Cache;
    Cache l2Cache;

public:
    MultiLevelCache() : l1Cache(32 * 1024, 64, 8, 8), l2Cache(256 * 1024, 64, 8, 16) {}

    bool accessMemory(uint32_t address, bool isWrite = false) {
        cout << "Accessing L1 Cache\n";
        if (l1Cache.accessCache(address, isWrite)) {
            return true;  // L1 hit
        }

        cout << "Accessing L2 Cache\n";
        if (l2Cache.accessCache(address, isWrite)) {
            // On L2 hit, load the block into L1
            l1Cache.accessCache(address, isWrite);
            return true;
        }

        cout << "Cache miss in both L1 and L2\n";
        return false;
    }
};

// Main function to simulate memory access pattern
int main() {
    MultiLevelCache multiLevelCache;

    // Simulated memory access pattern (address, isWrite)
    vector<pair<uint32_t, bool>> accessPattern = {
        {100, false},   // L1 miss, L2 miss
        {120, false},   // L1 miss, L2 hit, load to L1
        {100, false},   // L1 hit
        {200, true},    // Write operation
        {100, false}    // L1 hit
    };

    // Loop through access pattern
    for (const auto& access : accessPattern) {
        uint32_t address = access.first;
        bool isWrite = access.second;

        cout << "\n" << string(40, '-') << "\n";
        cout << (isWrite ? "Write" : "Read") << " access to address " << address << ":\n";
        bool hit = multiLevelCache.accessMemory(address, isWrite);
        cout << "Result: " << (hit ? "Hit" : "Miss") << "\n";
    }

    return 0;
}
