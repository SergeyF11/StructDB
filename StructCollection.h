#ifndef STRUCT_COLLECTION_H
#define STRUCT_COLLECTION_H

#include "StructBuilder.h"
#include "LittleFS.h"

template<typename StructType>
class StructCollection {
private:
    struct Header {
        char signature[4];      // "SCDB"
        uint16_t version;
        uint16_t schemaVersion;
        uint32_t recordCount;
        uint32_t firstFreeOffset;
        uint32_t crc32;
    };
    
    File file;
    Header header;
    bool isOpen;
    String filename;
    
    bool readHeader() {
        if (!file.seek(0)) return false;
        return file.readBytes((uint8_t*)&header, sizeof(Header)) == sizeof(Header);
    }
    
    bool writeHeader() {
        if (!file.seek(0)) return false;
        return file.write((uint8_t*)&header, sizeof(Header)) == sizeof(Header);
    }
    
    uint32_t calculateCRC() {
        // Simple CRC calculation
        uint32_t crc = 0xFFFFFFFF;
        // Implementation...
        return crc ^ 0xFFFFFFFF;
    }
    
public:
    StructCollection() : isOpen(false) {
        memcpy(header.signature, "SCDB", 4);
        header.version = 1;
        header.recordCount = 0;
        header.firstFreeOffset = sizeof(Header);
    }
    
    bool open(const char* filename) {
        this->filename = filename;
        
        if (LittleFS.exists(filename)) {
            file = LittleFS.open(filename, "r+");
            if (!file) return false;
            
            if (!readHeader()) {
                file.close();
                return false;
            }
            
            // Verify signature
            if (memcmp(header.signature, "SCDB", 4) != 0) {
                file.close();
                return false;
            }
        } else {
            file = LittleFS.open(filename, "w+");
            if (!file) return false;
            
            header.recordCount = 0;
            header.firstFreeOffset = sizeof(Header);
            if (!writeHeader()) {
                file.close();
                return false;
            }
        }
        
        isOpen = true;
        return true;
    }
    
    void close() {
        if (isOpen) {
            header.crc32 = calculateCRC();
            writeHeader();
            file.close();
            isOpen = false;
        }
    }
    
    bool append(const StructType& record) {
        if (!isOpen) return false;
        
        // Go to end of file
        if (!file.seek(header.firstFreeOffset)) return false;
        
        // Write record
        if (!record.writeTo(file)) return false;
        
        // Update header
        header.recordCount++;
        header.firstFreeOffset = file.position();
        
        return writeHeader();
    }
    
    bool read(size_t index, StructType& record) {
        if (!isOpen || index >= header.recordCount) return false;
        
        // Find offset for this record
        // For simplicity, we'll seek sequentially (could be optimized with index)
        uint32_t offset = sizeof(Header);
        if (!file.seek(offset)) return false;
        
        for (size_t i = 0; i < index; i++) {
            // Skip this record
            TestStream temp;  // Need to implement skipping
            // Actually need to read the record to know its size
            // This needs proper implementation
            break;
        }
        
        return record.readFrom(file);
    }
    
    bool update(size_t index, const StructType& record) {
        if (!isOpen || index >= header.recordCount) return false;
        // Similar to read but need to handle variable size records
        // For now, mark old record as deleted and append new
        return false;  // Need proper implementation
    }
    
    bool remove(size_t index) {
        if (!isOpen || index >= header.recordCount) return false;
        // Mark record as deleted
        return false;  // Need proper implementation
    }
    
    size_t count() const {
        return isOpen ? header.recordCount : 0;
    }
    
    bool isValid() const {
        return isOpen;
    }
    
    // Iterator support
    class Iterator {
    private:
        StructCollection* collection;
        size_t currentIndex;
        File positionFile;
        
    public:
        Iterator(StructCollection* coll, size_t index) 
            : collection(coll), currentIndex(index) {
            if (collection && collection->isOpen) {
                positionFile = LittleFS.open(collection->filename.c_str(), "r");
                // Seek to start of records
                positionFile.seek(sizeof(Header));
            }
        }
        
        bool next(StructType& record) {
            if (!positionFile || currentIndex >= collection->count()) {
                return false;
            }
            
            // Read current record
            if (!record.readFrom(positionFile)) {
                return false;
            }
            
            currentIndex++;
            return true;
        }
        
        bool hasNext() const {
            return positionFile && currentIndex < collection->count();
        }
    };
    
    Iterator getIterator() {
        return Iterator(this, 0);
    }
};

#endif // STRUCT_COLLECTION_H