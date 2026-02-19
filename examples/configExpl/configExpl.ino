#include "ConfigExample.h"

DeviceConfig config; //( ConfigStruct(1, "ESP test", 0.0f, 20 ) );


void setup(){
    Serial.begin(115200);
    delay(500);

    Serial.println("Test config ");

    if (!LittleFS.begin()) {
        Serial.println("LittleFS mount failed");
        // Возможно, здесь стоит попробовать отформатировать
        LittleFS.format();
        if (!LittleFS.begin()) {
            Serial.println("LittleFS mount failed after format");
           //return;
        }
    }

    if ( ! config.load() ){
        Serial.println("Wrong read config");
    } else {
        Serial.println( config.toString() );
    }

}

void loop(){
    delay(1000);
}