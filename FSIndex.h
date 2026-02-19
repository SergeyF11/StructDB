#ifndef FS_INDEX_H
#define FS_INDEX_H

/*
 * FSIndex.h — индекс в файловой системе без полной загрузки в RAM
 *
 * Архитектура:
 *   ┌─────────────────────────────────────────────────────┐
 *   │  RAM                                                │
 *   │  ┌──────────────┐   ┌──────────────────────────┐   │
 *   │  │ BloomFilter  │   │  Page Cache (N страниц)   │   │
 *   │  │ (512-1024 B) │   │  (DRAM или PSRAM)         │   │
 *   │  └──────────────┘   └──────────────────────────┘   │
 *   └─────────────────────────────────────────────────────┘
 *             │                        │
 *             ▼ false → miss           ▼ page hit/miss
 *   ┌─────────────────────────────────────────────────────┐
 *   │  LittleFS: sortedIndex.idx                         │
 *   │  [Header][Entry*] — отсортированный массив записей │
 *   └─────────────────────────────────────────────────────┘
 *
 * Формат файла индекса:
 *   [FSIdxHeader 16 байт]
 *   [Entry * count]  — каждый Entry: [keyBytes][uint32 offset]
 *
 * Ключи фиксированной длины (POD-типы) или переменной (String, до 255 байт).
 * Для переменных ключей каждый Entry: [uint8 keyLen][key bytes][uint32 offset].
 *
 * Поиск: бинарный по файлу (O(log N) чтений страниц).
 * Вставка/удаление: накапливаются в RAM-буфере (_dirty list),
 *   при превышении порога или явном вызове flush() — выполняется
 *   слияние (merge) с файлом (временный файл + rename).
 *
 * Страничный кэш: LRU, каждая страница = PAGE_SIZE байт.
 *   Позволяет переиспользовать прочитанные данные при бинарном поиске.
 *
 * PSRAM: кэш страниц и BloomFilter аллоцируются в PSRAM если указан режим.
 */

#include "BloomFilter.h"
#include "StructBuilder.h"    // File, LittleFS, etc.
#include <LittleFS.h>
#include <functional>
#include <vector>
#include <algorithm>

// ── Вспомогательные функции сериализации ключей (из StructIndex.h) ──────────
// Дублируем объявления чтобы FSIndex.h был самодостаточен
template<typename K>
bool fsidxWriteKey(File& f, const K& key) {
    return f.write((const uint8_t*)&key, sizeof(K)) == sizeof(K);
}
template<typename K>
bool fsidxReadKey(File& f, K& key) {
    return f.read((uint8_t*)&key, sizeof(K)) == sizeof(K);
}
template<> inline bool fsidxWriteKey<String>(File& f, const String& key) {
    uint8_t len = key.length() > 255 ? 255 : (uint8_t)key.length();
    if (f.write(&len, 1) != 1) return false;
    return f.write((const uint8_t*)key.c_str(), len) == len;
}
template<> inline bool fsidxReadKey<String>(File& f, String& key) {
    uint8_t len;
    if (f.read(&len, 1) != 1) return false;
    char buf[256]; buf[len] = '\0';
    if (f.read((uint8_t*)buf, len) != len) return false;
    key = String(buf);
    return true;
}

// ── Размер записи в файле ────────────────────────────────────────────────────
// POD: sizeof(K) + 4
// String: 1 + len + 4 (переменный — для строк используем специализацию)
template<typename K>
struct FSIdxEntrySize {
    static constexpr bool IS_FIXED = true;
    static constexpr size_t KEY_SIZE = sizeof(K);
    static constexpr size_t ENTRY_SIZE = sizeof(K) + sizeof(uint32_t);
    static size_t entrySize(const K&) { return ENTRY_SIZE; }
};
template<>
struct FSIdxEntrySize<String> {
    static constexpr bool IS_FIXED = false;
    static constexpr size_t KEY_SIZE = 0;
    static size_t entrySize(const String& k) {
        return 1 + (k.length() > 255 ? 255 : k.length()) + sizeof(uint32_t);
    }
};

// ── Заголовок файла индекса ───────────────────────────────────────────────────
struct FSIdxHeader {
    char     sig[4];      // "FSIX"
    uint32_t count;       // кол-во записей
    uint8_t  keyType;     // 0 = POD фиксированный, 1 = String
    uint8_t  keySize;     // для POD: sizeof(key), для String: 0
    uint8_t  _pad[2];
    // Итого: 12 байт, выровнено
};
static_assert(sizeof(FSIdxHeader) == 12, "FSIdxHeader size");

// ── Параметры кэша ────────────────────────────────────────────────────────────
struct FSIdxConfig {
    size_t   pageSize    = 256;   // байт на страницу кэша
    size_t   cachePages  = 4;     // кол-во страниц в кэше
    size_t   dirtyLimit  = 32;    // макс. pending-правок до flush
    bloom_alloc::MemType bloomMem = bloom_alloc::MemType::DRAM;
    bloom_alloc::MemType cacheMem = bloom_alloc::MemType::DRAM;
    float    bloomFPR    = 0.02f; // целевой FPR для bloom-фильтра
    uint32_t bloomN      = 500;   // ожидаемое кол-во элементов
};

// ────────────────────────────────────────────────────────────────────────────
// FSIndex — основной класс
// ────────────────────────────────────────────────────────────────────────────
template<typename KeyType, typename StructType>
class FSIndex : public IndexBase {
public:
    using Extractor = std::function<KeyType(const StructType&)>;

    // Типаж для размеров
    using ESize = FSIdxEntrySize<KeyType>;

private:
    // ── Pending (dirty) запись ────────────────────────────────────────────────
    enum class PendingOp : uint8_t { INSERT, REMOVE };
    struct Pending {
        KeyType   key;
        uint32_t  offset;   // payloadOffset; для REMOVE не важен
        PendingOp op;
    };

    // ── Страница кэша ─────────────────────────────────────────────────────────
    struct CachePage {
        uint32_t fileOffset = UINT32_MAX;  // смещение в файле (UINT32_MAX = пусто)
        uint8_t* data       = nullptr;
        uint32_t lruAge     = 0;
        bool     valid      = false;
    };

    // ── Состояние ─────────────────────────────────────────────────────────────
    String               _fname;
    FSIdxConfig          _cfg;
    Extractor            _extr;

    BloomFilter          _bloom;
    std::vector<Pending> _pending;   // накопленные правки
    uint32_t             _count   = 0;  // записей в файле (из header)
    mutable bool         _dirty   = false;

    // Страничный кэш
    CachePage*           _cache   = nullptr;
    size_t               _numCachePages = 0;
    mutable uint32_t     _lruClock = 0;

    // ── Аллокатор кэша ────────────────────────────────────────────────────────
    void _allocCache() {
        _numCachePages = _cfg.cachePages;
        _cache = new CachePage[_numCachePages];
        for (size_t i = 0; i < _numCachePages; ++i) {
            _cache[i].data = bloom_alloc::alloc(_cfg.pageSize, _cfg.cacheMem);
            _cache[i].valid = false;
            _cache[i].fileOffset = UINT32_MAX;
            _cache[i].lruAge = 0;
        }
    }

    void _freeCache() {
        if (!_cache) return;
        for (size_t i = 0; i < _numCachePages; ++i) {
            if (_cache[i].data)
                bloom_alloc::dealloc(_cache[i].data, _cfg.cacheMem);
        }
        delete[] _cache;
        _cache = nullptr;
        _numCachePages = 0;
    }

    void _invalidateCache() {
        for (size_t i = 0; i < _numCachePages; ++i) {
            _cache[i].valid = false;
            _cache[i].fileOffset = UINT32_MAX;
        }
    }

    // ── Получить страницу из кэша (LRU eviction) ─────────────────────────────
    // fileOffset — смещение начала страницы в файле.
    // Возвращает указатель на данные страницы или nullptr при ошибке.
    const uint8_t* _fetchPage(File& f, uint32_t fileOffset) const {
        ++_lruClock;

        // Ищем в кэше
        for (size_t i = 0; i < _numCachePages; ++i) {
            if (_cache[i].valid && _cache[i].fileOffset == fileOffset) {
                _cache[i].lruAge = _lruClock;
                return _cache[i].data;
            }
        }

        // LRU eviction: найдём старейшую страницу
        size_t victim = 0;
        for (size_t i = 1; i < _numCachePages; ++i) {
            if (_cache[i].lruAge < _cache[victim].lruAge) victim = i;
        }

        // Загружаем
        if (!f.seek(fileOffset)) return nullptr;
        size_t toRead = _cfg.pageSize;
        size_t actual = f.read(_cache[victim].data, toRead);
        if (actual == 0) return nullptr;

        _cache[victim].fileOffset = fileOffset;
        _cache[victim].lruAge     = _lruClock;
        _cache[victim].valid      = true;
        return _cache[victim].data;
    }

    // ── Читать Entry из файла по позиции (через кэш) ─────────────────────────
    // Для фиксированных ключей: key|offset по pageOffset
    // Для строк: не кэшируется (переменный размер) — прямое чтение
    bool _readEntryAt(File& f, uint32_t filePos, KeyType& key, uint32_t& offset) const {
        if constexpr (ESize::IS_FIXED) {
            uint32_t pageStart = (filePos / _cfg.pageSize) * _cfg.pageSize;
            uint32_t pageOff   = filePos - pageStart;

            const uint8_t* page = _fetchPage(f, pageStart);
            if (!page) return false;

            size_t entrySize = ESize::ENTRY_SIZE;

            // Убедиться что запись не пересекает страницу (упрощение: если да — прямое чтение)
            if (pageOff + entrySize > _cfg.pageSize) {
                if (!f.seek(filePos)) return false;
                return fsidxReadKey<KeyType>(f, key) &&
                       (f.read((uint8_t*)&offset, 4) == 4);
            }

            memcpy(&key, page + pageOff, sizeof(KeyType));
            memcpy(&offset, page + pageOff + sizeof(KeyType), 4);
            return true;
        } else {
            // String: прямое чтение без кэша (переменный размер)
            if (!f.seek(filePos)) return false;
            return fsidxReadKey<KeyType>(f, key) &&
                   (f.read((uint8_t*)&offset, 4) == 4);
        }
    }

    // ── Смещение N-й записи в файле (для фиксированных ключей — O(1)) ────────
    uint32_t _entryFilePos(uint32_t n) const {
        if constexpr (ESize::IS_FIXED) {
            return sizeof(FSIdxHeader) + n * ESize::ENTRY_SIZE;
        } else {
            // Для String нет O(1) — нужен скан (ограничение переменной длины)
            // Возвращаем UINT32_MAX, вызывающий должен использовать _scanToEntry
            return UINT32_MAX;
        }
    }

    // Для String: получить смещение N-й записи линейным сканом
    uint32_t _scanToEntryPos(File& f, uint32_t n) const {
        f.seek(sizeof(FSIdxHeader));
        for (uint32_t i = 0; i < n; ++i) {
            uint8_t klen;
            if (f.read(&klen, 1) != 1) return UINT32_MAX;
            if (!f.seek(f.position() + klen + 4)) return UINT32_MAX;
        }
        return f.position();
    }

    // ── Бинарный поиск в файле ────────────────────────────────────────────────
    // Возвращает payloadOffset или UINT32_MAX
    uint32_t _binarySearch(File& f, const KeyType& target) const {
        if (_count == 0) return UINT32_MAX;

        int32_t lo = 0, hi = (int32_t)_count - 1;

        while (lo <= hi) {
            int32_t mid = lo + (hi - lo) / 2;

            uint32_t pos;
            if constexpr (ESize::IS_FIXED) {
                pos = _entryFilePos((uint32_t)mid);
            } else {
                pos = _scanToEntryPos(f, (uint32_t)mid);
                if (pos == UINT32_MAX) return UINT32_MAX;
            }

            KeyType  midKey;
            uint32_t midOff;
            if (!_readEntryAt(f, pos, midKey, midOff)) return UINT32_MAX;

            if (midKey == target) return midOff;
            if (midKey < target) lo = mid + 1;
            else                 hi = mid - 1;
        }
        return UINT32_MAX;
    }

    // ── Загрузить заголовок файла ─────────────────────────────────────────────
    bool _readFileHeader(File& f) {
        if (!f.seek(0)) return false;
        FSIdxHeader hdr;
        if (f.read((uint8_t*)&hdr, sizeof(hdr)) != sizeof(hdr)) return false;
        if (memcmp(hdr.sig, "FSIX", 4) != 0) return false;
        _count = hdr.count;
        return true;
    }

    bool _writeFileHeader(File& f) {
        if (!f.seek(0)) return false;
        FSIdxHeader hdr;
        memcpy(hdr.sig, "FSIX", 4);
        hdr.count   = _count;
        hdr.keyType = ESize::IS_FIXED ? 0 : 1;
        hdr.keySize = (uint8_t)(ESize::IS_FIXED ? ESize::KEY_SIZE : 0);
        hdr._pad[0] = hdr._pad[1] = 0;
        return f.write((uint8_t*)&hdr, sizeof(hdr)) == sizeof(hdr);
    }

    // ── Применить pending список и пересоздать файл (merge) ──────────────────
    // Выполняется flush(): читаем старый файл + pending → сортируем → пишем новый
    bool _merge() {
        if (_pending.empty()) return true;

        // ── Шаг 1: читаем все записи из файла в вектор ─────────────────────
        struct Entry { KeyType key; uint32_t offset; };
        std::vector<Entry> entries;
        entries.reserve(_count + _pending.size());

        if (LittleFS.exists(_fname.c_str())) {
            File f = LittleFS.open(_fname.c_str(), "r");
            if (f) {
                FSIdxHeader hdr;
                f.read((uint8_t*)&hdr, sizeof(hdr));
                for (uint32_t i = 0; i < hdr.count; ++i) {
                    Entry e;
                    if (!fsidxReadKey<KeyType>(f, e.key)) break;
                    if (f.read((uint8_t*)&e.offset, 4) != 4) break;
                    entries.push_back(std::move(e));
                }
                f.close();
            }
        }

        // ── Шаг 2: применяем pending ─────────────────────────────────────────
        for (auto& p : _pending) {
            if (p.op == PendingOp::INSERT) {
                // Обновляем существующий или добавляем
                bool found = false;
                for (auto& e : entries) {
                    if (e.key == p.key) { e.offset = p.offset; found = true; break; }
                }
                if (!found) entries.push_back({p.key, p.offset});
            } else {
                // REMOVE
                entries.erase(
                    std::remove_if(entries.begin(), entries.end(),
                        [&](const Entry& e){ return e.key == p.key; }),
                    entries.end());
            }
        }

        // ── Шаг 3: сортируем ─────────────────────────────────────────────────
        std::sort(entries.begin(), entries.end(),
            [](const Entry& a, const Entry& b){ return a.key < b.key; });

        // ── Шаг 4: пишем во временный файл ───────────────────────────────────
        String tmpName = _fname + ".tmp";
        LittleFS.remove(tmpName.c_str());
        File tf = LittleFS.open(tmpName.c_str(), "w");
        if (!tf) return false;

        // placeholder header
        FSIdxHeader hdr;
        memset(&hdr, 0, sizeof(hdr));
        tf.write((uint8_t*)&hdr, sizeof(hdr));

        for (auto& e : entries) {
            fsidxWriteKey<KeyType>(tf, e.key);
            tf.write((uint8_t*)&e.offset, 4);
        }
        tf.close();

        // Записываем реальный заголовок
        tf = LittleFS.open(tmpName.c_str(), "r+");
        if (tf) {
            memcpy(hdr.sig, "FSIX", 4);
            hdr.count   = (uint32_t)entries.size();
            hdr.keyType = ESize::IS_FIXED ? 0 : 1;
            hdr.keySize = (uint8_t)(ESize::IS_FIXED ? ESize::KEY_SIZE : 0);
            tf.seek(0);
            tf.write((uint8_t*)&hdr, sizeof(hdr));
            tf.close();
        }

        // ── Шаг 5: rename ───────────────────────────────────────────────────
        LittleFS.remove(_fname.c_str());
        LittleFS.rename(tmpName.c_str(), _fname.c_str());

        _count = (uint32_t)entries.size();
        _pending.clear();
        _invalidateCache();

        // ── Шаг 6: перестроить Bloom-фильтр ─────────────────────────────────
        _bloom.reset();
        for (auto& e : entries) _bloomAdd(e.key);

        return true;
    }

    void _bloomAdd(const KeyType& key) {
        if constexpr (ESize::IS_FIXED) {
            _bloom.add(reinterpret_cast<const uint8_t*>(&key), sizeof(KeyType));
        } else {
            _bloom.add(key); // String overload
        }
    }

    bool _bloomMayContain(const KeyType& key) const {
        if constexpr (ESize::IS_FIXED) {
            return _bloom.mayContain(reinterpret_cast<const uint8_t*>(&key), sizeof(KeyType));
        } else {
            return _bloom.mayContain(key);
        }
    }

    // Проверить pending-список (перед обращением к файлу)
    uint32_t _findInPending(const KeyType& key) const {
        // Перебираем в обратном порядке (последняя правка приоритетнее)
        for (int i = (int)_pending.size() - 1; i >= 0; --i) {
            if (_pending[i].key == key) {
                return (_pending[i].op == PendingOp::INSERT)
                       ? _pending[i].offset
                       : UINT32_MAX;
            }
        }
        return UINT32_MAX - 1; // sentinel: "не найдено в pending"
    }

public:
    FSIndex(const char* filename, Extractor extr, const FSIdxConfig& cfg = {})
        : _fname(filename), _cfg(cfg), _extr(std::move(extr)),
          _bloom(BloomFilter::Params::optimal(cfg.bloomN, cfg.bloomFPR), cfg.bloomMem)
    {
        _allocCache();
        // Пробуем загрузить
        _loadMeta();
    }

    ~FSIndex() {
        flush();
        _freeCache();
    }

    // ── IndexBase ────────────────────────────────────────────────────────────

    void insert(const void* record, uint32_t offset) override {
        const StructType* r = static_cast<const StructType*>(record);
        KeyType key = _extr(*r);
        _pending.push_back({key, offset, PendingOp::INSERT});
        _bloomAdd(key);
        _dirty = true;
        if (_pending.size() >= _cfg.dirtyLimit) flush();
    }

    void remove(const void* record) override {
        const StructType* r = static_cast<const StructType*>(record);
        KeyType key = _extr(*r);
        _pending.push_back({key, 0, PendingOp::REMOVE});
        _dirty = true;
        // Bloom-фильтр не поддерживает удаление — после flush перестраивается
        if (_pending.size() >= _cfg.dirtyLimit) flush();
    }

    bool save(const char* /*filename*/) const override {
        // Для FSIndex "save" = flush pending + сохранить bloom
        const_cast<FSIndex*>(this)->flush();
        String bname = _fname + ".bloom";
        return _bloom.save(bname.c_str());
    }

    bool load(const char* /*filename*/) override {
        return _loadMeta();
    }

    void buildIndex(File& dataFile) override {
        // Полное перестроение: читаем все активные слоты из dataFile
        struct _Hdr {
            char sig[4]; uint16_t v, s;
            uint32_t recCnt, delCnt, dataEnd;
            uint8_t pad[4];
        } dh;

        uint32_t saved = dataFile.position();
        dataFile.seek(0);
        if (dataFile.read((uint8_t*)&dh, sizeof(dh)) != sizeof(dh)) {
            dataFile.seek(saved); return;
        }

        // Очищаем всё
        _pending.clear();
        _bloom.reset();
        LittleFS.remove(_fname.c_str());
        _count = 0;
        _invalidateCache();

        uint32_t pos = sizeof(dh);
        while (pos < dh.dataEnd) {
            dataFile.seek(pos);
            uint8_t marker; uint32_t sz;
            if (dataFile.read(&marker, 1) != 1) break;
            if (dataFile.read((uint8_t*)&sz, 4) != 4) break;
            uint32_t payloadOff = pos + 5;

            if (marker == 0xAA) {
                StructType rec;
                if (rec.readFrom(dataFile)) {
                    KeyType key = _extr(rec);
                    _pending.push_back({key, payloadOff, PendingOp::INSERT});
                    _bloomAdd(key);
                    if (_pending.size() >= 64) {
                        flush(); // периодически сбрасываем чтобы не переполнить RAM
                        yield();
                    }
                }
            }
            pos += 5 + sz;
        }
        flush();
        dataFile.seek(saved);
        _dirty = true;
    }

    uint32_t find(const void* key) const override {
        const KeyType* k = static_cast<const KeyType*>(key);
        return find(*k);
    }

    size_t size() const override { return _count + _pendingInsertCount(); }

    bool isDirty() const override { return _dirty; }
    void clearDirty() override    { _dirty = false; }
    void clear() override {
        _pending.clear();
        _bloom.reset();
        _count = 0;
        _invalidateCache();
        LittleFS.remove(_fname.c_str());
        _dirty = true;
    }

    // ── Публичный поиск ──────────────────────────────────────────────────────

    // Возвращает payloadOffset или UINT32_MAX
    uint32_t find(const KeyType& key) const {
        // 1. Bloom-фильтр: быстрый отказ
        if (!_bloomMayContain(key)) return UINT32_MAX;

        // 2. Проверить pending (может содержать INSERT/REMOVE поверх файла)
        uint32_t pendingResult = _findInPending(key);
        if (pendingResult != UINT32_MAX - 1) return pendingResult; // найдено в pending

        // 3. Бинарный поиск в файле
        if (_count == 0) return UINT32_MAX;
        if (!LittleFS.exists(_fname.c_str())) return UINT32_MAX;

        File f = LittleFS.open(_fname.c_str(), "r");
        if (!f) return UINT32_MAX;

        FSIdxHeader hdr;
        f.read((uint8_t*)&hdr, sizeof(hdr));

        uint32_t result = _binarySearch(f, key);
        f.close();
        return result;
    }

    // Диапазонный поиск — линейный скан файла (O(N) в худшем случае)
    // Возвращает вектор payloadOffset в порядке ключей
    std::vector<uint32_t> findRange(const KeyType& minKey, const KeyType& maxKey) const {
        std::vector<uint32_t> result;

        // Из файла
        if (_count > 0 && LittleFS.exists(_fname.c_str())) {
            File f = LittleFS.open(_fname.c_str(), "r");
            if (f) {
                FSIdxHeader hdr;
                f.read((uint8_t*)&hdr, sizeof(hdr));

                if constexpr (ESize::IS_FIXED) {
                    // Бинарный поиск начала диапазона
                    int32_t lo = 0, hi = (int32_t)hdr.count - 1, start = (int32_t)hdr.count;
                    while (lo <= hi) {
                        int32_t mid = lo + (hi - lo) / 2;
                        uint32_t pos = sizeof(FSIdxHeader) + mid * ESize::ENTRY_SIZE;
                        KeyType mk; uint32_t mo;
                        _readEntryAt(f, pos, mk, mo);
                        if (mk < minKey) lo = mid + 1;
                        else { start = mid; hi = mid - 1; }
                    }
                    // Линейный скан от start
                    for (uint32_t i = (uint32_t)start; i < hdr.count; ++i) {
                        uint32_t pos = sizeof(FSIdxHeader) + i * ESize::ENTRY_SIZE;
                        KeyType mk; uint32_t mo;
                        if (!_readEntryAt(f, pos, mk, mo)) break;
                        if (mk > maxKey) break;
                        result.push_back(mo);
                    }
                } else {
                    // String: линейный скан всего файла
                    for (uint32_t i = 0; i < hdr.count; ++i) {
                        KeyType mk; uint32_t mo;
                        if (!fsidxReadKey<KeyType>(f, mk)) break;
                        if (f.read((uint8_t*)&mo, 4) != 4) break;
                        if (mk >= minKey && mk <= maxKey) result.push_back(mo);
                        if (mk > maxKey) break;
                    }
                }
                f.close();
            }
        }

        // Добавляем из pending (без дублей)
        for (auto& p : _pending) {
            if (p.op == PendingOp::INSERT && p.key >= minKey && p.key <= maxKey) {
                // Проверить не перекрыт ли уже из файла
                bool dup = false;
                for (auto off : result) if (off == p.offset) { dup = true; break; }
                if (!dup) result.push_back(p.offset);
            }
        }

        return result;
    }

    // Принудительный flush pending → файл
    bool flush() {
        if (_pending.empty()) return true;
        bool ok = _merge();
        if (ok) {
            _dirty = !_pending.empty(); // после merge pending пуст
            // Сохраняем bloom
            String bname = _fname + ".bloom";
            _bloom.save(bname.c_str());
        }
        return ok;
    }

    // ── Статистика ───────────────────────────────────────────────────────────
    size_t   pendingCount()  const { return _pending.size(); }
    uint32_t fileCount()     const { return _count; }
    float    bloomFPR()      const { return _bloom.estimatedFPR(); }
    size_t   bloomBytes()    const { return _bloom.memBytes(); }
    size_t   cacheBytes()    const { return _numCachePages * _cfg.pageSize; }
    size_t   totalRamBytes() const { return bloomBytes() + cacheBytes() + _pending.size() * sizeof(Pending); }

    const BloomFilter& bloom() const { return _bloom; }

private:
    size_t _pendingInsertCount() const {
        size_t n = 0;
        for (auto& p : _pending) if (p.op == PendingOp::INSERT) n++;
        return n;
    }

    bool _loadMeta() {
        // Загрузить bloom
        String bname = _fname + ".bloom";
        if (LittleFS.exists(bname.c_str())) {
            _bloom.load(bname.c_str());
        }
        // Прочитать count из файла
        if (!LittleFS.exists(_fname.c_str())) { _count = 0; return true; }
        File f = LittleFS.open(_fname.c_str(), "r");
        if (!f) return false;
        bool ok = _readFileHeader(f);
        f.close();
        return ok;
    }
};

// ── Удобные псевдонимы ───────────────────────────────────────────────────────
template<typename StructType>
using FSStringIndex = FSIndex<String,   StructType>;

template<typename StructType>
using FSUInt32Index = FSIndex<uint32_t, StructType>;

template<typename StructType>
using FSUInt64Index = FSIndex<uint64_t, StructType>;

template<typename StructType>
using FSInt32Index  = FSIndex<int32_t,  StructType>;

#endif // FS_INDEX_H
