#pragma once
#include "extra/MemoryStream.h"

void test1();
void test2();
void test3();
void test4();
void test5();
void test6();

void test(uint8_t index){
    switch(index){
        case 1: return test1();
        case 2: return test2();
        case 3: return test3();
        case 4: return test4();
        case 5: return test5();
        case 6: return test6();

    }
}

void test7(){
    Serial.println("\nТест 7: плавающая точка");
    
        // Создаем структуру с конструктором
        //Struct1<UInt8Entry> struct1(UInt8Entry(0xAB));
        Struct2<FloatEntry, DoubleEntry> struct1;
        struct1.set<0>( 355.0/113.0 );
        struct1.set<1>(355.0/113.0);
        
        uint8_t buffer[100];
        MemoryStream stream(buffer, sizeof(buffer));
        
        Serial.print("Исходные значения: ");
        Serial.println(struct1.get<0>().toString());
        Serial.println(struct1.get<1>().toString());

        if (struct1.writeTo(stream)) {
            Serial.print("Структура записана. Данные: ");
            stream.printHex();
            Serial.println("Ожидается: FA DC 0F 49 40 FB B8 1F 12 78 FB 21 09 40 00");
            
            // Читаем обратно
            Struct2<FloatEntry, DoubleEntry> struct1_read;
            stream.reset();
            if (struct1_read.readFrom(stream)) {
                Serial.print("Прочитаны значения: ");
                Serial.println(struct1.get<0>().toString());
                Serial.println(struct1.get<1>().toString());
            } else {
                Serial.println("Ошибка чтения!");
            }
        }
        
        // Тест метода set() с числовым значением
        Serial.print("До set(): ");
        Serial.println(struct1.get<0>().toString());
        struct1.set<0>(2.6);  
        Serial.print("После set(2.6): ");
        Serial.println(struct1.get<0>().toString());
        
        // Тест метода set() с объектом
        struct1.set<0>(FloatEntry(0.1234));
        Serial.print("После set(FloatEntry(0.1234)): ");
        Serial.println(struct1.get<0>().toString());

};

void test6() {
    Serial.println("\nТест 6: Пограничные случаи массивов");
    
    // Тест 6.1: Максимальный размер ByteArrayEntry (255 байт)
    Serial.println("--- Тест 6.1: Максимальный размер ByteArrayEntry ---");
    {
        uint8_t maxData[255];
        for (int i = 0; i < 255; i++) {
            maxData[i] = i & 0xFF;
        }
        
        uint8_t buffer[300];
        MemoryStream buffer1(buffer, sizeof(buffer));

        ByteArrayEntry maxArray(maxData, 255);
        
        //MemoryStream buffer1(300);
        
        bool writeResult = maxArray.writeTo(buffer1);
        Serial.print("Запись максимального массива (255 байт): ");
        Serial.println(writeResult ? "УСПЕХ" : "ПРОВАЛ");
        
        if (writeResult) {
            buffer1.printHex();
            Serial.print("Записано ");
            Serial.print(buffer1.getWritten());
            Serial.println(" байт");
            
            buffer1.reset();
            bool readResult = maxArray.readFrom(buffer1);
            Serial.print("Чтение максимального массива: ");
            Serial.println(readResult ? "УСПЕХ" : "ПРОВАЛ");
            
            if (readResult && maxArray.getSize() == 255) {
                Serial.println("Тест 6.1: ПРОЙДЕН");
            } else {
                Serial.println("Тест 6.1: ПРОВАЛЕН");
            }
        }
    }
    
    // Тест 6.2: Большой массив (1000 байт) - должен использовать ByteArrayBigEntry
    Serial.println("\n--- Тест 6.2: Большой массив (1000 байт) ---");
    {
        uint8_t bigData[1000];
        for (int i = 0; i < 1000; i++) {
            bigData[i] = (i * 7) & 0xFF; // Непростая последовательность
        }
        
        uint8_t buffer[1500];
        MemoryStream buffer2(buffer, sizeof(buffer));

        ByteArrayBigEntry bigArray(bigData, 1000);
        //MemoryStream buffer2(1500);
        
        bool writeResult = bigArray.writeTo(buffer2);
        Serial.print("Запись большого массива (1000 байт): ");
        Serial.println(writeResult ? "УСПЕХ" : "ПРОВАЛ");
        
        if (writeResult) {
            Serial.print("Записано ");
            Serial.print(buffer2.getWritten());
            Serial.println(" байт");
            
            buffer2.reset();
            bool readResult = bigArray.readFrom(buffer2);
            Serial.print("Чтение большого массива: ");
            Serial.println(readResult ? "УСПЕХ" : "ПРОВАЛ");
            
            // Проверяем случайные позиции
            if (readResult && bigArray.getSize() == 1000) {
                bool testPassed = true;
                int testPositions[] = {0, 1, 127, 500, 999};
                for (int pos : testPositions) {
                    if (bigArray[pos] != ((pos * 7) & 0xFF)) {
                        Serial.print("ОШИБКА в позиции ");
                        Serial.println(pos);
                        testPassed = false;
                        break;
                    }
                }
                
                if (testPassed) {
                    Serial.println("Тест 6.2: ПРОЙДЕН");
                } else {
                    Serial.println("Тест 6.2: ПРОВАЛЕН");
                }
            } else {
                Serial.println("Тест 6.2: ПРОВАЛЕН - неверный размер");
            }
        }
    }
    
    // Тест 6.3: Пустые массивы в структуре
    Serial.println("\n--- Тест 6.3: Пустые массивы в структуре ---");
    {
        Struct4<ByteArrayEntry, ByteArrayBigEntry, StringEntry, UInt8Entry> struct6_3(
            ByteArrayEntry(nullptr, 0),
            ByteArrayBigEntry(nullptr, 0),
            StringEntry(""),
            UInt8Entry(0x42)
        );
        
  //      MemoryStream buffer3(100);
        uint8_t buffer[100];
        MemoryStream buffer3(buffer, sizeof(buffer));
        struct6_3.writeTo(buffer3);

        buffer3.printHex();
        Serial.print("Записано: ");
        Serial.print(buffer3.getWritten());
        Serial.print(" байт: ");
        buffer3.printHex();//Serial.println(StructDebug::hexDump(buffer3.getData(), buffer3.getWritten()));
        
        buffer3.reset();
        struct6_3.readFrom(buffer3);
        
        Serial.print("Прочитано: ");
        Serial.println(struct6_3.toString());
        
        if (struct6_3.get<0>().getSize() == 0 && 
            struct6_3.get<1>().getSize() == 0 && 
            struct6_3.get<2>().getLength() == 0 &&
            struct6_3.get<3>() == 0x42) {
            Serial.println("Тест 6.3: ПРОЙДЕН");
        } else {
            Serial.println("Тест 6.3: ПРОВАЛЕН");
        }
    }
}

void test5() {

    Serial.println("\nТест 5: Структура с массивами байтов");
    
    // Подготовка данных для теста
    uint8_t smallData[] = {0x01, 0x02, 0x03, 0x04, 0x05};
    uint8_t mediumData[] = "Hello ByteArray!";
    uint8_t largeData[300];
    for (int i = 0; i < 300; i++) {
        largeData[i] = i & 0xFF;
    }
    
    // Тест 5.1: ByteArrayEntry (до 255 байт)
    Serial.println("--- Тест 5.1: ByteArrayEntry ---");
    {
        Struct2<ByteArrayEntry, StringEntry> struct5_1(
            ByteArrayEntry(smallData, sizeof(smallData)),
            StringEntry("Test String")
        );

        uint8_t buffer[100];
        MemoryStream buffer1(buffer, sizeof(buffer));
        //MemoryStream buffer1(100);
        struct5_1.writeTo(buffer1);
        
        Serial.print("Записано: ");
        Serial.print(buffer1.getWritten());
        Serial.print(" байт: ");
        buffer1.printHex();//Serial.println(StructDebug::hexDump(buffer1.getData(), buffer1.getWritten()));
        
        buffer1.reset();
        struct5_1.readFrom(buffer1);
        
        Serial.print("Прочитано: ");
        Serial.println(struct5_1.toString());
        
        // Проверка данных
        bool testPassed = true;
        ByteArrayEntry& array1 = struct5_1.get<0>();
        if (array1.getSize() != sizeof(smallData)) {
            Serial.println("ОШИБКА: Неправильный размер массива");
            testPassed = false;
        } else {
            for (int i = 0; i < sizeof(smallData); i++) {
                if (array1[i] != smallData[i]) {
                    Serial.println("ОШИБКА: Неправильные данные в массиве");
                    testPassed = false;
                    break;
                }
            }
        }
        
        if (testPassed) {
            Serial.println("Тест 5.1: ПРОЙДЕН");
        } else {
            Serial.println("Тест 5.1: ПРОВАЛЕН");
        }
    }
    
    // Тест 5.2: ByteArrayBigEntry (до 65535 байт)
    Serial.println("\n--- Тест 5.2: ByteArrayBigEntry ---");
    {
        Struct2<ByteArrayBigEntry, UInt32Entry> struct5_2(
            ByteArrayBigEntry(largeData, 300),
            UInt32Entry(0xDEADBEEF)
        );
        
        uint8_t buffer[400];
        MemoryStream buffer2(buffer, sizeof(buffer));
        //MemoryStream buffer2(400);
        struct5_2.writeTo(buffer2);
        
        Serial.print("Записано: ");
        Serial.print(buffer2.getWritten());
        Serial.println(" байт");
        buffer2.printHex();
        
        buffer2.reset();
        struct5_2.readFrom(buffer2);
        
        Serial.print("Прочитано: ");
        Serial.println(struct5_2.toString());
        
        // Проверка данных
        bool testPassed = true;
        ByteArrayBigEntry& array2 = struct5_2.get<0>();
        if (array2.getSize() != 300) {
            Serial.print("ОШИБКА: Неправильный размер массива: ");
            Serial.println(array2.getSize());
            testPassed = false;
        } else {
            // Проверяем первые 10 байт
            for (int i = 0; i < 10; i++) {
                if (array2[i] != (i & 0xFF)) {
                    Serial.print("ОШИБКА в первых байтах: [");
                    Serial.print(i);
                    Serial.print("] = ");
                    Serial.println(array2[i]);
                    testPassed = false;
                    break;
                }
            }
            
            // Проверяем последние 10 байт
            for (int i = 290; i < 300; i++) {
                if (array2[i] != (i & 0xFF)) {
                    Serial.print("ОШИБКА в последних байтах: [");
                    Serial.print(i);
                    Serial.print("] = ");
                    Serial.println(array2[i]);
                    testPassed = false;
                    break;
                }
            }
        }
        
        if (testPassed) {
            Serial.println("Тест 5.2: ПРОЙДЕН");
        } else {
            Serial.println("Тест 5.2: ПРОВАЛЕН");
        }
    }
    
    // Тест 5.3: Смешанные типы массивов
    Serial.println("\n--- Тест 5.3: Смешанные типы массивов ---");
    {
        Struct3<ByteArrayEntry, ByteArrayBigEntry, StringEntry> struct5_3(
            ByteArrayEntry(mediumData, sizeof(mediumData) - 1), // -1 чтобы убрать null terminator
            ByteArrayBigEntry(largeData, 100),
            StringEntry("Array Test")
        );
        
        uint8_t buffer[200];
        MemoryStream buffer3(buffer, sizeof(buffer));
        //MemoryStream buffer3(200);
        struct5_3.writeTo(buffer3);
        
        
        Serial.print("Записано: ");
        Serial.print(buffer3.getWritten());
        Serial.println(" байт");
        buffer3.printHex();
        
        buffer3.reset();
        struct5_3.readFrom(buffer3);
        
        Serial.print("Прочитано: ");
        Serial.println(struct5_3.toString());
        
        // Проверяем строку
        StringEntry& strEntry = struct5_3.get<2>();
        if (strcmp(strEntry.toString().c_str(), "Array Test") == 0) {
            Serial.println("Тест 5.3: ПРОЙДЕН");
        } else {
            Serial.println("Тест 5.3: ПРОВАЛЕН - строка не совпадает");
        }
    }
    
    // Тест 5.4: Нулевые массивы
    Serial.println("\n--- Тест 5.4: Нулевые массивы ---");
    {
        Struct3<ByteArrayEntry, ByteArrayBigEntry, UInt16Entry> struct5_4(
            ByteArrayEntry(nullptr, 0),
            ByteArrayBigEntry(nullptr, 0),
            UInt16Entry(0x1234)
        );
        
        uint8_t buffer[50];
        MemoryStream buffer4(buffer, sizeof(buffer));
        //MemoryStream buffer4(50);
        struct5_4.writeTo(buffer4);
        
        
        Serial.print("Записано: ");
        Serial.print(buffer4.getWritten());
        Serial.print(" байт: ");
        buffer4.printHex();//Serial.println(StructDebug::hexDump(buffer4.getData(), buffer4.getWritten()));
        
        buffer4.reset();
        struct5_4.readFrom(buffer4);
        
        Serial.print("Прочитано: ");
        Serial.println(struct5_4.toString());
        
        // Проверяем, что массивы нулевые, а UInt16 прочитался
        ByteArrayEntry& ba1 = struct5_4.get<0>();
        ByteArrayBigEntry& ba2 = struct5_4.get<1>();
        UInt16Entry& num = struct5_4.get<2>();
        
        if (ba1.getSize() == 0 && ba2.getSize() == 0 && num == 0x1234) {
            Serial.println("Тест 5.4: ПРОЙДЕН");
        } else {
            Serial.println("Тест 5.4: ПРОВАЛЕН");
        }
    }
    
    // Тест 5.5: Запись и чтение массива как строки
    Serial.println("\n--- Тест 5.5: Массив как строка ---");
    {
        const char* text = "This is a byte array";
        ByteArrayEntry _byteArray((const uint8_t*)text, strlen(text));
        Struct1<ByteArrayEntry> byteArray(_byteArray );
        
        uint8_t buffer[50];
        MemoryStream buffer5(buffer, sizeof(buffer));
        //MemoryStream buffer5(50);
        byteArray.writeTo(buffer5);
        buffer5.printHex();

        Serial.print("Записано: ");
        Serial.print(buffer5.getWritten());
        Serial.print(" байт: ");
        buffer5.printHex();//Serial.println(StructDebug::hexDump(buffer5.getData(), buffer5.getWritten()));
        
        buffer5.reset();
        byteArray.readFrom(buffer5);
        
        Serial.print("Прочитано: ");
        Serial.println(byteArray.toString());
        
        ByteArrayEntry& readArray = byteArray.get<0>();
        // Проверяем содержимое
        bool testPassed = true;
        if (readArray.getSize() != strlen(text)) {
            testPassed = false;
        } else {
            for (int i = 0; i < strlen(text); i++) {
                if (readArray[i] != text[i]) {
                    testPassed = false;
                    break;
                }
            }
        }
        
        if (testPassed) {
            Serial.println("Тест 5.5: ПРОЙДЕН");
        } else {
            Serial.println("Тест 5.5: ПРОВАЛЕН");
        }
    }
}

void test3(){

    Serial.println("Тест 3: Проверка пропуска нулевых значений");
    {
        Struct3<UInt8Entry, UInt16Entry, Int32Entry> struct3;
        struct3.set<0>(0);       // Не запишется
        struct3.set<1>(0x1234);  // Запишется
        struct3.set<2>(0);       // Не запишется
        
        uint8_t buffer[100];
        MemoryStream stream(buffer, sizeof(buffer));
        Serial.println("Структура: " + struct3.toString());

        if (struct3.writeTo(stream)) {
            Serial.print("Записано (только UInt16): ");
            stream.printHex();
        }
        Serial.println("Проверка чтения нулевых значений");
        // Читаем обратно
        Struct3<UInt8Entry, UInt16Entry, Int32Entry> struct3_read;
        stream.reset();
        if (struct3_read.readFrom(stream)) {
            Serial.println("Прочитано: " + struct3_read.toString());
        }
    }
    
    Serial.println();
}

void test2(){
    
    Serial.println("Тест 2: Структура с несколькими типами");
    {
        Struct3<UInt8Entry, Int16Entry, StringEntry> struct2;
        struct2.set<0>(100);                // UInt8 - числовое значение
        struct2.set<1>(-500);               // Int16 - числовое значение
        struct2.set<2>("Test String");      // String - строка
        
        uint8_t buffer[200];
        MemoryStream stream(buffer, sizeof(buffer));
        
        if (struct2.writeTo(stream)) {
            Serial.println(struct2.toString());
            Serial.print("Записано: ");
            stream.printHex();
            
            // Читаем обратно
            Struct3<UInt8Entry, Int16Entry, StringEntry> struct2_read;
            stream.reset();
            if (struct2_read.readFrom(stream)) {
                Serial.println("Прочитано: " + struct2_read.toString());
            }
        }
    }
    
    Serial.println();
}

void test4(){
    Serial.println("Тест 4: Доступ по индексу");
    {
        Struct3<UInt16Entry, Int32Entry, StringEntry> struct4;
        
        // Используем методы set() с индексом из базового класса
        struct4.set<0>( static_cast<uint64_t>(12345));
        struct4.set<1>( static_cast<int64_t>(-98765));
        struct4.set<2>( "Hello World");
        
        Serial.println("Структура: " + struct4.toString());
        uint8_t buffer[100];
        MemoryStream stream(buffer, sizeof(buffer));
        
        if (struct4.writeTo(stream)) {
            Serial.print("Записано: ");
            stream.printHex();
        }
        Serial.println("Проверка чтения");
        // Читаем обратно
        Struct3<UInt16Entry, Int32Entry, StringEntry> struct_read;
        stream.reset();
        if (struct_read.readFrom(stream)) {
            Serial.println("Прочитано: " + struct_read.toString());
        }

    }
    
    Serial.println();
}

void test1() {

    Serial.println("Тест 1: Простая структура с UInt8");
    {
        // Создаем структуру с конструктором
        //Struct1<UInt8Entry> struct1(UInt8Entry(0xAB));
        Struct1<UInt8Entry> struct1;
        struct1.set<0>( 0xAB );
        
        uint8_t buffer[100];
        MemoryStream stream(buffer, sizeof(buffer));
        
        Serial.print("Исходное значение: ");
        Serial.println(struct1.get<0>().toString());
        
        if (struct1.writeTo(stream)) {
            Serial.print("Структура записана. Данные: ");
            stream.printHex();
            Serial.println("Ожидается: F0 AB 00");
            
            // Читаем обратно
            Struct1<UInt8Entry> struct1_read;
            stream.reset();
            if (struct1_read.readFrom(stream)) {
                Serial.print("Прочитано значение: ");
                Serial.println(struct1_read.get<0>().toString());
            } else {
                Serial.println("Ошибка чтения!");
            }
        }
        
        // Тест метода set() с числовым значением
        Serial.print("До set(0x42): ");
        Serial.println(struct1.get<0>().toString());
        struct1.set<0>(0x42);  // Теперь должно работать!
        Serial.print("После set(0x42): ");
        Serial.println(struct1.get<0>().toString());
        
        // Тест метода set() с объектом
        struct1.set<0>(UInt8Entry(0x55));
        Serial.print("После set(UInt8Entry(0x55)): ");
        Serial.println(struct1.get<0>().toString());
    }
    
    Serial.println();
}