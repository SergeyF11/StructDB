#ifndef STRUCT_BUILDER_H
#define STRUCT_BUILDER_H

#include "DataTypes.h"
#include <type_traits>

// Platform and C++ version detection
#if defined(ESP32) || defined(ESP8266)
    // ESP platforms typically support C++11/14, check actual version
    #if __cplusplus >= 201703L
        #define USE_CPP17_FEATURES 1
        #include <tuple>
        #include <utility>
    #elif __cplusplus >= 201402L
        #define USE_CPP17_FEATURES 0
        #define USE_CPP14_FEATURES 1
    #else
        #define USE_CPP17_FEATURES 0
        #define USE_CPP14_FEATURES 0
    #endif
#else
    // Other platforms
    #if __cplusplus >= 201703L
        #define USE_CPP17_FEATURES 1
        #include <tuple>
        #include <utility>
    #else
        #define USE_CPP17_FEATURES 0
    #endif
#endif

// Type traits for data entries
template<typename T>
struct is_string_entry : std::false_type {};

template<>
struct is_string_entry<StringEntry> : std::true_type {};

template<typename T>
struct is_byte_array_entry : std::false_type {};

template<>
struct is_byte_array_entry<ByteArrayEntry> : std::true_type {};

template<>
struct is_byte_array_entry<ByteArrayBigEntry> : std::true_type {};

template<typename T>
struct is_array_or_string {
    static constexpr bool value = 
        is_string_entry<T>::value || 
        is_byte_array_entry<T>::value;
};

template<typename T>
struct is_fixed_numeric {
    static constexpr bool value = 
        std::is_same<T, UInt8Entry>::value ||
        std::is_same<T, UInt16Entry>::value ||
        std::is_same<T, UInt32Entry>::value ||
        std::is_same<T, UInt64Entry>::value ||
        std::is_same<T, Int8Entry>::value ||
        std::is_same<T, Int16Entry>::value ||
        std::is_same<T, Int32Entry>::value ||
        std::is_same<T, Int64Entry>::value ||
        std::is_same<T, FloatEntry>::value ||    
        std::is_same<T, DoubleEntry>::value;
};

// Check for duplicate types
template<typename... Types>
struct check_duplicate_types;

template<typename T>
struct check_duplicate_types<T> {
    static constexpr bool valid = true;
};

template<typename T1, typename T2, typename... Rest>
struct check_duplicate_types<T1, T2, Rest...> {
private:
    static constexpr bool is_same_fixed = 
        is_fixed_numeric<T1>::value && 
        is_fixed_numeric<T2>::value && 
        std::is_same<T1, T2>::value;
    
    static constexpr bool first_ok = !is_same_fixed;
    static constexpr bool rest_ok = check_duplicate_types<T1, Rest...>::valid &&
                                     check_duplicate_types<T2, Rest...>::valid;
public:
    static constexpr bool valid = first_ok && rest_ok;
};

// OPTIMIZED: Fast skip function for readFrom
inline bool skipBytes(Stream& src, size_t count) {
    if (count == 0) return true;
    
    // Buffered reading (faster than byte-by-byte)
    const size_t BUFFER_SIZE = 64;
    uint8_t buffer[BUFFER_SIZE];
    
    while (count > 0) {
        size_t chunk = (count > BUFFER_SIZE) ? BUFFER_SIZE : count;
        if (src.readBytes(buffer, chunk) != chunk) return false;
        count -= chunk;
    }
    return true;
}

// Base class for all structures
template<size_t N>
class StructBase {
protected:
    IDataEntry* members[N];
    
public:
    StructBase() {
        for (size_t i = 0; i < N; i++) {
            members[i] = nullptr;
        }
    }
    
    virtual ~StructBase() = default;
    
    // Write structure to stream
    bool writeTo(Stream& dest) const {
        for (size_t i = 0; i < N; i++) {
            if (members[i] == nullptr) break;
            
            // Skip zero values for fixed-size types
            if (members[i]->isZero() && members[i]->isFixedSize()) {
                continue;
            }
            
            // Skip empty arrays and strings
            if (members[i]->isZero() && !members[i]->isFixedSize()) {
                continue;
            }
            
            if (!members[i]->writeTo(dest)) {
                return false;
            }
        }
        
        // Structure ends with NULL_T
        uint8_t nullTerminator = NULL_T;
        return dest.write(&nullTerminator, 1) == 1;
    }
    
    bool readFrom(Stream& src) {
        clear();
        
        size_t currentMember = 0;
        
        while (currentMember < N) {
            if (members[currentMember] == nullptr) break;
            
            if (src.peek() == -1) break;
            
            uint8_t typeByte = static_cast<uint8_t>(src.peek());
            
            if (typeByte == NULL_T) {
                src.read(); // Consume NULL_T
                break;
            }
            
            ENTRY_TYPE actualType = static_cast<ENTRY_TYPE>(typeByte);
            bool typeMatches = members[currentMember]->canHoldType(actualType);
            
            if (typeMatches) {
                if (!members[currentMember]->readFrom(src)) {
                    return false;
                }
                currentMember++;
            } else {
                // Skip mismatched element
                currentMember++;
            }
        }
        
        // OPTIMIZED: Skip remaining data until NULL_T
        while (true) {
            if (src.peek() == -1) break;
            
            uint8_t typeByte = static_cast<uint8_t>(src.peek());
            if (typeByte == NULL_T) {
                src.read(); // Consume NULL_T
                break;
            }
            
            src.read(); // Consume type byte
            
            size_t skipSize = 0;
            ENTRY_TYPE skippedType = static_cast<ENTRY_TYPE>(typeByte);
            
            if (skippedType >= UINT8_T && skippedType <= INT64_T) {
                // Numeric types
                static const size_t sizes[] = {1, 2, 4, 8, 1, 2, 4, 8};
                skipSize = sizes[skippedType - UINT8_T];
            } 
            else if (skippedType == FLOAT_T) {
                skipSize = 4;  // Размер float
            }
            else if (skippedType == DOUBLE_T) {
                skipSize = 8;  // Размер double
            }
            else if (skippedType == BYTE_ARRAY_T) {
                uint8_t sizeByte;
                if (src.readBytes(&sizeByte, 1) != 1) return false;
                skipSize = sizeByte;
            } 
            else if (skippedType == BYTE_ARRAY_BIG_T) {
                uint8_t sizeBytes[2];
                if (src.readBytes(sizeBytes, 2) != 2) return false;
                skipSize = (static_cast<uint16_t>(sizeBytes[1]) << 8) | sizeBytes[0];
            } 
            else if (skippedType >= CHARS_MIN && skippedType <= CHARS_MAX) {
                skipSize = skippedType;
            }
            
            // OPTIMIZED: Use fast skip instead of byte-by-byte
            if (!skipBytes(src, skipSize)) return false;
        }
        
        return true;
    }
    
    void clear() {
        for (size_t i = 0; i < N; i++) {
            if (members[i] != nullptr) {
                members[i]->clear();
            }
        }
    }
    
    String toString() const {
        String result = "Struct{";
        bool first = true;
        
        for (size_t i = 0; i < N; i++) {
            if (members[i] == nullptr) break;
            
            if (!first) result += ", ";
            first = false;
            
            result += "[";
            result += String(i);
            result += "]=";
            result += members[i]->toString();
        }
        
        result += "}";
        return result;
    }
    
    // Generic set by index
    template<typename V>
    bool set(size_t index, V&& value) {
        if (index >= N || members[index] == nullptr) return false;
        
        // Use type traits to determine how to set the value
        typedef typename std::remove_reference<V>::type CleanV;
        
        if (std::is_integral<CleanV>::value) {
            if (std::is_unsigned<CleanV>::value) {
                return members[index]->setValue(static_cast<uint64_t>(value));
            } else {
                return members[index]->setValue(static_cast<int64_t>(value));
            }
        } 
        else if (std::is_same<CleanV, float>::value) {
            return members[index]->setValue(static_cast<float>(value));
        }
        else if (std::is_same<CleanV, double>::value) {
            return members[index]->setValue(static_cast<double>(value));
        }
        else if (std::is_same<CleanV, String>::value) {
            return members[index]->setValue(value);
        }
        else if (std::is_same<CleanV, const char*>::value || 
                std::is_same<CleanV, char*>::value) {
            return members[index]->setValue(value);
        }
        
        return false;
    }

// 
    // bool set(size_t index, V&& value) {
    //     if (index >= N || members[index] == nullptr) return false;
        
    //     // Use type traits to determine how to set the value
    //     typedef typename std::remove_reference<V>::type CleanV;
        
    //     if (std::is_integral<CleanV>::value) {
    //         if (std::is_unsigned<CleanV>::value) {
    //             return members[index]->setValue(static_cast<uint64_t>(value));
    //         } else {
    //             return members[index]->setValue(static_cast<int64_t>(value));
    //         }
    //     } 
    //     else if (std::is_same<CleanV, String>::value) {
    //         return members[index]->setValue(value);
    //     }
    //     else if (std::is_same<CleanV, const char*>::value || 
    //              std::is_same<CleanV, char*>::value) {
    //         return members[index]->setValue(value);
    //     }
        
    //     return false;
    // }
    
    // Get member count
    static constexpr size_t size() { return N; }
};

#if USE_CPP17_FEATURES
// ============================================================================
// C++17 VERSION: Variadic templates with if constexpr
// ============================================================================

template<typename... Types>
class Struct : public StructBase<sizeof...(Types)> {
private:
    std::tuple<Types...> membersData;
    
    // Initialize members array with pointers to tuple elements
    template<size_t... Is>
    void initMembersImpl(std::index_sequence<Is...>) {
        // Expand pack using dummy array trick
        int dummy[] = {(StructBase<sizeof...(Types)>::members[Is] = &std::get<Is>(membersData), 0)...};
        (void)dummy; // Suppress unused warning
    }
    
    void initMembers() {
        initMembersImpl(std::index_sequence_for<Types...>{});
    }
    
    // Static assertion for duplicate types
    static_assert(check_duplicate_types<Types...>::valid, 
                  "Duplicate fixed numeric types in struct are not allowed");
    
public:
    Struct() {
        initMembers();
    }
    
    // Constructor with initial values
    template<typename... Args>
    explicit Struct(Args&&... args) : membersData(std::forward<Args>(args)...) {
        initMembers();
    }
    
    // Get member by index (compile-time)
    template<size_t I>
    auto& get() {
        static_assert(I < sizeof...(Types), "Index out of bounds");
        return std::get<I>(membersData);
    }
    
    template<size_t I>
    const auto& get() const {
        static_assert(I < sizeof...(Types), "Index out of bounds");
        return std::get<I>(membersData);
    }
    
    // Set member by index with if constexpr
    template<size_t I, typename V>
    void set(V&& value) {
        static_assert(I < sizeof...(Types), "Index out of bounds");
        
        using MemberType = typename std::tuple_element<I, std::tuple<Types...>>::type;
        using CleanV = typename std::remove_reference<V>::type;
        auto& member = std::get<I>(membersData);
        
        // Use if constexpr for compile-time branching (C++17)
        if constexpr (std::is_same<CleanV, MemberType>::value) {
            // Direct assignment of same type
            member = std::forward<V>(value);
        }
        else if constexpr (is_fixed_numeric<MemberType>::value && std::is_arithmetic<CleanV>::value) {
            // Numeric type with arithmetic value
            member = static_cast<typename MemberType::ValueType>(value);
        }
        else if constexpr (std::is_same<CleanV, const char*>::value ||
                           std::is_same<CleanV, char*>::value ||
                           std::is_same<CleanV, String>::value) {
            // String/char* to StringEntry or ByteArray
            member.setValue(value);
        }
        else {
            // Fallback: try direct assignment
            member = std::forward<V>(value);
        }
    }
    
    // Convenience: set all values at once
    template<typename... Args>
    void setAll(Args&&... args) {
        static_assert(sizeof...(Args) == sizeof...(Types), 
                     "Number of arguments must match number of members");
        setAllImpl(std::index_sequence_for<Types...>{}, std::forward<Args>(args)...);
    }
    
private:
    template<size_t... Is, typename... Args>
    void setAllImpl(std::index_sequence<Is...>, Args&&... args) {
        // Use dummy array to expand pack
        int dummy[] = {(set<Is>(std::forward<Args>(args)), 0)...};
        (void)dummy;
    }
};

// Type aliases for backward compatibility
template<typename T1>
using Struct1 = Struct<T1>;

template<typename T1, typename T2>
using Struct2 = Struct<T1, T2>;

template<typename T1, typename T2, typename T3>
using Struct3 = Struct<T1, T2, T3>;

template<typename T1, typename T2, typename T3, typename T4>
using Struct4 = Struct<T1, T2, T3, T4>;

#else
// ============================================================================
// C++11/14 FALLBACK VERSION: Manual Struct1-4 implementations
// ============================================================================

// Helper for set implementation - SFINAE based approach for C++11/14
namespace detail {
    // For numeric types with arithmetic values
    template<typename T, typename V>
    typename std::enable_if<is_fixed_numeric<T>::value && std::is_arithmetic<V>::value, void>::type
    set_impl(T& member, V&& value) {
        //member = static_cast<typename T::ValueType>(value);
        member.setValue(value);
    }
    
    // For same type assignment
    template<typename T>
    void set_impl(T& member, const T& value) {
        member = value;
    }
    
    template<typename T>
    void set_impl(T& member, T&& value) {
        member = std::move(value);
    }
    
    // For string/array types with string values
    template<typename T>
    typename std::enable_if<is_string_entry<T>::value || is_byte_array_entry<T>::value, void>::type
    set_impl(T& member, const char* value) {
        member.setValue(value);
    }
    
    template<typename T>
    typename std::enable_if<is_string_entry<T>::value || is_byte_array_entry<T>::value, void>::type
    set_impl(T& member, const String& value) {
        member.setValue(value);
    }
}

template<typename T1>
class Struct1 : public StructBase<1> {
    T1 member1;
    
public:
    Struct1() {
        this->members[0] = &member1;
    }
    
    explicit Struct1(const T1& m1) : member1(m1) {
        this->members[0] = &member1;
    }
    
    T1& get1() { return member1; }
    const T1& get1() const { return member1; }
    
    template<size_t I>
    T1& get() { 
        static_assert(I == 0, "Index out of bounds");
        return member1; 
    }
    
    template<size_t I>
    const T1& get() const { 
        static_assert(I == 0, "Index out of bounds");
        return member1; 
    }
    
    template<typename V>
    void set1(V&& value) { 
        detail::set_impl(member1, std::forward<V>(value));
    }
    
    template<size_t I, typename V>
    void set(V&& value) {
        static_assert(I == 0, "Index out of bounds");
        set1(std::forward<V>(value));
    }
};

template<typename T1, typename T2>
class Struct2 : public StructBase<2> {
    T1 member1;
    T2 member2;
    
    static_assert(check_duplicate_types<T1, T2>::valid, 
                  "Duplicate fixed numeric types in struct are not allowed");
    
public:
    Struct2() {
        this->members[0] = &member1;
        this->members[1] = &member2;
    }
    
    Struct2(const T1& m1, const T2& m2) : member1(m1), member2(m2) {
        this->members[0] = &member1;
        this->members[1] = &member2;
    }
    
    T1& get1() { return member1; }
    T2& get2() { return member2; }
    const T1& get1() const { return member1; }
    const T2& get2() const { return member2; }
    
    template<size_t I>
    typename std::conditional<I == 0, T1&, T2&>::type get() {
        static_assert(I < 2, "Index out of bounds");
        return I == 0 ? static_cast<typename std::conditional<I == 0, T1&, T2&>::type>(member1) 
                      : static_cast<typename std::conditional<I == 0, T1&, T2&>::type>(member2);
    }
    
    template<typename V>
    void set1(V&& value) { 
        detail::set_impl(member1, std::forward<V>(value));
    }
    
    template<typename V>
    void set2(V&& value) { 
        detail::set_impl(member2, std::forward<V>(value));
    }
    
    template<typename V1, typename V2>
    void set(V1&& m1, V2&& m2) {
        set1(std::forward<V1>(m1));
        set2(std::forward<V2>(m2));
    }
    
    template<size_t I, typename V>
    void set(V&& value) {
        static_assert(I < 2, "Index out of bounds");
        if (I == 0) set1(std::forward<V>(value));
        else set2(std::forward<V>(value));
    }
    
    using StructBase<2>::set;
};

template<typename T1, typename T2, typename T3>
class Struct3 : public StructBase<3> {
    T1 member1;
    T2 member2;
    T3 member3;
    
    static_assert(check_duplicate_types<T1, T2, T3>::valid, 
                  "Duplicate fixed numeric types in struct are not allowed");
    
public:
    Struct3() {
        this->members[0] = &member1;
        this->members[1] = &member2;
        this->members[2] = &member3;
    }
    
    Struct3(const T1& m1, const T2& m2, const T3& m3) : member1(m1), member2(m2), member3(m3) {
        this->members[0] = &member1;
        this->members[1] = &member2;
        this->members[2] = &member3;
    }
    
    T1& get1() { return member1; }
    T2& get2() { return member2; }
    T3& get3() { return member3; }
    const T1& get1() const { return member1; }
    const T2& get2() const { return member2; }
    const T3& get3() const { return member3; }
    
    template<size_t I>
    typename std::conditional<I == 0, T1&, typename std::conditional<I == 1, T2&, T3&>::type>::type get() {
        static_assert(I < 3, "Index out of bounds");
        return I == 0 ? (typename std::conditional<I == 0, T1&, typename std::conditional<I == 1, T2&, T3&>::type>::type)(member1)
             : I == 1 ? (typename std::conditional<I == 0, T1&, typename std::conditional<I == 1, T2&, T3&>::type>::type)(member2)
             : (typename std::conditional<I == 0, T1&, typename std::conditional<I == 1, T2&, T3&>::type>::type)(member3);
    }
    
    template<typename V>
    void set1(V&& value) { 
        detail::set_impl(member1, std::forward<V>(value));
    }
    
    template<typename V>
    void set2(V&& value) { 
        detail::set_impl(member2, std::forward<V>(value));
    }
    
    template<typename V>
    void set3(V&& value) { 
        detail::set_impl(member3, std::forward<V>(value));
    }
    
    template<typename V1, typename V2, typename V3>
    void set(V1&& m1, V2&& m2, V3&& m3) {
        set1(std::forward<V1>(m1));
        set2(std::forward<V2>(m2));
        set3(std::forward<V3>(m3));
    }
    
    template<size_t I, typename V>
    void set(V&& value) {
        static_assert(I < 3, "Index out of bounds");
        if (I == 0) set1(std::forward<V>(value));
        else if (I == 1) set2(std::forward<V>(value));
        else set3(std::forward<V>(value));
    }
    
    using StructBase<3>::set;
};

template<typename T1, typename T2, typename T3, typename T4>
class Struct4 : public StructBase<4> {
    T1 member1;
    T2 member2;
    T3 member3;
    T4 member4;
    
    static_assert(check_duplicate_types<T1, T2, T3, T4>::valid, 
                  "Duplicate fixed numeric types in struct are not allowed");
    
public:
    Struct4() {
        this->members[0] = &member1;
        this->members[1] = &member2;
        this->members[2] = &member3;
        this->members[3] = &member4;
    }
    
    Struct4(const T1& m1, const T2& m2, const T3& m3, const T4& m4) 
    : member1(m1), member2(m2), member3(m3), member4(m4)
    {
        this->members[0] = &member1;
        this->members[1] = &member2;
        this->members[2] = &member3;
        this->members[3] = &member4;
    }
    
    T1& get1() { return member1; }
    T2& get2() { return member2; }
    T3& get3() { return member3; }
    T4& get4() { return member4; }

    const T1& get1() const { return member1; }
    const T2& get2() const { return member2; }
    const T3& get3() const { return member3; }
    const T4& get4() const { return member4; }
    
    template<size_t I>
    typename std::conditional<I == 0, T1&, 
        typename std::conditional<I == 1, T2&,
            typename std::conditional<I == 2, T3&, T4&>::type>::type>::type get() {
        static_assert(I < 4, "Index out of bounds");
        return I == 0 ? (typename std::conditional<I == 0, T1&, typename std::conditional<I == 1, T2&, typename std::conditional<I == 2, T3&, T4&>::type>::type>::type)(member1)
             : I == 1 ? (typename std::conditional<I == 0, T1&, typename std::conditional<I == 1, T2&, typename std::conditional<I == 2, T3&, T4&>::type>::type>::type)(member2)
             : I == 2 ? (typename std::conditional<I == 0, T1&, typename std::conditional<I == 1, T2&, typename std::conditional<I == 2, T3&, T4&>::type>::type>::type)(member3)
             : (typename std::conditional<I == 0, T1&, typename std::conditional<I == 1, T2&, typename std::conditional<I == 2, T3&, T4&>::type>::type>::type)(member4);
    }
    
    template<typename V>
    void set1(V&& value) { 
        detail::set_impl(member1, std::forward<V>(value));
    }
    
    template<typename V>
    void set2(V&& value) { 
        detail::set_impl(member2, std::forward<V>(value));
    }
    
    template<typename V>
    void set3(V&& value) { 
        detail::set_impl(member3, std::forward<V>(value));
    }
    
    template<typename V>
    void set4(V&& value) { 
        detail::set_impl(member4, std::forward<V>(value));
    }
    
    template<typename V1, typename V2, typename V3, typename V4>
    void set(V1&& m1, V2&& m2, V3&& m3, V4&& m4) {
        set1(std::forward<V1>(m1));
        set2(std::forward<V2>(m2));
        set3(std::forward<V3>(m3));
        set4(std::forward<V4>(m4));
    }
    
    template<size_t I, typename V>
    void set(V&& value) {
        static_assert(I < 4, "Index out of bounds");
        if (I == 0) set1(std::forward<V>(value));
        else if (I == 1) set2(std::forward<V>(value));
        else if (I == 2) set3(std::forward<V>(value));
        else set4(std::forward<V>(value));
    }
    
    using StructBase<4>::set;
};

#endif // USE_CPP17_FEATURES

// Creator functions for convenience
namespace StructCreator {
    template<typename T1>
    Struct1<T1> create(const T1& m1) {
        return Struct1<T1>(m1);
    }
    
    template<typename T1, typename T2>
    Struct2<T1, T2> create(const T1& m1, const T2& m2) {
        return Struct2<T1, T2>(m1, m2);
    }
    
    template<typename T1, typename T2, typename T3>
    Struct3<T1, T2, T3> create(const T1& m1, const T2& m2, const T3& m3) {
        return Struct3<T1, T2, T3>(m1, m2, m3);
    }
    
    template<typename T1, typename T2, typename T3, typename T4>
    Struct4<T1, T2, T3, T4> create(const T1& m1, const T2& m2, const T3& m3, const T4& m4) {
        return Struct4<T1, T2, T3, T4>(m1, m2, m3, m4);
    }
}

// Schema Migration Support
namespace SchemaMigrator {
    // Base migration interface
    class Migration {
    public:
        virtual ~Migration() = default;
        virtual bool migrate(Stream& src, Stream& dest) = 0;
        virtual uint16_t getFromVersion() const = 0;
        virtual uint16_t getToVersion() const = 0;
    };

    // Generic migration between structs with different types
    template<typename FromStruct, typename ToStruct>
    class GenericMigration : public Migration {
    private:
        uint16_t fromVersion;
        uint16_t toVersion;
        
    public:
        GenericMigration(uint16_t fromVer, uint16_t toVer) 
            : fromVersion(fromVer), toVersion(toVer) {}
        
        uint16_t getFromVersion() const override { return fromVersion; }
        uint16_t getToVersion() const override { return toVersion; }
        
        bool migrate(Stream& src, Stream& dest) override {
            FromStruct oldStruct;
            if (!oldStruct.readFrom(src)) return false;
            
            ToStruct newStruct;
            // Copy common fields
            copyCommonFields(oldStruct, newStruct);
            
            return newStruct.writeTo(dest);
        }
        
    private:
        // Helper to copy common fields between structs
        template<size_t FromIdx, size_t ToIdx>
        void copyCommonFields(const FromStruct& from, ToStruct& to) {
            // This would need to be specialized for each migration
        }
    };

    // Migration registry
    class Migrator {
    private:
        struct MigrationEntry {
            uint16_t fromVersion;
            uint16_t toVersion;
            Migration* migration;
            
            bool operator<(const MigrationEntry& other) const {
                return fromVersion < other.fromVersion || 
                      (fromVersion == other.fromVersion && toVersion < other.toVersion);
            }
        };
        
        std::vector<MigrationEntry> migrations;
        
    public:
        void addMigration(Migration* migration) {
            migrations.push_back({migration->getFromVersion(), 
                                  migration->getToVersion(), 
                                  migration});
            std::sort(migrations.begin(), migrations.end());
        }
        
        bool migrate(Stream& src, Stream& dest, uint16_t currentVersion, uint16_t targetVersion) {
            // Find migration path
            std::vector<Migration*> path = findMigrationPath(currentVersion, targetVersion);
            if (path.empty()) return false;
            
            // Apply migrations sequentially
            for (Migration* migration : path) {
                if (!migration->migrate(src, dest)) return false;
            }
            
            return true;
        }
        
        ~Migrator() {
            for (auto& entry : migrations) {
                delete entry.migration;
            }
        }
        
    private:
        std::vector<Migration*> findMigrationPath(uint16_t from, uint16_t to) {
            // Simplified: find direct migration or chain
            std::vector<Migration*> result;
            // Implementation would use graph search
            return result;
        }
    };
}

#endif // STRUCT_BUILDER_H
