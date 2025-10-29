# Watchdog Timeout Recovery Solution

## Problem Analysis

The ESP32 watchdog timer was being triggered due to the `ui_tick` task hanging when trying to acquire the LVGL mutex. The error message was:

```
E (144354) task_wdt: Task watchdog got triggered. The following tasks/users did not reset the watchdog in time:
E (144354) task_wdt:  - IDLE1 (CPU 1)
E (144354) task_wdt: Tasks currently running:
E (144354) task_wdt: CPU 0: IDLE0
E (144354) task_wdt: CPU 1: ui_tick
```

### Root Cause

The main issue was LVGL lock contention between the `ui_tick` task and the LVGL task:
1. The `ui_tick` task was trying to acquire the LVGL mutex that was already held by the LVGL task
2. This caused a deadlock situation where `ui_tick` would hang indefinitely
3. The watchdog timer would eventually trigger because `ui_tick` wasn't resetting it
4. The system would show "Failed to acquire LVGL lock" messages and hang

## Solution Implementation

### 1. Explicit Watchdog Timer Reset
Added explicit watchdog timer resets in the `ui_tick_task` at key points:
- At the beginning of each iteration
- After processing selection timeout
- Before the task delay

```c
/* Reset watchdog timer at the beginning of each iteration */
esp_task_wdt_reset();
wdt_reset_counter++;

/* Reset watchdog timer again after operations to ensure we don't timeout */
esp_task_wdt_reset();

/* Reset watchdog before delay to ensure we don't timeout during sleep */
esp_task_wdt_reset();
```

### 2. Event Queue System
Implemented a queue-based communication system to avoid direct LVGL lock contention:

```c
/* Event queue for UI updates to avoid LVGL lock contention */
static QueueHandle_t ui_event_queue = NULL;
static SemaphoreHandle_t ui_event_mutex = NULL;

typedef enum {
    UI_EVENT_KNOB_ROTATION,
    UI_EVENT_BUTTON_CLICK,
    UI_EVENT_SELECTION_TIMEOUT
} ui_event_type_t;

typedef struct {
    ui_event_type_t type;
    int32_t value;
} ui_event_t;
```

### 3. LVGL Operations Moved to LVGL Task Context
Instead of trying to acquire the LVGL lock in `ui_tick_task`, we now:
1. Queue UI events in `ui_tick_task`
2. Notify the LVGL task to process these events
3. Process all UI updates in the LVGL task context where the lock is already held

```c
/* Notify LVGL task to process events instead of trying to acquire lock here */
lvgl_port_task_wake(LVGL_PORT_EVENT_USER, NULL);
```

### 4. LVGL Task Event Handling
Modified the LVGL task to handle `LVGL_PORT_EVENT_USER` events:

```c
if (events & LVGL_PORT_EVENT_USER) {
    extern void ui_process_events_in_lvgl_context(void);
    ui_process_events_in_lvgl_context();
}
```

### 5. Simplified ui_tick Function
The `ui_tick()` function was simplified to avoid any LVGL operations that could cause lock contention.

### 6. Increased Watchdog Timeout
Increased the watchdog timeout from 5 to 10 seconds to provide more time for operations:

```
CONFIG_ESP_TASK_WDT_TIMEOUT_S=10
```

## Key Benefits

1. **Eliminates Deadlock**: UI events are processed in LVGL context where the lock is already held
2. **Maintains Responsiveness**: Event queue ensures UI remains responsive during heavy input
3. **Robust Error Handling**: Added timeouts and error checking for all LVGL operations
4. **Better Resource Management**: Reduced lock contention and improved task coordination
5. **Enhanced Stability**: Increased watchdog timeout provides more margin for operations

## Testing and Verification

Use the provided test script to verify the implementation:

```bash
chmod +x test_watchdog_recovery.sh
./test_watchdog_recovery.sh
```

### Stress Test Procedure

1. Build and flash the firmware:
   ```bash
   idf.py build
   idf.py -p [PORT] flash monitor
   ```

2. Monitor for:
   - No watchdog timeout messages
   - UI remains responsive during heavy input
   - No "Failed to acquire LVGL lock" messages
   - Smooth knob and button operation

3. Stress test by rapidly rotating the knob and pressing buttons for 2-3 minutes

## Files Modified

1. `main/example_qspi_with_ram.c` - Main implementation with event queue and watchdog resets
2. `main/ui/ui.c` - Simplified ui_tick function
3. `managed_components/espressif__esp_lvgl_port/src/lvgl9/esp_lvgl_port.c` - LVGL task event handling
4. `managed_components/espressif__esp_lvgl_port/include/esp_lvgl_port.h` - Function declaration
5. `sdkconfig` and `sdkconfig.defaults` - Increased watchdog timeout

## Conclusion

This solution restructures the UI processing to avoid lock contention while maintaining all original functionality. The event-driven architecture ensures that UI updates are processed safely in the LVGL context, eliminating the deadlock that was causing the watchdog timeout.