#include "DataTypes.h"
#include "StructBuilder.h"
#include "CompactPhone.h"

#include "extra/MemoryStream.h"


void setup() {
    Serial.begin(115200);
    delay(1000);
    
    Serial.println("=== Тест структуры для Phone DB ===");
    Serial.println();

    PackedPhone myPhone( 79166014794ULL, 3, 131 );
    const char* desc = "Сергей";
    size_t desc_len = strlen(desc); // Без нулевого символа

    Serial.printf("Создаем запись: флаг=%u,", myPhone.flags() );
    Serial.printf( "группа=%u", myPhone.group());
    Serial.printf(", номер=%llu", myPhone.phone());

    Serial.print(", описание='");
    Serial.print(desc);
    Serial.println("'");

    Struct< UInt64Entry /* Flag & Group & phone */, StringEntry /* Name/Comment */> record;
    
    record.set<0>(UInt64Entry(myPhone.raw())); // Используем raw()
    Serial.println("Set 0");
    record.set<1>(StringEntry(desc, desc_len));
    Serial.println("Set 1");

    uint8_t buffer[200];
    MemoryStream stream(buffer, sizeof(buffer));
    
    if (record.writeTo(stream)) {
        Serial.print("Структура записана. Размер: ");
        //Serial.print(record.getTotalSize());
        Serial.print(" байт. Данные: ");
        stream.printHex();
        
        // Читаем обратно
        Struct2< UInt64Entry, StringEntry> testRead;
        stream.reset();
        
        if (testRead.readFrom(stream)) {
            Serial.println("Чтение успешно!");
            
            PackedPhone testGP(testRead.get<0>());
            auto testDescr = testRead.get<1>();
            
            Serial.printf("\nПрочитанные данные %s:\n", testGP.isValid() ? "корректны" : "повреждены");
            Serial.print("Флаг: ");
            Serial.println( testGP.flags() );
            Serial.print("Телефон: ");
            Serial.println( testGP.phone() );
            Serial.print("Группа: ");
            Serial.println( testGP.group() );
            Serial.print("Описание: ");
            Serial.println( testDescr.toString().c_str() );

            Serial.println("======== Тест phoneStr ====== ");
            Serial.printf("Телефон строкой: %s\n", testGP.phoneStr().c_str() );  
            
            // Проверяем совпадение
            if (
                testGP.raw() == myPhone.raw() && 
                strcmp(testDescr.toString().c_str(), desc) == 0) {
                Serial.println("✓ Все данные совпадают!");
            } else {
                Serial.println("✗ Данные не совпадают!");
            }
                
        } else {
            Serial.println("Ошибка чтения структуры!");
        }

    } else {
        Serial.println("Ошибка записи в поток!");
    }
    
    Serial.println("==== Тест сравнения со строкой ==========");
    PackedPhone other( "+7 (916) 601-47-94");
    if ( myPhone == other ){
        Serial.println("\nТест Ok : телефоны совпали");    
    } else {
        Serial.println("\nТест Fail : телефоны не совпали");    
    }

    Serial.println("\n=== Тест завершен ===");
}

void loop() {
    delay(1000);
}