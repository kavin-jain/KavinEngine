// ESP32-S3 target: platform.h over the native USB-CDC port, search on core 0.
#include <Arduino.h>
#include <esp_heap_caps.h>
#include <esp_timer.h>
#include "../src/platform.h"
#include "../src/uci.h"

static SemaphoreHandle_t out_mutex = nullptr;
static SemaphoreHandle_t worker_done = nullptr;
static TaskHandle_t worker_task = nullptr;
static void (*worker_fn)(void*) = nullptr;
static void* worker_arg = nullptr;

int64_t now_ms() { return esp_timer_get_time() / 1000; }

bool read_line(std::string& line) {
    line.clear();
    for (;;) {
        int c = Serial.read();
        if (c < 0) { vTaskDelay(1); continue; }
        if (c == '\n') return true;
        if (c != '\r') line += char(c);
    }
}

void write_line(const std::string& line) {
    xSemaphoreTake(out_mutex, portMAX_DELAY);  // search task and UCI task both print
    Serial.write(reinterpret_cast<const uint8_t*>(line.data()), line.size());
    Serial.write('\n');
    xSemaphoreGive(out_mutex);
}

void* alloc_mem(size_t bytes, bool fast) {
    return heap_caps_malloc(bytes, fast ? (MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT) : MALLOC_CAP_SPIRAM);
}

static void worker_entry(void*) {
    worker_fn(worker_arg);
    write_line("info string stack_free " + std::to_string(uxTaskGetStackHighWaterMark(nullptr)));
    xSemaphoreGive(worker_done);
    vTaskDelete(nullptr);
}

void start_worker(void (*fn)(void*), void* arg) {
    join_worker();
    worker_fn = fn;
    worker_arg = arg;
    if (xTaskCreatePinnedToCore(worker_entry, "search", 72 * 1024, nullptr, 1, &worker_task, 0) != pdPASS) {
        worker_task = nullptr;
        write_line("info string ERROR: cannot create 72 KB search task (internal RAM)");
    }
}

void join_worker() {
    if (!worker_task) return;
    xSemaphoreTake(worker_done, portMAX_DELAY);
    worker_task = nullptr;
}

void setup() {
    Serial.setRxBufferSize(4096);
    Serial.begin(115200);
    out_mutex = xSemaphoreCreateMutex();
    worker_done = xSemaphoreCreateBinary();
    disableCore0WDT();  // the search task saturates core 0; don't let the idle watchdog reset the chip
    const int64_t t0 = now_ms();
    engine_init();
    write_line("info string ready init_ms " + std::to_string(now_ms() - t0) +
               " free_internal " + std::to_string(heap_caps_get_free_size(MALLOC_CAP_INTERNAL)) +
               " largest_internal " + std::to_string(heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL)) +
               " free_psram " + std::to_string(heap_caps_get_free_size(MALLOC_CAP_SPIRAM)));
}

void loop() { uci_loop(); }
