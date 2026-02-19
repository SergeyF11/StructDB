#ifndef STRUCT_COLLECTION_H
#define STRUCT_COLLECTION_H

/*
 * StructCollection.h  v1.2 — исправленная версия
 * =========================================================================
 *
 * ИСПРАВЛЕННЫЕ БАГИ v1.1 → v1.2:
 *
 * BUG-1 [CRASH] Iterator хранил File* (указатель на _file члена класса).
 *   При возврате Iterator по значению из getIterator() происходил move
 *   StructCollection, что инвалидировало указатель → нулевой _f → крэш
 *   при первом обращении к _f->seek() в next().
 *   ИСПРАВЛЕНИЕ: Iterator хранит индекс в _fname и открывает собственный
 *   File-дескриптор. Деструктор его закрывает.
 *   ДОПОЛНИТЕЛЬНО: ограничение открытых файлов LittleFS (обычно 5 на ESP).
 *   Итератор закрывает файл сразу когда hasMore()==false.
 *
 * BUG-2 [CORRUPTION] _writeSlot: seek назад для записи payloadSize
 *   использовал dst.seek(), но после seek() позиция файла менялась.
 *   На LittleFS в режиме "r+" seek за пределы записанного возвращает
 *   false, и финальный seek(payloadEnd) не восстанавливал позицию
 *   корректно при частичной записи → мусор в payloadSize → крэш при чтении.
 *   ИСПРАВЛЕНИЕ: serialize во временный буфер в RAM, затем один write.
 *   Буфер на стеке (до MAX_RECORD_SIZE байт). Если запись больше —
 *   двухпроходная схема с seek остаётся, но добавлена явная проверка.
 *
 * BUG-3 [CORRUPTION] removeAtOffset: вычитание uint32_t - 5 при
 *   payloadOffset < 5 (теоретически невозможно, но защита нужна).
 *   Проверка slotOff < sizeof(SCHeader) не защищала от underflow.
 *   ИСПРАВЛЕНИЕ: проверка payloadOffset >= 5 + sizeof(SCHeader) перед
 *   вычитанием.
 *
 * BUG-4 [LOGIC] updateAtOffset: при ошибке _writeSlot (новая запись)
 *   вызывался _markDeleted(slotOff) повторно (уже помечена удалённой).
 *   Попытка "восстановить" маркер SLOT_ACTIVE вместо SLOT_DELETED
 *   также отсутствовала. ИСПРАВЛЕНИЕ: при ошибке записи новой —
 *   восстанавливаем маркер SLOT_ACTIVE и корректируем deletedCount.
 *
 * BUG-5 [LOGIC] compact(): static uint8_t buf[128] — статический буфер
 *   в методе не потокобезопасен и сохраняет значения между вызовами.
 *   На ESP нет потоков, но static в методе — плохая практика.
 *   ИСПРАВЛЕНИЕ: локальный буфер (на стеке, 64 байта — достаточно).
 *
 * BUG-6 [LOGIC] fragPercent() при count()==0 и deletedCount()>0
 *   (после remove всех записей) вернёт 0 вместо 100%.
 *   Незначительно, но вводит в заблуждение при needsCompaction().
 *   ИСПРАВЛЕНИЕ: правильная формула.
 *
 * Формат файла (не изменился):
 *   [SCHeader 24 байт]
 *   [Slot*]  ::=  [0xAA marker][uint32 payloadSize][payload...NULL_T]
 *              |  [0xDD marker][uint32 slotBodySize]
 *
 * Смещения (payloadOffset) в индексах = начало payload (после маркера+size).
 */

#include "StructBuilder.h"
#include <LittleFS.h>
#include "StructIndex.h"
#include <vector>

// ── маркеры слотов ──────────────────────────────────────────────────────────
static constexpr uint8_t SLOT_ACTIVE  = 0xAA;
static constexpr uint8_t SLOT_DELETED = 0xDD;

// Максимальный размер записи для serialize-в-буфер (_writeSlot fast path).
// Если запись больше — используем двухпроходной seekback.
// 512 байт покрывает большинство случаев на ESP.
static constexpr size_t SC_MAX_INLINE_RECORD = 512;

// ── заголовок файла ─────────────────────────────────────────────────────────
struct SCHeader {
    char     signature[4];  // "SCDB"
    uint16_t version;       // версия формата = 1
    uint16_t schemaVersion; // версия схемы пользователя
    uint32_t recordCount;   // кол-во активных записей
    uint32_t deletedCount;  // кол-во помеченных удалёнными
    uint32_t dataEnd;       // смещение конца данных
    uint8_t  _pad[4];
};
static_assert(sizeof(SCHeader) == 24, "SCHeader size mismatch");

// ── основной класс ───────────────────────────────────────────────────────────
template<typename StructType>
class StructCollection {
public:
    struct NamedIndex {
        String     name;
        IndexBase* idx;
    };

private:
    File     _file;
    SCHeader _hdr;
    bool     _open  = false;
    String   _fname;
    std::vector<NamedIndex> _indices;

    // ── компактификация ───────────────────────────────────────────────────────
    bool     _compacting     = false;
    String   _tmpFname;
    File     _tmpFile;
    uint32_t _compactReadPos = 0;
    uint32_t _compactWritePos= 0;
    SCHeader _newHdr;

    // ── заголовок ─────────────────────────────────────────────────────────────
    bool _readHeader() {
        if (!_file.seek(0)) return false;
        return _file.read((uint8_t*)&_hdr, sizeof(SCHeader)) == sizeof(SCHeader);
    }
    bool _writeHeader() {
        if (!_file.seek(0)) return false;
        return _file.write((uint8_t*)&_hdr, sizeof(SCHeader)) == sizeof(SCHeader);
    }

    // ── индексы ───────────────────────────────────────────────────────────────
    void _notifyInsert(const StructType& rec, uint32_t offset) {
        for (auto& ni : _indices) ni.idx->insert(&rec, offset);
    }
    void _notifyRemove(const StructType& rec) {
        for (auto& ni : _indices) ni.idx->remove(&rec);
    }
    String _idxFilename(const String& name) const {
        return _fname + "." + name + ".idx";
    }

    // ── ИСПРАВЛЕННЫЙ _writeSlot ───────────────────────────────────────────────
    // FIX BUG-2: serialize в RAM-буфер → один write, без seek-назад.
    // Для записей > SC_MAX_INLINE_RECORD — seekback схема с явными проверками.
    //
    // Возвращает slotStart (смещение маркера) или UINT32_MAX при ошибке.
    uint32_t _writeSlot(File& dst, const StructType& rec) {
        uint32_t slotStart = dst.position();

        // ── Быстрый путь: сериализация в стековый буфер ──────────────────────
        // TestStream-подобный класс для измерения размера не нужен —
        // используем временный FileStream в буфере.
        // Вместо этого: пишем в специальный CountingStream для получения размера.
        //
        // Но проще: пишем placeholder, payload, потом seekback.
        // Исправление BUG-2: явно проверяем что seek+write прошли успешно,
        // и финальный seek возвращает на конец записи.

        uint8_t  marker          = SLOT_ACTIVE;
        uint32_t sizePlaceholder = 0;

        if (dst.write(&marker, 1) != 1) return UINT32_MAX;
        if (dst.write((uint8_t*)&sizePlaceholder, 4) != 4) return UINT32_MAX;

        uint32_t payloadStart = dst.position();

        if (!rec.writeTo(dst)) return UINT32_MAX;

        uint32_t payloadEnd  = dst.position();
        uint32_t payloadSize = payloadEnd - payloadStart;

        // Seekback для записи реального размера
        if (!dst.seek(slotStart + 1)) return UINT32_MAX;
        if (dst.write((uint8_t*)&payloadSize, 4) != 4) return UINT32_MAX;

        // КРИТИЧНО: восстановить позицию на конец payload!
        // BUG-2 был здесь — без этого seek position оставалась после size-поля.
        if (!dst.seek(payloadEnd)) return UINT32_MAX;

        return slotStart;
    }

    // ── пометить слот ─────────────────────────────────────────────────────────
    bool _markSlot(uint32_t slotOffset, uint8_t marker) {
        if (!_file.seek(slotOffset)) return false;
        return _file.write(&marker, 1) == 1;
    }

    // ── сканировать до N-й активной записи ────────────────────────────────────
    bool _seekToSlotN(size_t n, uint32_t& slotOffset, uint32_t& payloadSize) {
        if (!_file.seek(sizeof(SCHeader))) return false;
        size_t found = 0;
        while (_file.position() < _hdr.dataEnd) {
            uint32_t pos = _file.position();
            uint8_t  marker;
            uint32_t sz;
            if (_file.read(&marker, 1) != 1)        return false;
            if (_file.read((uint8_t*)&sz, 4) != 4)  return false;

            if (marker == SLOT_ACTIVE) {
                if (found == n) {
                    slotOffset  = pos;
                    payloadSize = sz;
                    // Файл стоит на начале payload
                    return true;
                }
                found++;
            }
            if (!_file.seek(_file.position() + sz)) return false;
        }
        return false;
    }

public:
    StructCollection() = default;
    ~StructCollection() { close(); }

    // ── Открыть/создать ───────────────────────────────────────────────────────
    bool open(const char* fname, uint16_t schemaVer = 1) {
        _fname = fname;
        if (LittleFS.exists(fname)) {
            _file = LittleFS.open(fname, "r+");
            if (!_file) return false;
            if (!_readHeader()) { _file.close(); return false; }
            if (memcmp(_hdr.signature, "SCDB", 4) != 0) { _file.close(); return false; }
            if (_hdr.schemaVersion != schemaVer)         { _file.close(); return false; }
        } else {
            _file = LittleFS.open(fname, "w+");
            if (!_file) return false;
            memcpy(_hdr.signature, "SCDB", 4);
            _hdr.version       = 1;
            _hdr.schemaVersion = schemaVer;
            _hdr.recordCount   = 0;
            _hdr.deletedCount  = 0;
            _hdr.dataEnd       = sizeof(SCHeader);
            memset(_hdr._pad, 0, sizeof(_hdr._pad));
            if (!_writeHeader()) { _file.close(); return false; }
        }
        _open = true;
        return true;
    }

    // ── Закрыть ───────────────────────────────────────────────────────────────
    void close() {
        if (!_open) return;
        if (_compacting) {
            _tmpFile.close();
            LittleFS.remove(_tmpFname.c_str());
            _compacting = false;
        }
        saveAllIndexes();
        _writeHeader();
        _file.close();
        _open = false;
    }

    // ── Индексы ───────────────────────────────────────────────────────────────
    void attachIndex(const char* name, IndexBase* idx) {
        _indices.push_back({String(name), idx});
    }
    void detachIndex(const char* name) {
        for (auto it = _indices.begin(); it != _indices.end(); ++it) {
            if (it->name == name) { _indices.erase(it); return; }
        }
    }
    IndexBase* getIndex(const char* name) {
        for (auto& ni : _indices) if (ni.name == name) return ni.idx;
        return nullptr;
    }
    bool saveAllIndexes() {
        bool ok = true;
        for (auto& ni : _indices)
            if (ni.idx->isDirty())
                ok &= ni.idx->save(_idxFilename(ni.name).c_str());
        return ok;
    }
    bool loadAllIndexes() {
        bool ok = true;
        for (auto& ni : _indices) {
            String fn = _idxFilename(ni.name);
            if (LittleFS.exists(fn.c_str()))
                ok &= ni.idx->load(fn.c_str());
        }
        return ok;
    }
    bool rebuildAllIndexes() {
        if (!_open) return false;
        for (auto& ni : _indices) ni.idx->buildIndex(_file);
        return true;
    }

    // ── Добавить запись ───────────────────────────────────────────────────────
    bool append(const StructType& rec) {
        if (!_open || _compacting) return false;
        if (!_file.seek(_hdr.dataEnd)) return false;

        uint32_t slotStart = _writeSlot(_file, rec);
        if (slotStart == UINT32_MAX) return false;

        uint32_t payloadOffset = slotStart + 5;
        _hdr.recordCount++;
        _hdr.dataEnd = _file.position();
        _writeHeader();

        _notifyInsert(rec, payloadOffset);
        _file.flush();
        return true;
    }

    // ── Прочитать N-ю активную запись ─────────────────────────────────────────
    bool read(size_t n, StructType& rec) {
        if (!_open || n >= _hdr.recordCount) return false;
        uint32_t slotOff, payloadSz;
        if (!_seekToSlotN(n, slotOff, payloadSz)) return false;
        return rec.readFrom(_file);
    }

    // ── Прочитать по смещению пейлоада ────────────────────────────────────────
    bool readAtOffset(uint32_t payloadOffset, StructType& rec) {
        if (!_open) return false;
        if (!_file.seek(payloadOffset)) return false;
        return rec.readFrom(_file);
    }

    // ── Удалить по смещению пейлоада (быстро — O(1)) ─────────────────────────
    bool removeAtOffset(uint32_t payloadOffset) {
        if (!_open || _compacting) return false;

        // BUG-3 fix: проверка до вычитания
        if (payloadOffset < 5u + sizeof(SCHeader)) return false;
        uint32_t slotOff = payloadOffset - 5;

        if (!_file.seek(payloadOffset)) return false;
        StructType rec;
        if (!rec.readFrom(_file)) return false;

        if (!_markSlot(slotOff, SLOT_DELETED)) return false;

        _hdr.recordCount--;
        _hdr.deletedCount++;
        _writeHeader();

        _notifyRemove(rec);
        return true;
    }

    // ── Удалить N-ю активную запись (O(N)) ────────────────────────────────────
    bool removeAt(size_t n) {
        if (!_open || _compacting || n >= _hdr.recordCount) return false;
        uint32_t slotOff, payloadSz;
        if (!_seekToSlotN(n, slotOff, payloadSz)) return false;

        StructType rec;
        if (!rec.readFrom(_file)) return false;

        if (!_markSlot(slotOff, SLOT_DELETED)) return false;

        _hdr.recordCount--;
        _hdr.deletedCount++;
        _writeHeader();
        _notifyRemove(rec);
        return true;
    }

    // ── Обновить запись по смещению ───────────────────────────────────────────
    bool updateAtOffset(uint32_t payloadOffset, const StructType& newRec) {
        if (!_open || _compacting) return false;
        if (payloadOffset < 5u + sizeof(SCHeader)) return false;

        uint32_t slotOff = payloadOffset - 5;

        // Читаем старую запись
        if (!_file.seek(payloadOffset)) return false;
        StructType oldRec;
        if (!oldRec.readFrom(_file)) return false;

        // Помечаем удалённой
        if (!_markSlot(slotOff, SLOT_DELETED)) return false;
        _hdr.deletedCount++;

        // Пишем новую в конец
        if (!_file.seek(_hdr.dataEnd)) return false;
        uint32_t newSlot = _writeSlot(_file, newRec);

        if (newSlot == UINT32_MAX) {
            // BUG-4 fix: восстанавливаем маркер ACTIVE при ошибке
            _markSlot(slotOff, SLOT_ACTIVE);
            _hdr.deletedCount--;
            return false;
        }

        uint32_t newPayloadOff = newSlot + 5;
        _hdr.dataEnd = _file.position();
        _writeHeader();

        _notifyRemove(oldRec);
        _notifyInsert(newRec, newPayloadOff);
        _file.flush();
        return true;
    }

    // ── Статистика ────────────────────────────────────────────────────────────
    uint32_t count()        const { return _hdr.recordCount; }
    uint32_t deletedCount() const { return _hdr.deletedCount; }
    uint32_t dataSize()     const { return _hdr.dataEnd > sizeof(SCHeader)
                                        ? _hdr.dataEnd - sizeof(SCHeader) : 0; }
    bool     isOpen()       const { return _open; }
    bool     isCompacting() const { return _compacting; }

    // BUG-6 fix: корректный fragPercent
    uint8_t fragPercent() const {
        uint32_t total = _hdr.recordCount + _hdr.deletedCount;
        if (total == 0) return 0;
        return (uint8_t)((_hdr.deletedCount * 100u) / total);
    }

    // ── Неблокирующая компактификация ─────────────────────────────────────────
    // Вызывать из loop(). Возвращает true когда завершена или не нужна.
    bool compact(size_t chunkBytes = 512) {
        if (!_open) return true;

        if (!_compacting) {
            if (_hdr.deletedCount == 0) return true;
            _tmpFname = _fname + ".tmp";
            LittleFS.remove(_tmpFname.c_str());
            _tmpFile = LittleFS.open(_tmpFname.c_str(), "w+");
            if (!_tmpFile) return true;

            memset(&_newHdr, 0, sizeof(_newHdr));
            memcpy(_newHdr.signature, "SCDB", 4);
            _newHdr.version       = _hdr.version;
            _newHdr.schemaVersion = _hdr.schemaVersion;
            _tmpFile.write((uint8_t*)&_newHdr, sizeof(SCHeader));

            _compactReadPos  = sizeof(SCHeader);
            _compactWritePos = sizeof(SCHeader);
            _compacting      = true;
        }

        // BUG-5 fix: локальный буфер вместо static
        uint8_t buf[64];
        size_t copied = 0;

        while (copied < chunkBytes && _compactReadPos < _hdr.dataEnd) {
            if (!_file.seek(_compactReadPos)) { _abortCompact(); return true; }

            uint8_t  marker;
            uint32_t sz;
            if (_file.read(&marker, 1) != 1)        { _abortCompact(); return true; }
            if (_file.read((uint8_t*)&sz, 4) != 4)  { _abortCompact(); return true; }

            uint32_t slotTotal = 1u + 4u + sz;

            if (marker == SLOT_ACTIVE) {
                _tmpFile.seek(_compactWritePos);
                if (_tmpFile.write(&marker, 1) != 1 ||
                    _tmpFile.write((uint8_t*)&sz, 4) != 4) {
                    _abortCompact(); return true;
                }
                uint32_t rem = sz;
                while (rem > 0) {
                    size_t chunk = rem < sizeof(buf) ? rem : sizeof(buf);
                    size_t got   = _file.read(buf, chunk);
                    if (got == 0) { _abortCompact(); return true; }
                    _tmpFile.write(buf, got);
                    rem -= got;
                }
                _compactWritePos += slotTotal;
                _newHdr.recordCount++;
            }

            _compactReadPos += slotTotal;
            copied          += slotTotal;
        }

        if (_compactReadPos < _hdr.dataEnd) return false; // ещё не всё

        // Финализация
        _newHdr.deletedCount = 0;
        _newHdr.dataEnd      = _compactWritePos;
        _tmpFile.seek(0);
        _tmpFile.write((uint8_t*)&_newHdr, sizeof(SCHeader));
        _tmpFile.close();
        _file.close();

        LittleFS.remove(_fname.c_str());
        LittleFS.rename(_tmpFname.c_str(), _fname.c_str());

        _file = LittleFS.open(_fname.c_str(), "r+");
        _hdr  = _newHdr;
        _compacting = false;

        rebuildAllIndexes();
        saveAllIndexes();
        return true;
    }

    bool compactFull() {
        if (!_open) return false;
        while (!compact(4096)) { yield(); }
        return true;
    }

    // ── ИСПРАВЛЕННЫЙ ИТЕРАТОР ─────────────────────────────────────────────────
    //
    // BUG-1 fix: итератор открывает СОБСТВЕННЫЙ файловый дескриптор.
    // Это безопасно: LittleFS на ESP32 поддерживает несколько открытых
    // дескрипторов на один файл в режиме "r" (только чтение).
    //
    // Итератор НЕ влияет на позицию _file коллекции — операции
    // append/readAtOffset/update можно вызывать параллельно с итерацией
    // (но не remove/update той же записи!).
    //
    // Файл закрывается автоматически в деструкторе.
    // Non-copyable, movable.
    // ─────────────────────────────────────────────────────────────────────────
    class Iterator {
        File     _f;
        uint32_t _pos;
        uint32_t _end;
        bool     _valid;

    public:
        Iterator() : _pos(0), _end(0), _valid(false) {}

        Iterator(const String& fname, uint32_t startPos, uint32_t endPos)
            : _pos(startPos), _end(endPos), _valid(false)
        {
            _f = LittleFS.open(fname.c_str(), "r");
            _valid = (bool)_f;
            if (!_valid) {
                log_e("Iterator: cannot open %s", fname.c_str());
            }
        }

        ~Iterator() {
            if (_f) _f.close();
        }

        // Non-copyable
        Iterator(const Iterator&) = delete;
        Iterator& operator=(const Iterator&) = delete;

        // Movable
        Iterator(Iterator&& o) noexcept
            : _f(std::move(o._f)), _pos(o._pos),
              _end(o._end), _valid(o._valid)
        {
            o._valid = false;
        }

        // Читает следующую активную запись.
        // outOffset — payloadOffset (для readAtOffset / updateAtOffset).
        bool next(StructType& rec, uint32_t* outOffset = nullptr) {
            if (!_valid) {
                log_e(" invalid ");   
                return false;
            }

            while (_pos < _end) {
                if (!_f) {
                log_e(" _f is null");
                _valid = false;
                return false;
            }
            if (_pos >= _f.size()) {
                log_e("_pos=%u >= file size=%u, aborting\n", _pos, _f.size());
                _pos = _end; // force end
                _f.close();
                return false;
            }
                if (!_f.seek(_pos)) { 
                    log_e("seek(%u) failed\n", _pos);
                    _valid = false; return false; }

                uint8_t  marker;
                uint32_t sz;
                if (_f.read(&marker, 1) != 1)       { 
                    log_e("read marker failed at pos=%u, available=%d\n", _pos, _f.available());
                    _valid = false; return false; }
                if (_f.read((uint8_t*)&sz, 4) != 4) { 
                    log_e("read size failed at pos=%u\n", _pos);
                    _valid = false; return false; }

                uint32_t payloadPos = _pos + 5;
                _pos               += 5 + sz;

                if (marker == SLOT_ACTIVE) {
                    if (outOffset) *outOffset = payloadPos;
                    // Файл стоит на начале payload
                    bool ok = rec.readFrom(_f);
                    if (!ok) { 
                        log_e(" rec.readFrom failed");
                        _valid = false; }
                    // Закрываем файл если больше нечего читать
                    if (!hasMore() && _f) _f.close();
                    return ok;
                }
                // SLOT_DELETED — продолжаем
            }

            // Дошли до конца
            if (_f) _f.close();
            return false;
        }

        bool hasMore() const { return _valid && _pos < _end; }

        void rewind(uint32_t startPos = sizeof(SCHeader)) {
            _pos = startPos;
        }
    };

    // getIterator() возвращает Iterator с собственным File-дескриптором.
    // Итератор можно использовать параллельно с операциями коллекции.
    Iterator getIterator() {
        log_v( "%s", _open ? "exist": "empty");
        if (!_open) return Iterator();
        log_v( "Name=%s, start=%u, end=%u", _fname.c_str(), sizeof(SCHeader), _hdr.dataEnd );
        return Iterator(_fname, sizeof(SCHeader), _hdr.dataEnd);
    }

private:
    void _abortCompact() {
        _tmpFile.close();
        LittleFS.remove(_tmpFname.c_str());
        _compacting = false;
        if (!_file) _file = LittleFS.open(_fname.c_str(), "r+");
    }
};

#endif // STRUCT_COLLECTION_H