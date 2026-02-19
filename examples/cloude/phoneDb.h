/*
 * PackedPhoneDB.h — пример использования PackedPhone как поля записи
 * с индексированием ТОЛЬКО по номеру телефона (без flags/group)
 * =========================================================================
 *
 * Суть трюка:
 *   PackedPhone хранит в одном uint64_t три поля:
 *     [50 бит: num][4 бит: flags][10 бит: group]
 *
 *   В файле данных запись хранится как UInt64Entry (raw uint64_t).
 *   Индекс строится по phone() — только цифровому номеру (50 бит),
 *   игнорируя flags и group. Это позволяет:
 *     - найти запись по номеру независимо от её группы/флагов
 *     - обновить только flags/group без смены ключа
 *     - хранить в одном поле всё что нужно (8 байт вместо 8+1+2=11)
 *
 * Структура записи:
 *   [UInt64Entry: PackedPhone raw]  — основное поле (phone+flags+group)
 *   [StringEntry: description]          — комментарий (опционально)
 *
 * Индексы:
 *   phoneIdx: uint64_t → key = PackedPhone(raw).phone()  (только номер)
 *   nameIdx:  String   → key = name
 */

#pragma once
#include "StructCollection.h"
#include "StructIndex.h"     // RAM-индекс (для небольших коллекций)
#include "FSIndex.h"         // FS-индекс  (для больших коллекций)
#include "CompactPhone.h"

// ─────────────────────────────────────────────────────────────────────────────
// 1. Тип записи
// ─────────────────────────────────────────────────────────────────────────────
//
//  field 0: UInt64Entry  — PackedPhone.raw()  (phone + flags + group упакованы)
//  field 1: StringEntry  — комментарий
//
using PhoneRecord = Struct2<UInt64Entry, StringEntry>;

// ─────────────────────────────────────────────────────────────────────────────
// 2. Экстракторы ключей
// ─────────────────────────────────────────────────────────────────────────────

// Ключ для phoneIdx: только цифровой номер (phone()), игнорируем flags/group
//   PackedPhone хранится как raw uint64_t в UInt64Entry.
//   Конструируем PackedPhone из raw и берём .phone().
inline uint64_t extractPhone(const PhoneRecord& r) {
    uint64_t raw = static_cast<uint64_t>(r.get<0>());
    return PackedPhone(raw).phone();          // только номер, без flags/group
}

// Ключ для Group: имя как строка
inline uint16_t extractGroup(const PhoneRecord& r) {
    uint64_t raw = static_cast<uint64_t>(r.get<0>());
    return PackedPhone(raw).group();          // только номер, без flags/group
}


inline String extractPhoneStr(const PhoneRecord& r) {
    uint64_t raw = static_cast<uint64_t>(r.get<0>());
    return PackedPhone(raw).phoneStr();          // только номер строкой
}


// Создать PhoneRecord из PackedPhone + комментарий
inline PhoneRecord makeRecord(const PackedPhone& pp,
                               const String& description = "")
{
    PhoneRecord rec;
    rec.set<0>(pp.raw());   // сохраняем raw: phone + flags + group
    rec.set<1>(description.c_str());
    return rec;
}

// Распаковать PackedPhone из записи
inline PackedPhone getPhone(const PhoneRecord& rec) {
    return PackedPhone(static_cast<uint64_t>(rec.get<0>()));
}


// ─────────────────────────────────────────────────────────────────────────────
// 4. Класс-обёртка PhoneBook
// ─────────────────────────────────────────────────────────────────────────────

class PhoneBook {
public:
    // ── Конфигурация ───────────────────────────────────────────────────────
    // Выбираем тип индекса под платформу:
    //   ESP8266 / мало записей  → StructIndex (RAM)
    //   ESP8266 / много записей → FSIndex/DRAM
    //   ESP32 с PSRAM           → FSIndex/PSRAM

#if defined(USE_RAM_INDEX)
    // Вариант A: RAM-индекс
    using PhoneIdx = UInt64Index<PhoneRecord>;
    using GrouIdx = FSUint16Index<PhoneRecord>;
    //using NameIdx  = StringIndex<PhoneRecord>;
#else
    // Вариант B/C: FS-индекс (по умолчанию)
    using PhoneIdx = FSUInt64Index<PhoneRecord>;
    //using GroupIdx = FSUint16Index<PhoneRecord>;
    //using NameIdx  = FSStringIndex<PhoneRecord>;
#endif

private:
    StructCollection<PhoneRecord> _db;
    PhoneIdx _phoneIdx;
    //GroupIdx _groupIdx;

    bool _open = false;

    // ── Вспомогательный метод: прочитать запись по offset ─────────────────
    bool _readAt(uint32_t offset, PhoneRecord& rec) {
        return _db.readAtOffset(offset, rec);
    }

public:
    // ── Конструктор: создаём индексы с нужными экстракторами ──────────────

#if defined(USE_RAM_INDEX)
    PhoneBook()
        : _phoneIdx(extractPhone)
        //, _groupIdx(extractGroup)
        //, _nameIdx(extractName)
    {}
#else
    // FS-индекс: передаём имена файлов + конфиг
    explicit PhoneBook(const FSIdxConfig& cfg = defaultConfig())
        : _phoneIdx("/phonebook.phone.fsidx", extractPhone, cfg)
        //, _groupIdx ("/phonebook.group.fsidx",  extractGroup,  cfg)
    {}

    static FSIdxConfig defaultConfig() {
#if defined(ESP32)
        return FSIdxConfig{
            .pageSize   = 512,
            .cachePages = 4,
            .dirtyLimit = 16,
            .bloomMem   = bloom_alloc::MemType::PSRAM,
            .cacheMem   = bloom_alloc::MemType::PSRAM,
            .bloomFPR   = 0.01f,
            .bloomN     = 2000,
        };
#else
        return FSIdxConfig{
            .pageSize   = 256,
            .cachePages = 4,
            .dirtyLimit = 16,
            .bloomMem   = bloom_alloc::MemType::DRAM,
            .cacheMem   = bloom_alloc::MemType::DRAM,
            .bloomFPR   = 0.02f,
            .bloomN     = 500,
        };
#endif
    }
#endif

    // ── Открыть БД ─────────────────────────────────────────────────────────
    bool begin(const char* dbFile = "/phonebook.db") {
        if (!_db.open(dbFile)) return false;

        _db.attachIndex("phone", &_phoneIdx);
        //_db.attachIndex("name",  &_nameIdx);

        _db.loadAllIndexes();

        if (_db.count() > 0 && _phoneIdx.size() == 0) {
            log_i("Rebuilding phonebook indexes...");
            _db.rebuildAllIndexes();
        }

        _open = true;

        log_i("PhoneBook: %u records, %u deleted, frag %u%%",
              _db.count(), _db.deletedCount(), _db.fragPercent());

        return true;
    }

    void end() {
        _db.close();
        _open = false;
    }

    void clean(const char* dbFile = "/phonebook.db"){
        _db.close();
        LittleFS.remove(dbFile);
        begin(dbFile);
    }

    // ── Добавить запись ────────────────────────────────────────────────────
    //
    // pp содержит номер + flags + group — всё уже упаковано.
    // Индексируется только по pp.phone() (голый номер).
    //
    bool add(const PackedPhone& pp, const String& description = "") {
        if (!_open) return false;
        // Проверить дубль по номеру
        if (_phoneIdx.find(pp.phone()) != UINT32_MAX) {
            log_w("Phone %llu already exists", pp.phone());
            return false;
        }
        PhoneRecord rec = makeRecord(pp, description);
        return _db.append(rec);
    }

    // Удобная перегрузка: из строки номера
    bool add(const String& phoneStr, uint8_t flags, uint16_t group,
             const String& description = "")
    {
        PackedPhone pp(phoneStr, flags, group);
        if (!pp.isValid()) return false;
        return add(pp, description);
    }

    // ── Найти по номеру → возвращает PackedPhone (с flags/group!) ──────────
    //
    // Ключ поиска = голый номер. Возвращаем весь PackedPhone (с flags/group).
    // Это позволяет: нашли по номеру → получили flags/group "бесплатно".
    //
    bool findByPhone(uint64_t phoneNum, PackedPhone& outPhone,
                    String* outDescription = nullptr)
    {
        uint32_t offset = _phoneIdx.find(phoneNum);
        if (offset == UINT32_MAX) return false;

        PhoneRecord rec;
        if (!_readAt(offset, rec)) return false;

        outPhone = PackedPhone(static_cast<uint64_t>(rec.get<0>()));
        
        if (outDescription) *outDescription = rec.get<1>().toString();
        return true;
    }

    // Перегрузка: из строки номера (фильтрует не-цифры)
    bool findByPhone(const String& phoneStr, PackedPhone& outPhone,
                    String* outDescription = nullptr )
    {
        PackedPhone tmp(phoneStr);  // парсит цифры из строки
        if (!tmp.isValid()) return false;
        return findByPhone(tmp.phone(), outPhone, outDescription);
    }

    // Проверка наличия (быстрая, через Bloom → find)
    bool exists(uint64_t phoneNum) {
        return _phoneIdx.find(phoneNum) != UINT32_MAX;
    }

    bool exists(const String& phoneStr) {
        PackedPhone tmp(phoneStr);
        return tmp.isValid() && exists(tmp.phone());
    }

    // ── Обновить flags/group по номеру ─────────────────────────────────────
    //
    // Ключ индекса (phone()) НЕ меняется → индекс обновляется корректно:
    // remove(старый raw) + insert(новый raw) = один и тот же phone()-ключ.
    //
    bool updateFlags(uint64_t phoneNum, uint8_t newFlags, uint16_t newGroup) {
        uint32_t offset = _phoneIdx.find(phoneNum);
        if (offset == UINT32_MAX) return false;

        PhoneRecord rec;
        if (!_readAt(offset, rec)) return false;

        PackedPhone oldPP(static_cast<uint64_t>(rec.get<0>()));
        // Создаём новый PackedPhone с тем же номером, новыми flags/group
        PackedPhone newPP(oldPP.phone(), newFlags, newGroup);

        rec.set<0>(newPP.raw());
        return _db.updateAtOffset(offset, rec);
    }

    // Обновить имя/комментарий по номеру
    bool updateDescription(uint64_t phoneNum, const String& newdescription )
    {
        uint32_t offset = _phoneIdx.find(phoneNum);
        if (offset == UINT32_MAX) return false;

        PhoneRecord rec;
        if (!_readAt(offset, rec)) return false;
        rec.set<1>(newdescription.c_str());
        return _db.updateAtOffset(offset, rec);
    }

    // ── Удалить по номеру ──────────────────────────────────────────────────
    bool remove(uint64_t phoneNum) {
        uint32_t offset = _phoneIdx.find(phoneNum);
        if (offset == UINT32_MAX) return false;
        return _db.removeAtOffset(offset);
    }

    bool remove(const String& phoneStr) {
        PackedPhone tmp(phoneStr);
        return tmp.isValid() && remove(tmp.phone());
    }


    // // ── Найти по имени ─────────────────────────────────────────────────────
    // bool findByName(const String& name, PackedPhone& outPhone) {
    //     uint32_t offset = _nameIdx.find(name);
    //     if (offset == UINT32_MAX) return false;
    //     PhoneRecord rec;
    //     if (!_readAt(offset, rec)) return false;
    //     outPhone = getPhone(rec);
    //     return true;
    // }

    // ── Полный обход ───────────────────────────────────────────────────────
    // callback(PackedPhone, name, description) → return false чтобы прервать
    template<typename Fn>
    void forEach(Fn callback) {
        auto it = _db.getIterator();
        PhoneRecord rec;
        log_v("Start while");
        while (it.next(rec)) {
            log_v("in while");
            PackedPhone pp = getPhone(rec);
            String description = rec.get<1>().toString();
            log_d ( "%s, %s", pp.toString().c_str(), description.c_str() );
            log_v("run callback");
            if (!callback(pp, description)) break;
        }
    }

    // ── Компактификация (вызывать в loop()) ────────────────────────────────
    // Возвращает true когда завершена (или не нужна)
    bool compact(size_t chunkBytes = 512) {
        return _db.compact(chunkBytes);
    }

    bool needsCompaction() const {
        return _db.fragPercent() > 25;
    }

    // ── Статистика ─────────────────────────────────────────────────────────
    uint32_t count()        const { return _db.count(); }
    uint32_t deletedCount() const { return _db.deletedCount(); }
    uint8_t  fragPercent()  const { return _db.fragPercent(); }

    void printStats() const {
        Serial.printf("=== PhoneBook Stats ===\n");
        Serial.printf("  Records:   %u\n", _db.count());
        Serial.printf("  Deleted:   %u\n", _db.deletedCount());
        Serial.printf("  Frag:      %u%%\n", _db.fragPercent());
#if !defined(USE_RAM_INDEX)
        Serial.printf("  PhoneIdx:  %u in file, %zu pending, FPR=%.1f%%, RAM=%zu B\n",
            _phoneIdx.fileCount(), _phoneIdx.pendingCount(),
            _phoneIdx.bloomFPR() * 100.0f, _phoneIdx.totalRamBytes());
#endif
    }
};





// ─────────────────────────────────────────────────────────────────────────────
// 6. Важные детали реализации
// ─────────────────────────────────────────────────────────────────────────────
/*
 * ПОЧЕМУ КЛЮЧ = phone(), А НЕ raw()?
 * ────────────────────────────────────
 *
 *  PackedPhone.raw() включает flags и group в битах 50-63.
 *  Если бы ключом был raw():
 *    - один и тот же номер с разными flags → разные ключи → нельзя найти
 *    - updateFlags() всегда меняет ключ → нужно delete+insert в индексе
 *    - дубль-проверка (exists) работала бы только при совпадении flags/group
 *
 *  Ключ = phone() (только 50 бит номера):
 *    - поиск не зависит от flags/group
 *    - updateFlags() меняет только данные в файле, ключ индекса тот же
 *    - один номер = одна запись (что логично для телефонной книги)
 *
 *
 * КАК УInt64Entry ХРАНИТ PackedPhone
 * ────────────────────────────────────
 *
 *  PackedPhone → .raw() → uint64_t → UInt64Entry.set(uint64_t)
 *                                              ↓
 *                               В файле: [UINT64_T][8 байт little-endian]
 *
 *  При чтении: UInt64Entry → static_cast<uint64_t> → PackedPhone(raw_value)
 *
 *  Это прозрачно: StructCollection видит просто uint64_t,
 *  а прикладной код интерпретирует его как PackedPhone.
 *
 *
 * ИНДЕКСИРОВАНИЕ ПО phone() — ДЕТАЛИ
 * ─────────────────────────────────────
 *
 *  extractPhone(rec):
 *    uint64_t raw = static_cast<uint64_t>(rec.get<0>());  // UInt64Entry→uint64
 *    return PackedPhone(raw).phone();                      // достаём 50 бит номера
 *
 *  При вызове _db.append(rec):
 *    → notifyInsert(rec, offset)
 *    → _phoneIdx.insert(&rec, offset)
 *    → extractPhone(rec) → ключ = phone()
 *    → map/FSIndex: phone() → offset
 *
 *  При findByPhone(num):
 *    → phoneIdx.find(num)      ← ищем по phone()
 *    → offset → readAtOffset   ← читаем всю запись (raw включает flags/group)
 *    → PackedPhone(raw)        ← получаем обратно flags и group "бесплатно"
 *
 *
 * РАСХОД ПАМЯТИ (FSIndex/DRAM, N=500)
 * ─────────────────────────────────────
 *
 *  BloomFilter (FPR=2%, N=500): ~600 байт
 *  Page cache  (4 × 256 байт): 1024 байт
 *  Pending     (до 16 записей): ~256 байт
 *  Итого PhoneIdx:             ~1.9 KB DRAM
 *
 *  NameIdx: аналогично +  переменная длина ключей → чуть больше
 *
 *  Для сравнения — StructIndex на 500 записей: ~16 KB DRAM
 */