#ifndef DATA_TYPES_H
#define DATA_TYPES_H

#include <Arduino.h>
#include <type_traits>
#include <string.h>
#include <limits>

// Base data types enum
enum ENTRY_TYPE : uint8_t {
    NULL_T = 0,
    CHARS_MIN = 1,
    CHARS_MAX = 0xF0 - 1,
    UINT8_T = 0xF0,
    UINT16_T = 0xF1,
    UINT32_T = 0xF2,
    UINT64_T = 0xF3,
    INT8_T = 0xF4,
    INT16_T = 0xF5,
    INT32_T = 0xF6,
    INT64_T = 0xF7,
    BYTE_ARRAY_T = 0xF8,
    BYTE_ARRAY_BIG_T = 0xF9,
    FLOAT_T = 0xFA,
    DOUBLE_T = 0xFB,
    _RESERVED_T = 0xFF
};


// Interface for all data entry types
class IDataEntry {
public:
    static const uint8_t MAX_CHARS_LENGTH = uint8_t(CHARS_MAX);

    virtual ~IDataEntry() = default;
    
    virtual ENTRY_TYPE getType() const = 0;
    virtual size_t getDataSize() const = 0;
    virtual size_t getTotalSize() const = 0;
    
    virtual bool readFrom(Stream& src) = 0;
    virtual bool writeTo(Stream& dest) const = 0;
    
    virtual void clear() = 0;
    virtual String toString() const = 0;
    
    virtual bool setValue(int64_t value) = 0;
    virtual bool setValue(uint64_t value) = 0;
    virtual bool setValue(const String& value) = 0;
    virtual bool setValue(const char* value, uint8_t length = 0) = 0;
    virtual bool setValue(const uint8_t* data, uint16_t size) = 0;
    
    // Добавляем виртуальные методы для float/double
    virtual bool setValue(float value) { return false; }
    virtual bool setValue(double value) { return false; }
    
    virtual bool isZero() const = 0;
    virtual bool isFixedSize() const = 0;
    
    virtual bool canHoldType(ENTRY_TYPE type) const {
        return type == getType();
    }
};

// Base class for fixed-size numeric types - OPTIMIZED VERSION
template<ENTRY_TYPE T, typename NativeType, size_t S>
class NumericEntry : public IDataEntry {
protected:
    uint8_t data[S];
    
public:
    typedef NativeType ValueType;
    static const size_t SIZE = S;
    static const size_t TOTAL_SIZE = 1 + S;  // Cached for performance
    static const ENTRY_TYPE TYPE = T;
    
    // Default constructor
    NumericEntry() {
        memset(data, 0, S);
    }
    
    // Value constructor - OPTIMIZED: direct memcpy instead of loop
    NumericEntry(NativeType value) {
        memcpy(data, &value, S);
    }
    
    ENTRY_TYPE getType() const override { return T; }
    size_t getDataSize() const override { return S; }
    size_t getTotalSize() const override { return isZero() ? 0 : TOTAL_SIZE; }
    bool isFixedSize() const override { return true; }
    
    bool readFrom(Stream& src) override {
        uint8_t typeByte;
        if (src.readBytes(&typeByte, 1) != 1) return false;
        if (typeByte != T) return false;
        return src.readBytes(data, S) == S;
    }
    
    bool writeTo(Stream& dest) const override {
        if (isZero()) return true;
        
        uint8_t typeByte = T;
        if (dest.write(&typeByte, 1) != 1) return false;
        return dest.write(data, S) == S;
    }
    
    void clear() override { memset(data, 0, S); }
    
    // OPTIMIZED: Single memcpy instead of byte-by-byte
    operator NativeType() const {
        NativeType result;
        memcpy(&result, data, S);
        return result;
    }
    
    // OPTIMIZED: Single memcpy
    NumericEntry& operator=(NativeType value) {
        memcpy(data, &value, S);
        return *this;
    }
    
    // OPTIMIZED: Single comparison instead of loop
    bool isZero() const override {
        // Для ESP лучше использовать поэлементную проверку
        for (size_t i = 0; i < S; i++) {
            if (data[i] != 0) return false;
        }
        return true;
    }
    
    bool setValue(int64_t value) override {
        // Для unsigned типов проверяем, что значение не отрицательное
        if (std::is_unsigned<NativeType>::value && value < 0) return false;
        
        // Проверка на переполнение
        if (value < std::numeric_limits<NativeType>::min() || 
            value > std::numeric_limits<NativeType>::max()) {
            return false;
        }
        
        *this = static_cast<NativeType>(value);
        return true;
    }
    
    bool setValue(uint64_t value) override {
        // Проверка на переполнение
        if (value > std::numeric_limits<NativeType>::max()) {
            return false;
        }
        
        *this = static_cast<NativeType>(value);
        return true;
    }
    
    // Реализация setValue для float/double
    bool setValue(float value) override {
        if constexpr (std::is_same<NativeType, float>::value) {
            *this = value;
            return true;
        } else if constexpr (std::is_same<NativeType, double>::value) {
            *this = static_cast<double>(value);
            return true;
        } else {
            // Для целочисленных типов проверяем возможность преобразования
            if (value < std::numeric_limits<NativeType>::min() || 
                value > std::numeric_limits<NativeType>::max()) {
                return false;
            }
            *this = static_cast<NativeType>(value);
            return true;
        }
    }
    
    bool setValue(double value) override {
        if constexpr (std::is_same<NativeType, double>::value) {
            *this = value;
            return true;
        } else if constexpr (std::is_same<NativeType, float>::value) {
            // Проверяем, не выходит ли double за пределы float
            if (value < -std::numeric_limits<float>::max() || 
                value > std::numeric_limits<float>::max()) {
                return false;
            }
            *this = static_cast<float>(value);
            return true;
        } else {
            // Для целочисленных типов проверяем возможность преобразования
            if (value < std::numeric_limits<NativeType>::min() || 
                value > std::numeric_limits<NativeType>::max()) {
                return false;
            }
            *this = static_cast<NativeType>(value);
            return true;
        }
    }
    
    bool setValue(const String&) override { return false; }
    bool setValue(const char*, uint8_t = 0) override { return false; }
    bool setValue(const uint8_t*, uint16_t) override { return false; }
    
    const uint8_t* raw() const { return data; }
    uint8_t* raw() { return data; }
};

// Unsigned types
class UInt8Entry : public NumericEntry<UINT8_T, uint8_t, 1> {
public:
    using NumericEntry::NumericEntry;
    String toString() const override { 
        return String(static_cast<uint8_t>(*this)); 
    }
};

class UInt16Entry : public NumericEntry<UINT16_T, uint16_t, 2> {
public:
    using NumericEntry::NumericEntry;
    String toString() const override { 
        return String(static_cast<uint16_t>(*this)); 
    }
};

class UInt32Entry : public NumericEntry<UINT32_T, uint32_t, 4> {
public:
    using NumericEntry::NumericEntry;
    String toString() const override { 
        return String(static_cast<uint32_t>(*this)); 
    }
};

class UInt64Entry : public NumericEntry<UINT64_T, uint64_t, 8> {
public:
    using NumericEntry::NumericEntry;
    String toString() const override {
        char buffer[21];
        #ifdef ESP32
            snprintf(buffer, sizeof(buffer), "%llu", static_cast<unsigned long long>(*this));
        #elif defined(ESP8266)
            snprintf(buffer, sizeof(buffer), "%llu", static_cast<unsigned long long>(*this));
        #else
            snprintf(buffer, sizeof(buffer), "%llu", static_cast<uint64_t>(*this));
        #endif
        return String(buffer);
    }
};

// Signed types
class Int8Entry : public NumericEntry<INT8_T, int8_t, 1> {
public:
    using NumericEntry::NumericEntry;
    String toString() const override { 
        return String(static_cast<int8_t>(*this)); 
    }
};

class Int16Entry : public NumericEntry<INT16_T, int16_t, 2> {
public:
    using NumericEntry::NumericEntry;
    String toString() const override { 
        return String(static_cast<int16_t>(*this)); 
    }
};

class Int32Entry : public NumericEntry<INT32_T, int32_t, 4> {
public:
    using NumericEntry::NumericEntry;
    String toString() const override { 
        return String(static_cast<int32_t>(*this)); 
    }
};

class Int64Entry : public NumericEntry<INT64_T, int64_t, 8> {
public:
    using NumericEntry::NumericEntry;
    String toString() const override {
        char buffer[21];
        #ifdef ESP32
            snprintf(buffer, sizeof(buffer), "%lld", static_cast<long long>(*this));
        #elif defined(ESP8266)
            snprintf(buffer, sizeof(buffer), "%lld", static_cast<long long>(*this));
        #else
            snprintf(buffer, sizeof(buffer), "%lld", static_cast<int64_t>(*this));
        #endif
        return String(buffer);
    }
};

// Float type (4 байта)
class FloatEntry : public NumericEntry<FLOAT_T, float, 4> {
public:
    using NumericEntry::NumericEntry;
    String toString() const override { 
        char buffer[32];
        // Используем dtostrf для ESP-совместимого форматирования float
        dtostrf(static_cast<float>(*this), 1, 6, buffer);
        return String(buffer);
    }
    
    // Специализированные методы для float
    bool setValue(float value) override {
        *this = value;
        return true;
    }
    
    bool setValue(double value) override {
        // Проверяем, не выходит ли double за пределы float
        if (value < -std::numeric_limits<float>::max() || 
            value > std::numeric_limits<float>::max()) {
            return false;
        }
        *this = static_cast<float>(value);
        return true;
    }
};

// Double type (8 байт)
class DoubleEntry : public NumericEntry<DOUBLE_T, double, 8> {
public:
    using NumericEntry::NumericEntry;
    String toString() const override { 
        char buffer[32];
        // Используем dtostrf для ESP-совместимого форматирования double
        dtostrf(static_cast<double>(*this), 1, 12, buffer);
        return String(buffer);
    }
    
    // Специализированные методы для double
    bool setValue(float value) override {
        *this = static_cast<double>(value);
        return true;
    }
    
    bool setValue(double value) override {
        *this = value;
        return true;
    }
};

// String type - IMPROVED with move semantics
class StringEntry : public IDataEntry {
private:
    uint8_t* data = nullptr;
    uint8_t length = 0;
    bool ownsData = false;
    
    void freeData() {
        if (ownsData && data) {
            delete[] data;
        }
        data = nullptr;
        length = 0;
        ownsData = false;
    }
    
public:
    StringEntry() = default;
    
    StringEntry(const char* str, uint8_t len = 0) {
        set(str, len);
    }
    
    StringEntry(uint8_t* buffer, uint8_t len, bool takeOwnership = false) {
        if (takeOwnership) {
            data = buffer;
            length = len;
            ownsData = true;
        } else {
            set(buffer, len);
        }
    }
    
    // Move constructor
    StringEntry(StringEntry&& other) noexcept 
        : data(other.data), length(other.length), ownsData(other.ownsData)
    {
        other.data = nullptr;
        other.length = 0;
        other.ownsData = false;
    }
    
    // Move assignment
    StringEntry& operator=(StringEntry&& other) noexcept {
        if (this != &other) {
            freeData();
            data = other.data;
            length = other.length;
            ownsData = other.ownsData;
            other.data = nullptr;
            other.length = 0;
            other.ownsData = false;
        }
        return *this;
    }
    
    // Copy constructor
    StringEntry(const StringEntry& other) {
        set(other.data, other.length);
    }
    
    // Copy assignment
    StringEntry& operator=(const StringEntry& other) {
        if (this != &other) {
            set(other.data, other.length);
        }
        return *this;
    }
    
    ~StringEntry() { freeData(); }
    
    ENTRY_TYPE getType() const override {
        return (length == 0) ? NULL_T : static_cast<ENTRY_TYPE>(length);
    }
    
    size_t getDataSize() const override { return length; }
    size_t getTotalSize() const override { return (length > 0) ? (1 + length) : 0; }
    bool isFixedSize() const override { return false; }
    
    void set(const char* str, uint8_t len = 0) {
        if (!str) {
            clear();
            return;
        }
        
        if (len == 0) len = strlen(str);
        if (len > MAX_CHARS_LENGTH) len = MAX_CHARS_LENGTH;
        
        set(reinterpret_cast<const uint8_t*>(str), len);
    }
    
    void set(const uint8_t* src, uint8_t len) {
        freeData();
        
        if (!src || len == 0) return;
        
        if (len > MAX_CHARS_LENGTH) len = MAX_CHARS_LENGTH;
        
        length = len;
        data = new uint8_t[length];
        memcpy(data, src, length);
        ownsData = true;
    }
    
    bool readFrom(Stream& src) override {
        uint8_t type;
        if (src.readBytes(&type, 1) != 1) return false;
        
        if (type == NULL_T) {
            clear();
            return true;
        }
        
        if (type < CHARS_MIN || type > CHARS_MAX) return false;
        
        uint8_t strLen = type;
        freeData();
        
        length = strLen;
        data = new uint8_t[length];
        ownsData = true;
        
        return src.readBytes(data, length) == length;
    }
    
    bool writeTo(Stream& dest) const override {
        if (length == 0) return true;
        
        uint8_t type = length;
        if (dest.write(&type, 1) != 1) return false;
        return dest.write(data, length) == length;
    }
    
    void clear() override { freeData(); }
    
    bool isZero() const override { return length == 0; }
    
    bool setValue(int64_t) override { return false; }
    bool setValue(uint64_t) override { return false; }
    bool setValue(float) override { return false; }
    bool setValue(double) override { return false; }
    
    bool setValue(const String& value) override {
        set(value.c_str(), value.length());
        return true;
    }
    
    bool setValue(const char* value, uint8_t length = 0) override {
        set(value, length);
        return true;
    }
    
    bool setValue(const uint8_t* data, uint16_t size) override {
        if (size > MAX_CHARS_LENGTH) return false;
        set(data, static_cast<uint8_t>(size));
        return true;
    }
    
    const uint8_t* getData() const { return data; }
    uint8_t getLength() const { return length; }
    
    String toString() const override {
        if (!data || length == 0 || length > MAX_CHARS_LENGTH ) return String("");
        // Для ESP используем безопасный способ создания строки
        String result;
        result.reserve(length);
        for (uint8_t i = 0; i < length; i++) {
            result += static_cast<char>(data[i]);
        }
        return result;
    }
    
    bool canHoldType(ENTRY_TYPE type) const override {
        return type == NULL_T || (type >= CHARS_MIN && type <= CHARS_MAX);
    }
};

// Base class for byte arrays - IMPROVED with move semantics
class BaseByteArrayEntry : public IDataEntry {
protected:
    uint8_t* data = nullptr;
    uint16_t size = 0;
    bool ownsData = false;
    
    void freeData() {
        if (ownsData && data) {
            delete[] data;
        }
        data = nullptr;
        size = 0;
        ownsData = false;
    }
    
public:
    BaseByteArrayEntry() = default;
    
    explicit BaseByteArrayEntry(uint16_t size) {
        set(nullptr, size);
    }
    
    BaseByteArrayEntry(const uint8_t* src, uint16_t size) {
        set(src, size);
    }
    
    // Move constructor
    BaseByteArrayEntry(BaseByteArrayEntry&& other) noexcept
        : data(other.data), size(other.size), ownsData(other.ownsData)
    {
        other.data = nullptr;
        other.size = 0;
        other.ownsData = false;
    }
    
    // Move assignment
    BaseByteArrayEntry& operator=(BaseByteArrayEntry&& other) noexcept {
        if (this != &other) {
            freeData();
            data = other.data;
            size = other.size;
            ownsData = other.ownsData;
            other.data = nullptr;
            other.size = 0;
            other.ownsData = false;
        }
        return *this;
    }
    
    BaseByteArrayEntry(const BaseByteArrayEntry& other) {
        set(other.data, other.size);
    }
    
    BaseByteArrayEntry& operator=(const BaseByteArrayEntry& other) {
        if (this != &other) {
            set(other.data, other.size);
        }
        return *this;
    }
    
    virtual ~BaseByteArrayEntry() { freeData(); }
    
    size_t getDataSize() const override { return size; }
    bool isFixedSize() const override { return false; }
    
    void clear() override { freeData(); }
    
    bool isZero() const override { return size == 0; }
    
    void set(const uint8_t* src, uint16_t newSize) {
        freeData();
        
        size = newSize;
        if (size > 0) {
            data = new uint8_t[size];
            if (src) {
                memcpy(data, src, size);
            } else {
                memset(data, 0, size);
            }
        }
        ownsData = true;
    }
    
    const uint8_t* getData() const { return data; }
    uint8_t* getData() { return data; }
    uint16_t getSize() const { return size; }
    
    bool setValue(int64_t) override { return false; }
    bool setValue(uint64_t) override { return false; }
    bool setValue(float) override { return false; }
    bool setValue(double) override { return false; }
    
    bool setValue(const String& value) override {
        set(reinterpret_cast<const uint8_t*>(value.c_str()), value.length());
        return true;
    }
    
    bool setValue(const char* value, uint8_t length = 0) override {
        if (!length && value) length = strlen(value);
        set(reinterpret_cast<const uint8_t*>(value), length);
        return true;
    }
    
    bool setValue(const uint8_t* data, uint16_t size) override {
        set(data, size);
        return true;
    }
    
    uint8_t& operator[](uint16_t index) {
        static uint8_t dummy = 0;
        return (index < size && data) ? data[index] : dummy;
    }
    
    const uint8_t& operator[](uint16_t index) const {
        static const uint8_t dummy = 0;
        return (index < size && data) ? data[index] : dummy;
    }
    
    String toString() const override {
        String result = "ByteArray[";
        result += String(size);
        result += "] = {";
        
        uint16_t limit = (size < 16) ? size : 16;
        for (uint16_t i = 0; i < limit; i++) {
            if (i > 0) result += " ";
            char hex[3];
            snprintf(hex, sizeof(hex), "%02X", data[i]);
            result += hex;
        }
        if (size > 16) result += " ...";
        result += "}";
        
        return result;
    }
};

// Byte array up to 255 bytes
class ByteArrayEntry : public BaseByteArrayEntry {
public:
    using BaseByteArrayEntry::BaseByteArrayEntry;
    
    ENTRY_TYPE getType() const override { 
        return size == 0 ? NULL_T : BYTE_ARRAY_T;
    }
    
    size_t getTotalSize() const override { 
        return (size > 0) ? (1 + 1 + size) : 0;
    }
    
    bool readFrom(Stream& src) override {
        uint8_t type;
        if (src.readBytes(&type, 1) != 1) return false;
        
        if (type == NULL_T) {
            clear();
            return true;
        }
        
        if (type != BYTE_ARRAY_T) return false;
        
        uint8_t arraySize;
        if (src.readBytes(&arraySize, 1) != 1) return false;
        
        freeData();
        
        size = arraySize;
        if (size > 0) {
            data = new uint8_t[size];
            ownsData = true;
            return src.readBytes(data, size) == size;
        }
        
        ownsData = true;
        return true;
    }
    
    bool writeTo(Stream& dest) const override {
        if (size == 0) return true;
        if (size > 0xFF) return false;
        
        uint8_t type = BYTE_ARRAY_T;
        if (dest.write(&type, 1) != 1) return false;
        
        uint8_t sizeByte = static_cast<uint8_t>(size);
        if (dest.write(&sizeByte, 1) != 1) return false;
        
        if (size > 0 && data) {
            return dest.write(data, size) == size;
        }
        
        return true;
    }
    
    bool canHoldType(ENTRY_TYPE type) const override {
        return type == NULL_T || type == BYTE_ARRAY_T;
    }
};

// Byte array up to 65535 bytes
class ByteArrayBigEntry : public BaseByteArrayEntry {
public:
    using BaseByteArrayEntry::BaseByteArrayEntry;
    
    ENTRY_TYPE getType() const override { 
        return size == 0 ? NULL_T : BYTE_ARRAY_BIG_T;
    }
    
    size_t getTotalSize() const override { 
        return (size > 0) ? (1 + 2 + size) : 0;
    }
    
    bool readFrom(Stream& src) override {
        uint8_t type;
        if (src.readBytes(&type, 1) != 1) return false;
        
        if (type == NULL_T) {
            clear();
            return true;
        }
        
        if (type != BYTE_ARRAY_BIG_T) return false;
        
        uint8_t sizeBytes[2];
        if (src.readBytes(sizeBytes, 2) != 2) return false;
        
        uint16_t arraySize = (static_cast<uint16_t>(sizeBytes[1]) << 8) | sizeBytes[0];
        
        freeData();
        
        size = arraySize;
        if (size > 0) {
            data = new uint8_t[size];
            ownsData = true;
            return src.readBytes(data, size) == size;
        }
        
        ownsData = true;
        return true;
    }
    
    bool writeTo(Stream& dest) const override {
        if (size == 0) return true;
        if (size > 0xFFFF) return false;
        
        uint8_t type = BYTE_ARRAY_BIG_T;
        if (dest.write(&type, 1) != 1) return false;
        
        uint8_t sizeBytes[2] = {
            static_cast<uint8_t>(size & 0xFF),
            static_cast<uint8_t>((size >> 8) & 0xFF)
        };
        if (dest.write(sizeBytes, 2) != 2) return false;
        
        if (size > 0 && data) {
            return dest.write(data, size) == size;
        }
        
        return true;
    }
    
    bool canHoldType(ENTRY_TYPE type) const override {
        return type == NULL_T || type == BYTE_ARRAY_BIG_T;
    }
};

#endif // DATA_TYPES_H