#ifndef BLOOM_FILTER_H
#define BLOOM_FILTER_H

/*
 * BloomFilter.h — вероятностный фильтр Блума для ESP8266/ESP32
 *
 * Особенности:
 *   - Аллокатор выбирается в compile-time: DRAM или PSRAM (ESP32)
 *   - k хеш-функций на основе двойного хеширования (Kirsch–Mitzenmacher)
 *     → только 2 реальных хеша, k комбинаций — без потери качества
 *   - False Positive Rate ≈ (1 - e^(-kn/m))^k
 *     При m=4096 бит (512 байт), k=5, n=500:  FPR ≈ 2.2%
 *     При m=8192 бит (1024 байт), k=6, n=500: FPR ≈ 0.3%
 *   - Поддержка сериализации в файл (save/load)
 *   - Статический и динамический размер
 */

#include <Arduino.h>
#include <LittleFS.h>
#include <string.h>

// ── Детект PSRAM ─────────────────────────────────────────────────────────────
#if defined(ESP32)
  #include <esp_heap_caps.h>
  #define HAS_PSRAM_SUPPORT 1
#else
  #define HAS_PSRAM_SUPPORT 0
#endif

// ── Аллокатор ────────────────────────────────────────────────────────────────
namespace bloom_alloc {

enum class MemType : uint8_t {
    DRAM  = 0,  // обычная RAM (ESP8266 и ESP32)
    PSRAM = 1,  // PSRAM только на ESP32; при отсутствии fallback → DRAM
};

inline uint8_t* alloc(size_t bytes, MemType mem) {
#if HAS_PSRAM_SUPPORT
    if (mem == MemType::PSRAM) {
        void* p = heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (p) return static_cast<uint8_t*>(p);
        // Fallback на DRAM если PSRAM нет/нет места
    }
#else
    (void)mem;
#endif
    return new uint8_t[bytes];
}

inline void dealloc(uint8_t* p, MemType mem) {
    if (!p) return;
#if HAS_PSRAM_SUPPORT
    if (mem == MemType::PSRAM) {
        heap_caps_free(p);
        return;
    }
#else
    (void)mem;
#endif
    delete[] p;
}

} // namespace bloom_alloc

// ────────────────────────────────────────────────────────────────────────────
// Bloom Filter
// ────────────────────────────────────────────────────────────────────────────
class BloomFilter {
public:
    // Рекомендуемые параметры для заданного N и желаемого FPR
    struct Params {
        uint32_t bits;  // m — размер битового массива
        uint8_t  hashes; // k — кол-во хеш-функций

        // Оптимальные параметры: m = -n*ln(p)/(ln(2))^2, k = m/n * ln(2)
        static Params optimal(uint32_t n, float fpr = 0.01f) {
            float ln2 = 0.693147f;
            float m = -(float)n * logf(fpr) / (ln2 * ln2);
            float k = (m / (float)n) * ln2;
            uint32_t bits = (uint32_t)m;
            if (bits < 64) bits = 64;
            // Округлить до байта
            bits = (bits + 7) & ~7u;
            uint8_t hashes = (uint8_t)k;
            if (hashes < 1) hashes = 1;
            if (hashes > 16) hashes = 16;
            return {bits, hashes};
        }

        size_t bytes() const { return (bits + 7) / 8; }
    };

private:
    uint8_t*           _bits   = nullptr;
    uint32_t           _m      = 0;       // кол-во бит
    uint8_t            _k      = 0;       // кол-во хешей
    uint32_t           _count  = 0;       // кол-во добавленных элементов
    bloom_alloc::MemType _mem  = bloom_alloc::MemType::DRAM;

    // ── FNV-1a 32 бит ────────────────────────────────────────────────────────
    static uint32_t fnv1a(const uint8_t* data, size_t len, uint32_t seed = 2166136261u) {
        uint32_t h = seed;
        for (size_t i = 0; i < len; ++i) {
            h ^= data[i];
            h *= 16777619u;
        }
        return h;
    }

    // ── DJB2 ─────────────────────────────────────────────────────────────────
    static uint32_t djb2(const uint8_t* data, size_t len, uint32_t seed = 5381u) {
        uint32_t h = seed;
        for (size_t i = 0; i < len; ++i) {
            h = ((h << 5) + h) ^ data[i];
        }
        return h;
    }

    // ── Kirsch–Mitzenmacher: h_i = h1 + i*h2 ────────────────────────────────
    void _setBit(uint32_t idx) {
        idx %= _m;
        _bits[idx >> 3] |= (1u << (idx & 7));
    }

    bool _testBit(uint32_t idx) const {
        idx %= _m;
        return (_bits[idx >> 3] >> (idx & 7)) & 1u;
    }

public:
    BloomFilter() = default;

    // bits_m — размер в битах, hashes_k — кол-во хеш-функций
    explicit BloomFilter(uint32_t bits_m, uint8_t hashes_k,
                         bloom_alloc::MemType mem = bloom_alloc::MemType::DRAM)
        : _m(bits_m), _k(hashes_k), _mem(mem)
    {
        size_t bytes = (_m + 7) / 8;
        _bits = bloom_alloc::alloc(bytes, _mem);
        if (_bits) memset(_bits, 0, bytes);
    }

    explicit BloomFilter(Params p,
                         bloom_alloc::MemType mem = bloom_alloc::MemType::DRAM)
        : BloomFilter(p.bits, p.hashes, mem) {}

    ~BloomFilter() {
        if (_bits) bloom_alloc::dealloc(_bits, _mem);
    }

    // Non-copyable, movable
    BloomFilter(const BloomFilter&) = delete;
    BloomFilter& operator=(const BloomFilter&) = delete;

    BloomFilter(BloomFilter&& o) noexcept
        : _bits(o._bits), _m(o._m), _k(o._k),
          _count(o._count), _mem(o._mem)
    {
        o._bits = nullptr; o._m = 0; o._k = 0; o._count = 0;
    }

    BloomFilter& operator=(BloomFilter&& o) noexcept {
        if (this != &o) {
            if (_bits) bloom_alloc::dealloc(_bits, _mem);
            _bits = o._bits; _m = o._m; _k = o._k;
            _count = o._count; _mem = o._mem;
            o._bits = nullptr; o._m = 0; o._k = 0; o._count = 0;
        }
        return *this;
    }

    bool isValid() const { return _bits != nullptr && _m > 0; }

    // ── Добавить произвольный ключ ───────────────────────────────────────────
    void add(const uint8_t* data, size_t len) {
        if (!isValid()) return;
        uint32_t h1 = fnv1a(data, len);
        uint32_t h2 = djb2(data,  len);
        for (uint8_t i = 0; i < _k; ++i) {
            _setBit(h1 + (uint32_t)i * h2);
        }
        _count++;
    }

    template<typename T>
    void add(const T& value) {
        add(reinterpret_cast<const uint8_t*>(&value), sizeof(T));
    }

    void add(const String& s) {
        add(reinterpret_cast<const uint8_t*>(s.c_str()), s.length());
    }

    // ── Проверить наличие ────────────────────────────────────────────────────
    // Возвращает false → элемента ТОЧНО нет
    // Возвращает true  → элемент ВЕРОЯТНО есть (возможен false positive)
    bool mayContain(const uint8_t* data, size_t len) const {
        if (!isValid()) return true; // safe default
        uint32_t h1 = fnv1a(data, len);
        uint32_t h2 = djb2(data,  len);
        for (uint8_t i = 0; i < _k; ++i) {
            if (!_testBit(h1 + (uint32_t)i * h2)) return false;
        }
        return true;
    }

    template<typename T>
    bool mayContain(const T& value) const {
        return mayContain(reinterpret_cast<const uint8_t*>(&value), sizeof(T));
    }

    bool mayContain(const String& s) const {
        return mayContain(reinterpret_cast<const uint8_t*>(s.c_str()), s.length());
    }

    // ── Сброс ────────────────────────────────────────────────────────────────
    void reset() {
        if (_bits) memset(_bits, 0, (_m + 7) / 8);
        _count = 0;
    }

    // ── Статистика ───────────────────────────────────────────────────────────
    uint32_t count()     const { return _count; }
    uint32_t bits()      const { return _m; }
    uint8_t  hashes()    const { return _k; }
    size_t   memBytes()  const { return (_m + 7) / 8; }
    bloom_alloc::MemType memType() const { return _mem; }

    // Приблизительный FPR при текущем заполнении
    float estimatedFPR() const {
        if (_m == 0 || _k == 0) return 1.0f;
        float exponent = -((float)_k * (float)_count) / (float)_m;
        float p = powf(1.0f - expf(exponent), (float)_k);
        return p;
    }

    // ── Персистентность ──────────────────────────────────────────────────────
    // Формат: [magic 2B][m 4B][k 1B][count 4B][bits...]
    static constexpr uint16_t MAGIC = 0xBF01;

    bool save(const char* filename) const {
        if (!isValid()) return false;
        File f = LittleFS.open(filename, "w");
        if (!f) return false;

        uint16_t magic = MAGIC;
        f.write((uint8_t*)&magic,   sizeof(magic));
        f.write((uint8_t*)&_m,      sizeof(_m));
        f.write(&_k,                sizeof(_k));
        f.write((uint8_t*)&_count,  sizeof(_count));
        size_t bytes = (_m + 7) / 8;
        f.write(_bits, bytes);
        f.close();
        return true;
    }

    bool load(const char* filename) {
        File f = LittleFS.open(filename, "r");
        if (!f) return false;

        uint16_t magic;
        if (f.read((uint8_t*)&magic, sizeof(magic)) != sizeof(magic) || magic != MAGIC) {
            f.close(); return false;
        }

        uint32_t m; uint8_t k; uint32_t cnt;
        f.read((uint8_t*)&m,   sizeof(m));
        f.read(&k,             sizeof(k));
        f.read((uint8_t*)&cnt, sizeof(cnt));

        // Переаллоцировать если размер изменился
        size_t newBytes = (m + 7) / 8;
        size_t oldBytes = (_m + 7) / 8;
        if (!_bits || m != _m) {
            if (_bits) bloom_alloc::dealloc(_bits, _mem);
            _bits = bloom_alloc::alloc(newBytes, _mem);
        }
        _m = m; _k = k; _count = cnt;

        if (!_bits) { f.close(); return false; }
        bool ok = (f.read(_bits, newBytes) == newBytes);
        f.close();
        return ok;
    }
};

#endif // BLOOM_FILTER_H
