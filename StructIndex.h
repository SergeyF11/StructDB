#ifndef STRUCT_INDEX_H
#define STRUCT_INDEX_H

/*
 * StructIndex.h — индексы для StructCollection (ESP8266/ESP32 / LittleFS)
 *
 * IndexBase   — абстрактный интерфейс (работает с void* для хранения в векторе)
 * StructIndex — шаблонная реализация на std::map<KeyType, uint32_t>
 *               хранит payloadOffset (смещение начала пейлоада в файле данных)
 *
 * Особенности:
 *   - Специализации writeKey/readKey для String вынесены в отдельные inline-функции
 *     (избегаем проблем с частичной специализацией методов)
 *   - buildIndex умеет парсить новый формат слотов (маркер 0xAA/0xDD + size)
 *   - findRange возвращает отсортированный вектор смещений
 *   - Поддержка мультиключей через multimap (опционально)
 */

#include "StructBuilder.h"
#include <LittleFS.h>
#include <map>
#include <vector>
#include <functional>

// Маркеры слотов (повторяем, чтобы не тянуть StructCollection.h)
static constexpr uint8_t _IDX_SLOT_ACTIVE  = 0xAA;
static constexpr uint8_t _IDX_SLOT_DELETED = 0xDD;

// ────────────────────────────────────────────────────────────────────────────
// Абстрактный базовый класс
// ────────────────────────────────────────────────────────────────────────────
class IndexBase {
public:
    virtual ~IndexBase() = default;

    // Вставка/удаление; record — указатель на экземпляр StructType, приведённый к void*
    // offset — payloadOffset (начало пейлоада, НЕ начало слота)
    virtual void   insert(const void* record, uint32_t offset) = 0;
    virtual void   remove(const void* record) = 0;

    // Персистентность
    virtual bool   save(const char* filename) const = 0;
    virtual bool   load(const char* filename) = 0;

    // Полное перестроение по открытому файлу данных (новый формат слотов)
    virtual void   buildIndex(File& dataFile) = 0;

    // Поиск; key приводится к KeyType внутри реализации
    // Возвращает payloadOffset или UINT32_MAX если не найдено
    virtual uint32_t find(const void* key) const = 0;

    // Кол-во элементов
    virtual size_t size() const = 0;

    // Грязный флаг
    virtual bool   isDirty() const = 0;
    virtual void   clearDirty() = 0;
    virtual void   clear() = 0;
};

// ────────────────────────────────────────────────────────────────────────────
// Вспомогательные функции сериализации ключа (не методы класса!)
// По умолчанию — POD-тип (memcpy)
// ────────────────────────────────────────────────────────────────────────────
template<typename K>
bool indexWriteKey(File& f, const K& key) {
    return f.write((const uint8_t*)&key, sizeof(K)) == sizeof(K);
}

template<typename K>
bool indexReadKey(File& f, K& key) {
    return f.read((uint8_t*)&key, sizeof(K)) == sizeof(K);
}

// Специализация для String
template<>
inline bool indexWriteKey<String>(File& f, const String& key) {
    uint8_t len = (key.length() > 255) ? 255 : (uint8_t)key.length();
    if (f.write(&len, 1) != 1) return false;
    return f.write((const uint8_t*)key.c_str(), len) == len;
}

template<>
inline bool indexReadKey<String>(File& f, String& key) {
    uint8_t len;
    if (f.read(&len, 1) != 1) return false;
    char buf[256];
    if (f.read((uint8_t*)buf, len) != len) return false;
    buf[len] = '\0';
    key = String(buf);
    return true;
}

// ────────────────────────────────────────────────────────────────────────────
// Шаблонная реализация индекса
// ────────────────────────────────────────────────────────────────────────────
template<typename KeyType, typename StructType>
class StructIndex : public IndexBase {
public:
    using Extractor = std::function<KeyType(const StructType&)>;

private:
    std::map<KeyType, uint32_t> _map;   // key -> payloadOffset
    Extractor                   _extr;
    mutable bool                _dirty = false;

public:
    explicit StructIndex(Extractor extractor)
        : _extr(std::move(extractor)) {}

    // ── IndexBase ────────────────────────────────────────────────────────────

    void insert(const void* record, uint32_t offset) override {
        const StructType* r = static_cast<const StructType*>(record);
        _map[_extr(*r)] = offset;
        _dirty = true;
    }

    void remove(const void* record) override {
        const StructType* r = static_cast<const StructType*>(record);
        KeyType key = _extr(*r);
        auto it = _map.find(key);
        if (it != _map.end()) {
            _map.erase(it);
            _dirty = true;
        }
    }

    bool save(const char* filename) const override {
        // Всегда сохраняем, чтобы гарантировать синхронизацию после rebuildIndex
        File f = LittleFS.open(filename, "w");
        if (!f) return false;

        uint32_t count = (uint32_t)_map.size();
        if (f.write((uint8_t*)&count, sizeof(count)) != sizeof(count)) {
            f.close(); return false;
        }

        for (const auto& pair : _map) {
            if (!indexWriteKey<KeyType>(f, pair.first)) { f.close(); return false; }
            uint32_t off = pair.second;
            if (f.write((uint8_t*)&off, sizeof(off)) != sizeof(off)) {
                f.close(); return false;
            }
        }
        f.close();
        _dirty = false;
        return true;
    }

    bool load(const char* filename) override {
        File f = LittleFS.open(filename, "r");
        if (!f) return false;

        uint32_t count;
        if (f.read((uint8_t*)&count, sizeof(count)) != sizeof(count)) {
            f.close(); return false;
        }

        _map.clear();
        for (uint32_t i = 0; i < count; ++i) {
            KeyType  key;
            uint32_t off;
            if (!indexReadKey<KeyType>(f, key)) { f.close(); return false; }
            if (f.read((uint8_t*)&off, sizeof(off)) != sizeof(off)) {
                f.close(); return false;
            }
            _map[key] = off;
        }
        f.close();
        _dirty = false;
        return true;
    }

    // Строит индекс, сканируя файл данных в новом формате слотов
    void buildIndex(File& dataFile) override {
        _map.clear();

        // Читаем заголовок, чтобы найти dataEnd
        // Заголовок файла: signature[4] version[2] schemaVersion[2]
        //                  recordCount[4] deletedCount[4] dataEnd[4] pad[4] = 24 байта
        struct _Hdr {
            char     sig[4];
            uint16_t ver, schema;
            uint32_t recCnt, delCnt, dataEnd;
            uint8_t  pad[4];
        } hdr;

        uint32_t savedPos = dataFile.position();
        dataFile.seek(0);
        if (dataFile.read((uint8_t*)&hdr, sizeof(hdr)) != sizeof(hdr)) {
            dataFile.seek(savedPos); return;
        }

        uint32_t dataEnd = hdr.dataEnd;
        uint32_t pos     = sizeof(hdr);

        while (pos < dataEnd) {
            dataFile.seek(pos);
            uint8_t  marker;
            uint32_t sz;
            if (dataFile.read(&marker, 1) != 1) break;
            if (dataFile.read((uint8_t*)&sz, 4) != 4) break;

            uint32_t payloadOffset = pos + 5;

            if (marker == _IDX_SLOT_ACTIVE) {
                StructType rec;
                if (rec.readFrom(dataFile)) {
                    _map[_extr(rec)] = payloadOffset;
                }
            }

            pos += 5 + sz;
        }

        dataFile.seek(savedPos);
        _dirty = true;
    }

    uint32_t find(const void* key) const override {
        const KeyType* k = static_cast<const KeyType*>(key);
        auto it = _map.find(*k);
        return (it != _map.end()) ? it->second : UINT32_MAX;
    }

    size_t size() const override { return _map.size(); }

    bool isDirty() const override { return _dirty; }
    void clearDirty() override    { _dirty = false; }
    void clear() override         { _map.clear(); _dirty = true; }

    // ── Удобные перегрузки без void* ────────────────────────────────────────

    uint32_t find(const KeyType& key) const {
        auto it = _map.find(key);
        return (it != _map.end()) ? it->second : UINT32_MAX;
    }

    // Диапазонный поиск — возвращает вектор payloadOffset в порядке ключей
    std::vector<uint32_t> findRange(const KeyType& minKey, const KeyType& maxKey) const {
        std::vector<uint32_t> result;
        auto it  = _map.lower_bound(minKey);
        auto end = _map.upper_bound(maxKey);
        for (; it != end; ++it) result.push_back(it->second);
        return result;
    }

    // Все смещения (для полного обхода через индекс)
    std::vector<uint32_t> allOffsets() const {
        std::vector<uint32_t> result;
        result.reserve(_map.size());
        for (const auto& p : _map) result.push_back(p.second);
        return result;
    }

    // Обновить смещение для существующего ключа (используется при in-place update, если возможен)
    bool updateOffset(const KeyType& key, uint32_t newOffset) {
        auto it = _map.find(key);
        if (it == _map.end()) return false;
        it->second = newOffset;
        _dirty = true;
        return true;
    }

    // Прямой доступ к map (только чтение) — для отладки / дампа
    const std::map<KeyType, uint32_t>& rawMap() const { return _map; }
};

// ── Удобные псевдонимы ───────────────────────────────────────────────────────
template<typename StructType>
using StringIndex = StructIndex<String,   StructType>;

template<typename StructType>
using IntIndex    = StructIndex<int32_t,  StructType>;

template<typename StructType>
using UInt32Index = StructIndex<uint32_t, StructType>;

template<typename StructType>
using UInt64Index = StructIndex<uint64_t, StructType>;

#endif // STRUCT_INDEX_H