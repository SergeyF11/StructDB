#ifndef STRUCT_INDEX_H
#define STRUCT_INDEX_H

#include "StructBuilder.h"
#include <map>
#include <functional>

template<typename KeyType, typename StructType>
class StructIndex {
private:
    std::map<KeyType, uint32_t> indexMap;  // key -> file offset
    bool isBuilt;
    
    // Function to extract key from struct
    std::function<KeyType(const StructType&)> keyExtractor;
    
public:
    StructIndex(std::function<KeyType(const StructType&)> extractor) 
        : keyExtractor(extractor), isBuilt(false) {}
    
    void buildIndex(File& dataFile) {
        indexMap.clear();
        
        if (!dataFile) return;
        
        // Start from beginning of data (after header if any)
        dataFile.seek(0);
        
        uint32_t currentOffset = dataFile.position();
        StructType record;
        
        while (dataFile.available()) {
            uint32_t recordStart = dataFile.position();
            
            if (record.readFrom(dataFile)) {
                KeyType key = keyExtractor(record);
                indexMap[key] = recordStart;
            } else {
                break;
            }
            
            currentOffset = dataFile.position();
        }
        
        isBuilt = true;
    }
    
    size_t find(const KeyType& key) {
        auto it = indexMap.find(key);
        if (it != indexMap.end()) {
            return it->second;  // Returns file offset
        }
        return SIZE_MAX;  // Not found
    }
    
    std::vector<size_t> findRange(const KeyType& minKey, const KeyType& maxKey) {
        std::vector<size_t> results;
        
        auto it = indexMap.lower_bound(minKey);
        auto end = indexMap.upper_bound(maxKey);
        
        for (; it != end; ++it) {
            results.push_back(it->second);
        }
        
        return results;
    }
    
    size_t size() const {
        return indexMap.size();
    }
    
    bool isIndexBuilt() const {
        return isBuilt;
    }
    
    void clear() {
        indexMap.clear();
        isBuilt = false;
    }
    
    // Multi-index support
    template<typename SecondaryKey>
    class SecondaryIndex {
    private:
        std::multimap<SecondaryKey, uint32_t> indexMap;
        std::function<SecondaryKey(const StructType&)> keyExtractor;
        
    public:
        SecondaryIndex(std::function<SecondaryKey(const StructType&)> extractor)
            : keyExtractor(extractor) {}
            
        void addRecord(const StructType& record, uint32_t offset) {
            SecondaryKey key = keyExtractor(record);
            indexMap.insert({key, offset});
        }
        
        std::vector<uint32_t> findAll(const SecondaryKey& key) {
            std::vector<uint32_t> results;
            auto range = indexMap.equal_range(key);
            
            for (auto it = range.first; it != range.second; ++it) {
                results.push_back(it->second);
            }
            
            return results;
        }
    };
};

// Helper for common key types
template<typename StructType>
class StringIndex : public StructIndex<String, StructType> {
public:
    StringIndex(std::function<String(const StructType&)> extractor)
        : StructIndex<String, StructType>(extractor) {}
};

template<typename StructType>
class IntIndex : public StructIndex<int32_t, StructType> {
public:
    IntIndex(std::function<int32_t(const StructType&)> extractor)
        : StructIndex<int32_t, StructType>(extractor) {}
};

#endif // STRUCT_INDEX_H