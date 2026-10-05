// tests/test_main.cpp
#include "test.h"
#include "../src/board.h"

std::vector<std::pair<const char*, TestFn>>& registry() {
    static std::vector<std::pair<const char*, TestFn>> r;
    return r;
}
int g_failures = 0;

int main() {
    init_bitboards(); Board::init();  // Task 9 replaces this with engine_init()
    for (auto& t : registry()) {
        int before = g_failures;
        t.second();
        std::printf("%s %s\n", g_failures == before ? "ok  " : "FAIL", t.first);
    }
    if (g_failures) { std::printf("%d check(s) failed\n", g_failures); return 1; }
    std::printf("all tests passed\n");
    return 0;
}
