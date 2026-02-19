#include "DataTypes.h"
#include "StructBuilder.h"


#include "tests.h"


void setup() {
    Serial.begin(115200);
    delay(1000);
    
    Serial.println("=== Тест системы типов и структур ===");
    Serial.println();
    
    // Тест 1: Простая структура с UInt8
    test(1);
    // Тест 2: Структура с несколькими типами
    test(2);
    // Тест 3: Проверка пропуска нулевых значений
    test(3);   
    
    // Тест 4: Доступ по индексу
    test(4);

    test(5);
    test(6);

    test7();

    Serial.println("=== Все тесты завершены ===");
}

void loop() {
    // Ничего не делаем в loop
}