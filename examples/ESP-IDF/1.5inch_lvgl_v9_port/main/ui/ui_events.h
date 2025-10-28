#ifndef UI_EVENTS_H
#define UI_EVENTS_H

/* Synthetic event codes for knob events to avoid enum collisions with button_event_t.
 * These are high values intentionally chosen not to overlap with existing button enums.
 */
#define EVT_KNOB_LEFT  1001
#define EVT_KNOB_RIGHT 1002

#endif // UI_EVENTS_H