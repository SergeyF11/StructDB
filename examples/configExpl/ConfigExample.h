#ifndef CONFIG_EXAMPLE_H
#define CONFIG_EXAMPLE_H

#include "StructBuilder.h"
#include "LittleFS.h"

// Example config structure
using ConfigStruct = Struct4<
    UInt32Entry,    // version (for migration)
    StringEntry,    // deviceName
    FloatEntry,     // temperatureOffset
    UInt8Entry      // brightness (0-100)
>;

class DeviceConfig {
private:
    ConfigStruct config;
    const char* filename;
    
public:
    DeviceConfig(const char* filename = "/config.dat") 
        : config(1, "ESP32-Device", 0.0f, 100), filename(filename) {}

    // DeviceConfig(ConfigStruct config, const char* filename = "/config.dat") 
    //     : config(config), filename(filename) {}
    
    bool load() {
        if (!LittleFS.begin()) {
            Serial.println("Failed to mount LittleFS");
            return false;
        }
        
        if (!LittleFS.exists(filename)) {
            Serial.println("Config file doesn't exist, using defaults");
            return save();  // Create with defaults
        }
        
        File file = LittleFS.open(filename, "r");
        if (!file) {
            Serial.println("Failed to open config file");
            return false;
        }
        
        bool success = config.readFrom(file);
        file.close();
        
        if (!success) {
            Serial.println("Failed to read config");
            // Try to recover by saving defaults
            return save();
        }
        
        Serial.println("Config loaded successfully");
        return true;
    }
    
    bool save() {
        if (!LittleFS.begin()) {
            Serial.println("Failed to mount LittleFS");
            return false;
        }
        
        File file = LittleFS.open(filename, "w");
        if (!file) {
            Serial.println("Failed to create config file");
            return false;
        }
        
        bool success = config.writeTo(file);
        file.close();
        
        if (success) {
            Serial.println("Config saved successfully");
        } else {
            Serial.println("Failed to save config");
        }
        
        return success;
    }
    
    // Getters
    uint32_t getVersion() const { return config.get<0>(); }
    String getDeviceName() const { return config.get<1>().toString(); }
    float getTemperatureOffset() const { return config.get<2>(); }
    uint8_t getBrightness() const { return config.get<3>(); }
    
    // Setters
    void setDeviceName(const String& name) { 
        config.set<1>(name.c_str()); 
        save();  // Auto-save on change
    }
    
    void setTemperatureOffset(float offset) { 
        config.set<2>(offset); 
        save();
    }
    
    void setBrightness(uint8_t brightness) { 
        if (brightness <= 100) {
            config.set<3>(brightness); 
            save();
        }
    }
    
    String toString() const {
        return config.toString();
    }
};

// Migration example for config
class ConfigMigrationV1toV2 : public SchemaMigrator::Migration {
private:
    using OldConfig = Struct3<StringEntry, FloatEntry, UInt8Entry>;
    using NewConfig = Struct4<UInt32Entry, StringEntry, FloatEntry, UInt8Entry>;
    
public:
    uint16_t getFromVersion() const override { return 1; }
    uint16_t getToVersion() const override { return 2; }
    
    bool migrate(Stream& src, Stream& dest) override {
        OldConfig oldConfig;
        if (!oldConfig.readFrom(src)) return false;
        
        // Create new config with version 2 and copied values
        NewConfig newConfig(
            2,  // version
            oldConfig.get<0>(),  // deviceName
            oldConfig.get<1>(),  // temperatureOffset  
            oldConfig.get<2>()   // brightness
        );
        
        return newConfig.writeTo(dest);
    }
};

#endif // CONFIG_EXAMPLE_H