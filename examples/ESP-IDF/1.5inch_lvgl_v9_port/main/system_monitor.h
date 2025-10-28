#ifndef SYSTEM_MONITOR_H
#define SYSTEM_MONITOR_H

#include <stdint.h>

// Forward declaration for LVGL object type
typedef struct _lv_obj_t lv_obj_t;

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize system monitoring
 */
void system_monitor_init(void);

/**
 * @brief Update system information (FPS, CPU, RAM)
 * @param fps_label LVGL label object for FPS display
 * @param cpu_label LVGL label object for CPU display
 * @param ram_label LVGL label object for RAM display
 */
void system_monitor_update(lv_obj_t *fps_label, lv_obj_t *cpu_label, lv_obj_t *ram_label);

/**
 * @brief Get overall heap memory percentage (8-bit heap)
 * @return Free memory percentage (0.0 - 100.0)
 */
float get_heap_free_percent(void);

/**
 * @brief Get internal RAM memory percentage
 * @return Free internal RAM percentage (0.0 - 100.0)
 */
float get_internal_ram_free_percent(void);

/**
 * @brief Get PSRAM memory percentage (if available)
 * @return Free PSRAM percentage (0.0 - 100.0)
 */
float get_psram_free_percent(void);

/**
 * @brief Reset system monitoring variables
 */
void system_monitor_reset(void);

/**
 * @brief Compute CPU load for a specific core using FreeRTOS run-time stats
 *
 * @param core_id Core ID (0 or 1 for ESP32-S3)
 * @param sample_ms Sampling window in milliseconds
 * @return CPU usage percentage (0.0 - 100.0)
 */
float compute_core_cpu_load(int core_id, uint32_t sample_ms);

/**
 * @brief Get average CPU usage across all cores
 *
 * @param sample_ms Sampling window in milliseconds
 * @return Average CPU usage percentage (0.0 - 100.0)
 */
float get_average_cpu_load(uint32_t sample_ms);

#ifdef __cplusplus
}
#endif

#endif // SYSTEM_MONITOR_H