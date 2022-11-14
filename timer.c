#include <furi.h>
#include <gui/gui.h>
#include <input/input.h>
#include <notification/notification_messages.h>

// Event types: timer and input
typedef enum { EventTypeTick, EventTypeInput } EventType;
// Timer status
typedef enum { TimerTicking, TimerStopped, TimerAlarm, TimerEditing } TimerStatus;
// What part of time we're editing
typedef enum { Hours, Minutes, Seconds } TimeEditing;
// Time for displaying
typedef struct {
    int hours;
    int minutes;
    int seconds;
} Time;
// Event types we use
typedef struct {
    EventType type;
    InputEvent input;
} TimerEvent;

const NotificationSequence sequence_alarm = {
    &message_red_255,
    &message_vibro_on,
    &message_note_f4,
    &message_delay_500,
    NULL,
};

bool cursor_visible = 0;
TimeEditing timeediting = Minutes;

void viewport_input_callback(InputEvent* event, void* ctx) {
    furi_assert(ctx);
    FuriMessageQueue* queue = ctx;
    TimerEvent timer_event = {.type = EventTypeInput, .input = *event};
    furi_message_queue_put(queue, &timer_event, FuriWaitForever);
}

void viewport_draw_callback(Canvas* canvas, void* ctx) {
    furi_assert(ctx);

    canvas_clear(canvas);
    canvas_set_font(canvas, FontBigNumbers);

    Time* timeptr = ctx;
    uint8_t x_pos = 20;
    uint8_t y_pos = 40;
    uint8_t width = canvas_string_width(canvas, "00");

    char seconds_str[10];
    snprintf(
        seconds_str, 10, "%02d:%02d:%02d", timeptr->hours, timeptr->minutes, timeptr->seconds);
    canvas_draw_str(canvas, x_pos, y_pos, seconds_str);
    if(cursor_visible) {
        canvas_draw_box(canvas, x_pos + timeediting * 36, y_pos + 2, width, 2);
    }
}

void timer_tick(FuriMessageQueue* event_queue) {
    furi_assert(event_queue);
    FuriMessageQueue* queue = event_queue;
    TimerEvent timer_event = {.type = EventTypeTick};
    furi_message_queue_put(queue, &timer_event, FuriWaitForever);
}

void update_time(Time* timeptr, int seconds_total) {
    timeptr->hours = seconds_total / (60 * 60);
    timeptr->minutes = (seconds_total - timeptr->hours * 60 * 60) / 60;
    timeptr->seconds = (seconds_total - timeptr->hours * 60 * 60 - timeptr->minutes * 60);
}

bool short_or_repeated(InputType input_type) {
    return (input_type == InputTypeShort) | (input_type == InputTypeRepeat);
}

int32_t timer_app(void* p) {
    UNUSED(p);

    TimerStatus timer_status = TimerStopped;
    // TimerStatus timer_status = TimerTicking;

    // Variables for use
    TimerEvent event;
    FuriMessageQueue* queue = furi_message_queue_alloc(8, sizeof(TimerEvent));
    Time time = {.hours = 0, .minutes = 1, .seconds = 0};
    // Time time = {.hours = 0, .minutes = 0, .seconds = 3};
    int seconds_total;
    seconds_total = time.hours * 60 * 60 + time.minutes * 60 + time.seconds;
    uint16_t edit_multiplier = 60;

    // Update the displayed time on startup
    update_time(&time, seconds_total);

    // GUI init
    ViewPort* vp = view_port_alloc();
    view_port_draw_callback_set(vp, viewport_draw_callback, &time);
    view_port_input_callback_set(vp, viewport_input_callback, queue);
    Gui* gui = furi_record_open(RECORD_GUI);
    gui_add_view_port(gui, vp, GuiLayerFullscreen);

    // Timer
    FuriTimer* timer = furi_timer_alloc(timer_tick, FuriTimerTypePeriodic, queue);
    furi_timer_start(timer, 1000);

    // Notifications for the alarm when we reach 00:00:00
    NotificationApp* notifications = furi_record_open(RECORD_NOTIFICATION);

    // Main loop
    while(1) {
        update_time(&time, seconds_total);
        furi_check(furi_message_queue_get(queue, &event, FuriWaitForever) == FuriStatusOk);
        if(event.type == EventTypeInput) {
            InputKey key = event.input.key;
            InputType input_type = event.input.type;

            // Turn the alarm off on any key pressed
            if(timer_status == TimerAlarm) {
                timer_status = TimerStopped;
            }

            if(key == InputKeyBack) {
                timer_status = TimerStopped;
                break;
            }

            if((key == InputKeyOk) & (input_type == InputTypeShort)) {
                if((timer_status == TimerStopped) & (seconds_total > 0)) {
                    timer_status = TimerTicking;
                } else if(timer_status == TimerTicking) {
                    timer_status = TimerStopped;
                } else if(timer_status == TimerEditing) {
                    timer_status = TimerStopped;
                    cursor_visible = 0;
                }
            } else if((key == InputKeyOk) & (input_type == InputTypeLong)) {
                if(timer_status == TimerEditing) {
                    timer_status = TimerStopped;
                    cursor_visible = 0;
                } else {
                    timer_status = TimerEditing;
                }
            } else if(
                (key == InputKeyUp) & (timer_status == TimerEditing) &
                short_or_repeated(input_type)) {
                seconds_total = seconds_total + edit_multiplier;

            } else if(
                (key == InputKeyDown) & (timer_status == TimerEditing) &
                short_or_repeated(input_type)) {
                seconds_total = seconds_total - edit_multiplier;
                if(seconds_total < 0) {
                    seconds_total = 0;
                }
            } else if((key == InputKeyLeft) & (input_type == InputTypeShort)) {
                // move the cursor left
                if((timeediting > 0) & (timer_status == TimerEditing)) {
                    timeediting--;

                    switch(timeediting) {
                    case Hours:
                        edit_multiplier = 3600;
                        break;
                    case Minutes:
                        edit_multiplier = 60;
                        break;
                    case Seconds:
                        edit_multiplier = 1;
                        break;
                    default:
                        break;
                    }
                }
            } else if((key == InputKeyRight) & (input_type == InputTypeShort)) {
                if((timeediting < 2) & (timer_status == TimerEditing)) {
                    timeediting++;
                    switch(timeediting) {
                    case Hours:
                        edit_multiplier = 3600;
                        break;
                    case Minutes:
                        edit_multiplier = 60;
                        break;
                    case Seconds:
                        edit_multiplier = 1;
                        break;
                    default:
                        break;
                    }
                }
            }
        } else if(event.type == EventTypeTick) {
            if(timer_status == TimerTicking) {
                seconds_total--;
            } else if(timer_status == TimerAlarm) {
                notification_message(notifications, &sequence_alarm);
            } else if(timer_status == TimerEditing) {
                cursor_visible = !cursor_visible;
            }

            if((seconds_total <= 0) & (timer_status == TimerTicking)) {
                timer_status = TimerAlarm;
            }
        }
    }

    // Cleanup
    furi_message_queue_free(queue);
    gui_remove_view_port(gui, vp);
    view_port_free(vp);
    furi_record_close(RECORD_NOTIFICATION);
    furi_record_close(RECORD_GUI);
    furi_timer_free(timer);

    return 0;
}
