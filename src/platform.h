#pragma once
#include <cstddef>
#include <cstdint>
#include <string>

// Everything that differs between PC and ESP32 lives behind these functions.
int64_t now_ms();
bool read_line(std::string& line);                // blocking; false on EOF
void write_line(const std::string& line);         // appends '\n'; whole line is written atomically
void* alloc_mem(size_t bytes, bool fast);         // fast = internal SRAM on ESP32; free with std::free
void start_worker(void (*fn)(void*), void* arg);  // run fn(arg) on the background search thread
void join_worker();                               // wait for that thread (no-op if none running)
