// tests/test.h — tiny self-registering test framework (no dependencies).
#pragma once
#include <cstdio>
#include <utility>
#include <vector>

using TestFn = void (*)();
std::vector<std::pair<const char*, TestFn>>& registry();
extern int g_failures;

#define CHECK(cond) do { if (!(cond)) { std::printf("  FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); ++g_failures; } } while (0)
#define CHECK_EQ(a, b) do { long long _a = (long long)(a), _b = (long long)(b); \
    if (_a != _b) { std::printf("  FAIL %s:%d: %s == %s (%lld vs %lld)\n", __FILE__, __LINE__, #a, #b, _a, _b); ++g_failures; } } while (0)
#define TEST(name) static void name(); \
    static const bool name##_registered = (registry().emplace_back(#name, name), true); \
    static void name()
