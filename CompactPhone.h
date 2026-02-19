#pragma once
#include "DataTypes.h"

#define VAR_CLOUDE

#ifdef VAR_CLOUDE
class PackedPhone {
public:
    // Bit field constants
    static constexpr uint8_t FLAGS_BITS = 4;
    static constexpr uint8_t GROUP_BITS = 10;
    static constexpr uint8_t NUM_BITS = 64 - FLAGS_BITS - GROUP_BITS; // 50 bits
    
    // Maximum values
    static constexpr uint16_t MAX_GROUP = (1U << GROUP_BITS) - 1;  // 1023
    static constexpr uint64_t MAX_NUM = (1ULL << NUM_BITS) - 1;    // ~1.125e15
    static constexpr uint8_t MAX_FLAGS = (1U << FLAGS_BITS) - 1;   // 15

private:
    union {
        struct {
            uint64_t num   : NUM_BITS;
            uint64_t flags : FLAGS_BITS;
            uint64_t group : GROUP_BITS;
            
        } parts;
        uint64_t raw;
    } dat;

public:
    // Default constructor
    constexpr PackedPhone() : dat{.raw = 0ULL} {}

    // Main constructor with validation
    PackedPhone(uint64_t phone, uint8_t flags = 0, uint16_t group = 0) 
        : dat{.raw = 0ULL}
    {
        if (phone <= MAX_NUM && group <= MAX_GROUP && flags <= MAX_FLAGS) {
            dat.parts.num = phone;
            dat.parts.flags = flags;
            dat.parts.group = group;
        }
    }

    PackedPhone(const String& phone_str, uint8_t flags = 0, uint16_t group = 0) :
         dat{.raw = 0ULL}
    {
        uint64_t num;
        char digits[20];
        // фильтруем цифры
        int len = 0;
        for (int i = 0; phone_str[i] != '\0'; i++) {
            if (isdigit(phone_str[i]) ) {
                digits[len++] = phone_str[i];
            }
        }
        digits[len] = '\0';
        log_d("Found %i digits", len);

        if ( len < 5) return;

        log_d("%s", digits);

        auto res = sscanf(digits, "%llu",  &num );
        if ( res > 0 ){
            dat.parts.num = num;
            dat.parts.flags = flags;
            dat.parts.group = group;
        }
        log_d("%llu, %u, %u", this->phone(), this->group(), this->flags()  );
    }

    // Explicit constructor from raw value
    explicit constexpr PackedPhone(uint64_t raw_value) : dat{.raw = raw_value} {}

    // Explicit constructor from UInt64Entry
    explicit PackedPhone(const UInt64Entry& entry) 
        : dat{.raw = static_cast<uint64_t>(entry)} {}

    // Getters
    constexpr bool isValid() const { return dat.parts.num != 0; }
    constexpr uint64_t phone() const { return dat.parts.num; }
    constexpr uint16_t group() const { return static_cast<uint16_t>(dat.parts.group); }
    constexpr uint8_t flags() const { return static_cast<uint8_t>(dat.parts.flags); }
    constexpr uint64_t raw() const { return dat.raw; }

    // Conversion operators
    constexpr operator uint64_t() const { return dat.raw; }

    // Assignment operators
    PackedPhone& operator=(const UInt64Entry& entry) {
        dat.raw = static_cast<uint64_t>(entry);
        return *this;
    }

    PackedPhone& operator=(uint64_t value) {
        dat.raw = value;
        return *this;
    }

    // Format phone as string with buffer (returns success status)
    bool phoneStr(char* buf, size_t buffSize) const {
        if (!buf || buffSize < 21) return false;
        return snprintf(buf, buffSize, "%llu", phone()) > 0;
    }

    // Format phone as String object
    String phoneStr() const {
        char buff[21];
        snprintf(buff, sizeof(buff), "%llu", phone());
        return String(buff);
    }

    // Full debug string
    String toString() const {
        char buffer[100];
        snprintf(buffer, sizeof(buffer), 
                 "Phone{num:%llu, group:%u, flags:%u}", 
                 phone(), group(), flags());
        return String(buffer);
    }

    // // Comparison operators
    // constexpr bool operator==(const PackedPhone& other) const {
    //     return dat.raw == other.dat.raw;
    // }

    // constexpr bool operator!=(const PackedPhone& other) const {
    //     return dat.raw != other.dat.raw;
    // }

        // Comparison operators
    constexpr bool operator==(const PackedPhone& other) const {
        return phone() == other.phone();
    }

    constexpr bool operator!=(const PackedPhone& other) const {
        return phone() != other.phone();
    }
    constexpr bool operator<(const PackedPhone& other) const {
        return phone() < other.phone();
    }
};

#else

class PackedPhone {
public:
    static constexpr const uint8_t FLAGS_BITS = 4; // 4 флага
    static constexpr const uint8_t GROUP_BITS = 10;
    static constexpr const uint16_t MAX_GROUP = (1U << GROUP_BITS) - 1; // 10=>1023
    static constexpr const uint64_t MAX_NUM = ( 1ULL << (sizeof(uint64_t)*8 - (FLAGS_BITS+GROUP_BITS ))) -1;
private:
    union {
        struct {
            uint64_t flags : FLAGS_BITS;
            uint64_t group : GROUP_BITS;
            uint64_t num : (sizeof(uint64_t)*8 - (FLAGS_BITS+GROUP_BITS ));
        } parts;
        uint64_t raw;
    } dat;
    
public:
    PackedPhone() : dat{ 0ULL}
    {
       // dat.raw = 0ull;
    };

    PackedPhone(const uint8_t flags, const uint64_t phone,  const uint16_t group = 0 ) 
        : dat{ 0ULL }
    {
        if (group <= MAX_GROUP) {
            dat.parts.flags = flags;
            dat.parts.group = group;
            dat.parts.num = phone;     
        }
    }

        // Явный конструктор из raw значения (uint64_t)
    explicit PackedPhone(uint64_t raw_value) //: dat{raw_value} 
    {
        dat.raw = raw_value;
    }
    
    // Явный конструктор из UInt64Entry
    explicit PackedPhone(const UInt64Entry& entry) //: dat{static_cast<uint64_t>(entry)} {}
    //GPhone(const uint64_t raw_value)
    {
        dat.raw = static_cast<uint64_t>(entry);
    }
    
    bool isValid() const { return phone() != 0ULL; }
    uint64_t phone() const { return dat.parts.num; }
    uint16_t group() const { return (uint16_t)dat.parts.group; }
    uint8_t flags() const { return (uint8_t)dat.parts.flags; }
    uint64_t raw() const { return dat.raw; }
    
    // Для преобразования в UInt64Entry
    operator uint64_t() const { return dat.raw; }
    
    // Для присваивания из UInt64Entry
    PackedPhone& operator=(const UInt64Entry& entry ) {
        uint64_t raw = static_cast<uint64_t>(entry);
        dat.raw = raw;
        return *this;
    }
    PackedPhone& operator=(uint64_t value) {
        dat.raw = value;
        return *this;
    }
    
    char * phoneStr(char * buf, size_t buffSize) const {
        if ( buffSize < 21 ) return nullptr;
        auto res = snprintf(buf, buffSize, "%llu", phone() );
        return res > 0 ? buf : nullptr;
    };
    String phoneStr() const {
        char buff[21];
        return String(phoneStr(buff, sizeof(buff)));
    }

    String toString() const {
        char buffer[100];
        snprintf(buffer, sizeof(buffer), "Phone{num:%llu, group:%u, flags:%u }", 
                phone(), group(), flags() );
        return String(buffer);
    }
}; 

#endif

