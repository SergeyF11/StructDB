#ifndef BENCHMARK_H
#define BENCHMARK_H

#include "StructBuilder.h"

class TestStream : public Stream {
private:
    uint8_t* buffer;
    size_t capacity;
    size_t position;
    size_t writePosition;
    bool allowOverflow;
    
public:
    TestStream(size_t size = 4096) 
        : capacity(size), position(0), writePosition(0), allowOverflow(false) {
        buffer = new uint8_t[size];
    }
    
    ~TestStream() {
        delete[] buffer;
    }
    
    void rewind() {
        position = 0;
        writePosition = 0;
    }
    
    void clear() {
        rewind();
        memset(buffer, 0, capacity);
    }
    
    size_t getWriteSize() const {
        return writePosition;
    }
    
    int available() override {
        return writePosition - position;
    }
    
    int read() override {
        if (position >= writePosition) return -1;
        return buffer[position++];
    }
    
    int peek() override {
        if (position >= writePosition) return -1;
        return buffer[position];
    }
    
    size_t write(uint8_t value) override {
        if (writePosition >= capacity && !allowOverflow) return 0;
        if (writePosition < capacity) {
            buffer[writePosition++] = value;
            return 1;
        }
        return 0;
    }
    
    size_t write(const uint8_t* data, size_t size) override {
        if (writePosition + size > capacity && !allowOverflow) return 0;
        size_t toWrite = min(size, capacity - writePosition);
        memcpy(buffer + writePosition, data, toWrite);
        writePosition += toWrite;
        return toWrite;
    }
    
    void printWrittenData() {
        Serial.print("[");
        Serial.print(writePosition);
        Serial.print(" байт]: ");
        
        for (size_t i = 0; i < writePosition && i < 128; i++) {
            if (i > 0) Serial.print(" ");
            if (buffer[i] < 0x10) Serial.print("0");
            Serial.print(buffer[i], HEX);
        }
        
        if (writePosition > 128) Serial.print(" ...");
        Serial.println();
    }
    
    void setAllowOverflow(bool allow) {
        allowOverflow = allow;
    }
};

class Benchmark {
public:
    static void runStructBenchmark() {
        Serial.println("\n=== Benchmarking Struct Library ===\n");
        
        TestStream stream(8192);
        Struct3<FloatEntry, FloatEntry, UInt32Entry> record;
        
        // Warm up
        for (int i = 0; i < 10; i++) {
            stream.rewind();
            record.set<0>(i * 1.5f);
            record.set<1>(i * 2.5f);
            record.set<2>(i);
            record.writeTo(stream);
        }
        
        // Write benchmark
        unsigned long writeStart = micros();
        const int iterations = 1000;
        
        for (int i = 0; i < iterations; i++) {
            stream.rewind();
            record.set<0>(i * 1.5f);
            record.set<1>(i * 2.5f);
            record.set<2>(i);
            record.writeTo(stream);
        }
        
        unsigned long writeTime = micros() - writeStart;
        
        // Read benchmark
        unsigned long readStart = micros();
        
        for (int i = 0; i < iterations; i++) {
            stream.rewind();
            record.readFrom(stream);
        }
        
        unsigned long readTime = micros() - readStart;
        
        // Memory usage
        size_t structSize = sizeof(record);
        size_t streamSize = stream.getWriteSize();
        
        Serial.println("Results:");
        Serial.printf("  Struct size: %zu bytes\n", structSize);
        Serial.printf("  Serialized size per record: %zu bytes\n", streamSize);
        Serial.printf("  Write performance: %lu us total, %.2f us/record\n", 
                     writeTime, (float)writeTime / iterations);
        Serial.printf("  Read performance: %lu us total, %.2f us/record\n", 
                     readTime, (float)readTime / iterations);
        Serial.printf("  Throughput: %.2f records/second\n", 
                     (iterations * 1000000.0) / (writeTime + readTime));
        
        // Test with different struct sizes
        benchmarkStructSizes();
    }
    
private:
    static void benchmarkStructSizes() {
        Serial.println("\n=== Benchmarking Different Struct Sizes ===\n");
        
        TestStream stream(16384);
        
        // Struct1 benchmark
        {
            Struct1<FloatEntry> s1;
            unsigned long start = micros();
            for (int i = 0; i < 1000; i++) {
                stream.rewind();
                s1.set<0>(i * 1.1f);
                s1.writeTo(stream);
                s1.readFrom(stream);
            }
            unsigned long time = micros() - start;
            Serial.printf("Struct1<Float>: %.2f us/operation\n", time / 2000.0);
        }
        
        // Struct2 benchmark  
        {
            Struct2<FloatEntry, StringEntry> s2;
            unsigned long start = micros();
            for (int i = 0; i < 1000; i++) {
                stream.rewind();
                s2.set<0>(i * 1.1f);
                s2.set<1>("Test String");
                s2.writeTo(stream);
                s2.readFrom(stream);
            }
            unsigned long time = micros() - start;
            Serial.printf("Struct2<Float, String>: %.2f us/operation\n", time / 2000.0);
        }
        
        // Struct4 benchmark
        {
            Struct4<UInt32Entry, FloatEntry, DoubleEntry, StringEntry> s4;
            unsigned long start = micros();
            for (int i = 0; i < 1000; i++) {
                stream.rewind();
                s4.set<0>(i);
                s4.set<1>(i * 1.1f);
                s4.set<2>(i * 1.111);
                s4.set<3>("Longer Test String");
                s4.writeTo(stream);
                s4.readFrom(stream);
            }
            unsigned long time = micros() - start;
            Serial.printf("Struct4<UInt32, Float, Double, String>: %.2f us/operation\n", time / 2000.0);
        }
    }
};

#endif // BENCHMARK_H