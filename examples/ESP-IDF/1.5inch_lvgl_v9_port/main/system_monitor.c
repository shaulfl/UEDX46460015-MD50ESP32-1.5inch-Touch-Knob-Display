#include "system_monitor.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_system.h"
#include "esp_heap_caps.h"
#include "lvgl.h"

static const char *TAG = "system_monitor";

/* FPS tracking variables */
static uint32_t frame_count = 0;
static uint64_t last_fps_time = 0;
static float current_fps = 0.0f;
static uint32_t last_cpu_time = 0;

float compute_core_cpu_load(int core_id, uint32_t sample_ms)
{
    // Snapshot A
    UBaseType_t nA = uxTaskGetNumberOfTasks();
    TaskStatus_t *a = pvPortMalloc(nA * sizeof(TaskStatus_t));
    if (!a) {
        ESP_LOGE(TAG, "Failed to allocate memory for task status A");
        return 0.0f;
    }
    uint64_t totalA = 0;
    nA = uxTaskGetSystemState(a, nA, (uint32_t*)&totalA);

    // Wait sampling window
    vTaskDelay(pdMS_TO_TICKS(sample_ms));

    // Snapshot B
    UBaseType_t nB = uxTaskGetNumberOfTasks();
    TaskStatus_t *b = pvPortMalloc(nB * sizeof(TaskStatus_t));
    if (!b) {
        ESP_LOGE(TAG, "Failed to allocate memory for task status B");
        vPortFree(a);
        return 0.0f;
    }
    uint64_t totalB = 0;
    nB = uxTaskGetSystemState(b, nB, (uint32_t*)&totalB);

    TaskHandle_t idle = xTaskGetIdleTaskHandleForCPU(core_id);
    if (!idle) {
        ESP_LOGE(TAG, "Failed to get idle task handle for core %d", core_id);
        vPortFree(a);
        vPortFree(b);
        return 0.0f;
    }

    // Find Idle task counters in A and B
    uint64_t idleA = 0, idleB = 0;
    for (UBaseType_t i = 0; i < nA; ++i) {
        if (a[i].xHandle == idle) { 
            idleA = a[i].ulRunTimeCounter; 
            break; 
        }
    }
    for (UBaseType_t i = 0; i < nB; ++i) {
        if (b[i].xHandle == idle) { 
            idleB = b[i].ulRunTimeCounter; 
            break; 
        }
    }

    vPortFree(a);
    vPortFree(b);

    uint64_t totalDelta = (totalB >= totalA) ? (totalB - totalA) : 0;
    uint64_t idleDelta  = (idleB  >= idleA) ? (idleB  - idleA) : 0;

    if (totalDelta == 0) return 0.0f;
    float idlePct = (100.0f * (double)idleDelta) / (double)totalDelta;
    float cpuPct  = 100.0f - idlePct;
    if (cpuPct < 0) cpuPct = 0;
    if (cpuPct > 100) cpuPct = 100;
    
    return cpuPct;
}

float get_average_cpu_load(uint32_t sample_ms)
{
    float cpu0 = compute_core_cpu_load(0, sample_ms);
    float cpu1 = compute_core_cpu_load(1, sample_ms);
    return (cpu0 + cpu1) / 2.0f;
}

void system_monitor_init(void)
{
    frame_count = 0;
    last_fps_time = 0;
    current_fps = 0.0f;
    last_cpu_time = 0;
}

void system_monitor_update(lv_obj_t *fps_label, lv_obj_t *cpu_label, lv_obj_t *ram_label)
{
    // Update FPS
    frame_count++;
    uint64_t current_time = esp_timer_get_time();
    if (current_time - last_fps_time >= 1000000) { // Update every second
        current_fps = (float)frame_count * 1000000.0f / (current_time - last_fps_time);
        frame_count = 0;
        last_fps_time = current_time;
        
        if (fps_label) {
            char buf[32];
            snprintf(buf, sizeof(buf), "FPS: %.1f", current_fps);
            lv_label_set_text(fps_label, buf);
            lv_obj_invalidate(fps_label);  // Force refresh
        }
    }
    
    // Update CPU usage
    if (current_time - last_cpu_time >= 2000000) { // Update every 2 seconds to avoid overhead
        last_cpu_time = current_time;
        
        size_t free_heap = esp_get_free_heap_size();
        
        if (cpu_label) {
            char buf[32];
            // Get real CPU usage from FreeRTOS run-time stats
            float cpu0_usage = compute_core_cpu_load(0, 100);  // Sample for 100ms
            float cpu1_usage = compute_core_cpu_load(1, 100);  // Sample for 100ms
            float avg_cpu = (cpu0_usage + cpu1_usage) / 2.0f;
            
            snprintf(buf, sizeof(buf), "CPU: %.1f%%", avg_cpu);
            lv_label_set_text(cpu_label, buf);
            lv_obj_invalidate(cpu_label);  // Force refresh
        }
        
        if (ram_label) {
            char buf[32];
            // Use enhanced RAM monitoring with percentage
            float ram_free_pct = get_heap_free_percent();
            snprintf(buf, sizeof(buf), "RAM: %.1f%%", ram_free_pct);
            lv_label_set_text(ram_label, buf);
            lv_obj_invalidate(ram_label);  // Force refresh
        }
    }
}

void system_monitor_reset(void)
{
    frame_count = 0;
    last_fps_time = 0;
    current_fps = 0.0f;
    last_cpu_time = 0;
}

float get_heap_free_percent(void)
{
    size_t free_b  = heap_caps_get_free_size(MALLOC_CAP_8BIT);    // bytes free
    size_t total_b = heap_caps_get_total_size(MALLOC_CAP_8BIT);   // bytes total
    float free_pct = total_b ? (100.0f * (float)free_b / (float)total_b) : 0.0f;
    return free_pct;
}

float get_internal_ram_free_percent(void)
{
    size_t int_free  = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
    size_t int_total = heap_caps_get_total_size(MALLOC_CAP_INTERNAL);
    float int_free_pct = int_total ? (100.0f * (float)int_free / (float)int_total) : 0.0f;
    return int_free_pct;
}

float get_psram_free_percent(void)
{
    size_t ps_free  = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
    size_t ps_total = heap_caps_get_total_size(MALLOC_CAP_SPIRAM);
    float ps_free_pct = ps_total ? (100.0f * (float)ps_free / (float)ps_total) : 0.0f;
    return ps_free_pct;
}