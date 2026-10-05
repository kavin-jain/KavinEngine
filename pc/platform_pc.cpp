// pc/platform_pc.cpp — platform.h for macOS/Linux.
#include "../src/platform.h"
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <thread>

int64_t now_ms() {
    using namespace std::chrono;
    return duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
}

bool read_line(std::string& line) { return bool(std::getline(std::cin, line)); }

void write_line(const std::string& line) {
    std::string out = line + '\n';
    std::fwrite(out.data(), 1, out.size(), stdout);  // one call: stdio locks it, so lines never interleave
    std::fflush(stdout);
}

void* alloc_mem(size_t bytes, bool) { return std::malloc(bytes); }

static std::thread worker;
void start_worker(void (*fn)(void*), void* arg) { join_worker(); worker = std::thread(fn, arg); }
void join_worker() { if (worker.joinable()) worker.join(); }
