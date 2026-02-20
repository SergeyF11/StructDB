// ── Пример реализации базы данных для телефонов ─────

#include "CompactPhone.h"
#include "phoneDb.h"

#include <Busybox.h>

// ─────────────────────────────────────────────────────────────────────────────
// 5. Пример использования в скетче
// ─────────────────────────────────────────────────────────────────────────────

PhoneBook phonebook;

void setup() {
    Serial.begin(115200);
    delay(500);


    LittleFS.begin(true);
    phonebook.begin("/phonebook.db");
    phonebook.clean("/phonebook.db");

    Busybox::ls("/");
    Busybox::view("/phonebook.db");

    // ── Добавление ─────────────────────────────────────────────────────────

    // Способ 1: из строки (нечистый номер — фильтрует автоматически)
    phonebook.add("+7 (916) 123-45-67", /*flags=*/1, /*group=*/5,
                  "Иван Петров, VIP клиент");

    // Способ 2: из PackedPhone напрямую
    PackedPhone pp(79161234567ULL, /*flags=*/0, /*group=*/1);
    phonebook.add(pp, "Сидоров А.В.");

    // Способ 3: из PackedPhone, построенного из строки
    PackedPhone pp2(String("+7-900-000-00-01"), /*flags=*/2, /*group=*/3);
    if (pp2.isValid()) {
        phonebook.add(pp2, "Тестовый номер");
    }

    // ── Поиск ──────────────────────────────────────────────────────────────

    PackedPhone found;
    String description;

    // По числовому номеру
    if (phonebook.findByPhone(79161234567ULL, found, &description)) {
        Serial.printf("Найдено: %s\n", found.toString().c_str());
        // found.phone()  = 79161234567  (номер)
        // found.flags()  = 1            (флаги из записи)
        // found.group()  = 5            (группа из записи)
        // name           = "Иван Петров"
        // description        = "VIP клиент"

        Serial.printf("  Номер:  %llu\n", found.phone());
        Serial.printf("  Flags:  %u\n",   found.flags());
        Serial.printf("  Group:  %u\n",   found.group());
        Serial.printf("  Имя:    %s\n",   description.c_str());
    }

    // По строке номера (фильтрует +, -, пробелы)
    if (phonebook.findByPhone("+7 916 123-45-67", found)) {
        Serial.printf("Найдено по строке: phone=%llu\n", found.phone());
    }

    // Быстрая проверка (через Bloom → без чтения файла данных)
    if (phonebook.exists(79161234567ULL)) {
        Serial.println("Номер существует");
    }

    // ── Обновление flags/group (ключ индекса не меняется!) ─────────────────
    phonebook.updateFlags(79161234567ULL, /*newFlags=*/3, /*newGroup=*/7);

    // Убеждаемся: поиск по тому же номеру работает, flags изменились
    if (phonebook.findByPhone(79161234567ULL, found)) {
        Serial.printf("После update: flags=%u group=%u\n",
                      found.flags(), found.group());
    }

    // ── Обновление имени ───────────────────────────────────────────────────
    phonebook.updateDescription(79161234567ULL, "Иван Петрович");

    phonebook.printStats();
    // Busybox::ls("/");
    // Busybox::view("/phonebook.db");

    // ── Обход всех записей ─────────────────────────────────────────────────
    Serial.println("=== Все записи ===");
    phonebook.forEach([](const PackedPhone& pp, const String& description) {
        Serial.printf("  %llu (flags=%u group=%u)",
                      pp.phone(), pp.flags(), pp.group());
        if (description.length()) Serial.printf(" [%s]", description.c_str());
        Serial.println();
        return true; // продолжить
    });

    // ── Удаление ───────────────────────────────────────────────────────────
    // phonebook.remove(79161234567ULL);
    // phonebook.remove("+7-900-000-00-01");  // по строке

    // // ── Обход всех записей ─────────────────────────────────────────────────
    // Serial.println("=== Все записи ===");
    // phonebook.forEach([](const PackedPhone& pp, const String& description) {
    //     Serial.printf("  %llu (flags=%u group=%u) — %s",
    //                   pp.phone(), pp.flags(), pp.group());
    //     if (description.length()) Serial.printf(" [%s]", description.c_str());
    //     Serial.println();
    //     return true; // продолжить
    // });

    phonebook.printStats();
}

void loop() {
    if (phonebook.needsCompaction()) {
        if( phonebook.compact(512) ){ // неблокирующий вызов
            Busybox::ls("/");
            Busybox::view("/phonebook.db");
            phonebook.printStats();
        }
    }
}


// void setup() {
//     LittleFS.begin();

//     db.open("/phones.db");
//     db.attachIndex("phone", &phoneIdx);
//     db.attachIndex("name",  &nameIdx);

//     // Пробуем загрузить индексы с диска
//     if (!db.loadAllIndexes()) {
//         // Первый запуск — перестраиваем
//         db.rebuildAllIndexes();
//         db.saveAllIndexes();
//     }
// }

// // 3. Добавление записи
// void addPhone(uint64_t num, const char* name, uint16_t group, uint8_t flags) {
//     PhoneRecord rec;
//     rec.set<0>(num);
//     rec.set<1>(name);
//     rec.set<2>(group);
//     rec.set<3>(flags);
//     db.append(rec);
// }

// // 4. Поиск по телефону через индекс (O(log N), без чтения всего файла)
// bool findByPhone(uint64_t num, PhoneRecord& result) {
//     uint32_t offset = phoneIdx.find(num);
//     if (offset == UINT32_MAX) return false;
//     return db.readAtOffset(offset, result);
// }

// // 5. Обновление записи
// bool updatePhone(uint64_t oldNum, uint64_t newNum, const char* newName) {
//     uint32_t offset = phoneIdx.find(oldNum);
//     if (offset == UINT32_MAX) return false;

//     PhoneRecord newRec;
//     newRec.set<0>(newNum);
//     newRec.set<1>(newName);
//     return db.updateAtOffset(offset, newRec);
// }

// // 6. Удаление по ключу
// bool deletePhone(uint64_t num) {
//     uint32_t offset = phoneIdx.find(num);
//     if (offset == UINT32_MAX) return false;
//     return db.removeAtOffset(offset);
// }

// // 7. Полный обход через итератор
// void listAll() {
//     auto it = db.getIterator();
//     PhoneRecord rec;
//     uint32_t offset;
//     while (it.next(rec, &offset)) {
//         Serial.printf("[%u] phone=%llu name=%s\n",
//             offset,
//             (uint64_t)rec.get<0>(),
//             rec.get<1>().toString().c_str());
//     }
// }

// // 8. Неблокирующая компактификация в loop()
// void loop() {
//     // Вызывать только когда фрагментация превышает порог
//     if (db.fragPercent() > 30) {
//         bool done = db.compact(512); // 512 байт за итерацию loop()
//         if (done) {
//             Serial.println("Compaction done");
//         }
//     }

//     // ... другой код loop
// }