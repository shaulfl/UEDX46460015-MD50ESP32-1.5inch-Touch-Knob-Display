#include <stdio.h>
#include "ui/screens.h"

// Simple test function to verify volume control
void test_volume_control() {
    printf("Testing volume control...\n");
    
    // Test initial volume
    int initial_volume = get_volume_value();
    printf("Initial volume: %d\n", initial_volume);
    
    // Test setting volume to 50
    set_volume_value(50);
    int test_volume = get_volume_value();
    printf("Set volume to 50, got: %d\n", test_volume);
    
    // Test setting volume to 0
    set_volume_value(0);
    test_volume = get_volume_value();
    printf("Set volume to 0, got: %d\n", test_volume);
    
    // Test setting volume to 100
    set_volume_value(100);
    test_volume = get_volume_value();
    printf("Set volume to 100, got: %d\n", test_volume);
    
    // Restore initial volume
    set_volume_value(initial_volume);
    printf("Restored initial volume: %d\n", get_volume_value());
    
    printf("Volume control test completed.\n");
}