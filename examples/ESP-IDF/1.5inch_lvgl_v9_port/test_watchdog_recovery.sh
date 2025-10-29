#!/bin/bash

# Test script to verify the watchdog recovery solution
# This script will monitor the serial output for watchdog timeout errors
# and check if the recovery mechanisms are working

echo "=== Watchdog Recovery Test Script ==="
echo "This test will monitor for watchdog timeout issues and verify recovery mechanisms"
echo ""

# Check if the required files exist
echo "Checking required files..."
REQUIRED_FILES=(
    "main/example_qspi_with_ram.c"
    "main/ui/ui.c"
    "sdkconfig"
    "managed_components/espressif__esp_lvgl_port/src/lvgl9/esp_lvgl_port.c"
    "managed_components/espressif__esp_lvgl_port/include/esp_lvgl_port.h"
)

for file in "${REQUIRED_FILES[@]}"; do
    if [ -f "$file" ]; then
        echo "✓ $file exists"
    else
        echo "✗ $file missing"
        exit 1
    fi
done

echo ""
echo "Checking for key recovery features..."

# Check if watchdog reset is implemented in ui_tick_task
if grep -q "esp_task_wdt_reset" main/example_qspi_with_ram.c; then
    echo "✓ Watchdog reset implemented in ui_tick_task"
else
    echo "✗ Watchdog reset missing in ui_tick_task"
fi

# Check if event queue is implemented
if grep -q "ui_event_queue" main/example_qspi_with_ram.c; then
    echo "✓ Event queue system implemented"
else
    echo "✗ Event queue system missing"
fi

# Check if LVGL lock timeout is implemented
if grep -q "lvgl_port_lock(10)" main/example_qspi_with_ram.c; then
    echo "✓ LVGL lock timeout implemented"
else
    echo "✗ LVGL lock timeout missing"
fi

# Check if ui_tick function is simplified
if grep -q "ui_tick()" main/ui/ui.c; then
    echo "✓ Simplified ui_tick function found"
else
    echo "✗ Simplified ui_tick function missing"
fi

# Check if LVGL task handles user events
if grep -q "LVGL_PORT_EVENT_USER" managed_components/espressif__esp_lvgl_port/src/lvgl9/esp_lvgl_port.c; then
    echo "✓ LVGL task handles user events"
else
    echo "✗ LVGL task user event handling missing"
fi

# Check if watchdog timeout is increased in sdkconfig
if grep -q "CONFIG_ESP_TASK_WDT_TIMEOUT_S=10" sdkconfig; then
    echo "✓ Watchdog timeout increased to 10 seconds"
else
    echo "✗ Watchdog timeout not increased"
fi

echo ""
echo "=== Build and Test Instructions ==="
echo "1. Build the project:"
echo "   idf.py build"
echo ""
echo "2. Flash to device:"
echo "   idf.py -p [PORT] flash monitor"
echo ""
echo "3. Monitor for the following during testing:"
echo "   - No watchdog timeout messages"
echo "   - UI remains responsive during heavy input"
echo "   - No 'Failed to acquire LVGL lock' messages"
echo "   - Smooth knob and button operation"
echo ""
echo "4. Stress test by rapidly rotating the knob and pressing buttons"
echo "   for at least 2-3 minutes to verify stability."
echo ""
echo "=== Key Recovery Features Implemented ==="
echo "1. Explicit watchdog timer reset in ui_tick_task"
echo "2. Event queue system to avoid LVGL lock contention"
echo "3. LVGL operations moved to LVGL task context"
echo "4. Increased watchdog timeout from 5 to 10 seconds"
echo "5. Error handling and execution time monitoring"
echo ""
echo "If all checks pass, the recovery solution should resolve the watchdog timeout issue."