#include "linux_input.h"

#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>

#if !USE_DESKTOP
#include <cerrno>
#include <dirent.h>
#include <fcntl.h>
#include <linux/input.h>
#include <string>
#include <sys/ioctl.h>
#include <unistd.h>
#endif

#ifndef APP_KEY_INPUT_DEVICE
#define APP_KEY_INPUT_DEVICE ""
#endif

namespace platform {
namespace {

std::array<lv_obj_t*, kSoftKeySlotCount> soft_key_slots{};
uint32_t pressed_key = 0;   // the key being followed; 0 while none is held
uint32_t press_started_at = 0;
bool long_press_sent = false;
lv_timer_t* long_press_timer = nullptr;
KeyReleaseListener key_release_listener = nullptr;
void* key_release_listener_user_data = nullptr;
GlobalKeyListener global_key_listener = nullptr;
void* global_key_listener_user_data = nullptr;

constexpr uint32_t kLongPressMs = 700;

#if !USE_DESKTOP
struct EvdevKeypad {
    int fd{-1};
    lv_indev_state_t state{LV_INDEV_STATE_RELEASED};
    uint32_t key{0};
    bool router_event_pending{false};
    uint32_t router_key{0};
    bool router_pressed{false};
};
#endif

void emit_global_key(uint32_t key, bool long_pressed) {
    if (global_key_listener) {
        global_key_listener(key, describe_key(key), long_pressed, global_key_listener_user_data);
    }
}

void long_press_timer_cb(lv_timer_t*) {
    if (!pressed_key || long_press_sent || lv_tick_elaps(press_started_at) < kLongPressMs) {
        return;
    }

    long_press_sent = true;
    emit_global_key(pressed_key, true);
}

void ensure_long_press_timer() {
    if (!long_press_timer) {
        long_press_timer = lv_timer_create(long_press_timer_cb, 40, nullptr);
    }
    if (long_press_timer) {
        lv_timer_resume(long_press_timer);
        lv_timer_reset(long_press_timer);
    }
}

// Only one key is followed at a time: a new key going down replaces the one
// held before, whose release is then not reported. The release of the
// followed key is matched by its code, so letting go of an earlier key while
// a later one is still held neither ends the later one nor loses its release.
void route_key_state(uint32_t key, bool pressed) {
    if (key == 0) {
        return;
    }

    if (pressed) {
        if (pressed_key == key) {
            return;
        }
        pressed_key = key;
        press_started_at = lv_tick_get();
        long_press_sent = false;
        emit_global_key(key, false);
        ensure_long_press_timer();
    }
    else if (pressed_key == key) {
        if (key_release_listener) {
            key_release_listener(key, describe_key(key), key_release_listener_user_data);
        }
        pressed_key = 0;
        if (long_press_timer) {
            lv_timer_pause(long_press_timer);
        }
    }
}

void key_event_cb(lv_event_t* event) {
    LV_UNUSED(event);

    auto* indev = lv_indev_active();
    if (!indev) {
        return;
    }

#if !USE_DESKTOP
    auto* keypad = static_cast<EvdevKeypad*>(lv_indev_get_driver_data(indev));
    if (keypad) {
        // LVGL emits LV_EVENT_KEY on every poll, even when evdev had no new event.
        if (!keypad->router_event_pending) {
            return;
        }

        keypad->router_event_pending = false;
        const auto key = keypad->router_key;
        const bool pressed = keypad->router_pressed;

        route_key_state(key, pressed);
        return;
    }
#endif

    const auto key = lv_indev_get_key(indev);
    const bool pressed = lv_indev_get_state(indev) == LV_INDEV_STATE_PRESSED;

    route_key_state(key, pressed);
}

#if !USE_DESKTOP
// Letters come through in lowercase: shift is not handled yet. The keyboard
// driver resolves fn and sym itself: fn+F/X/Z/C arrive as the arrow keys,
// fn+J as SysRq and fn+H as Help, and each sym symbol as a code of its own,
// which M5's console keymap (tca8418_keypad_m5stack_keymap.map) names. Those
// codes keep the kernel's names, which don't match the symbols: sym+S, the
// colon, is code 80 (KEY_KP2), and sym+B, the dot, is code 91
// (KEY_HIRAGANA). KEY_DOT is what sym+8 (the asterisk) sends, so it is not
// a dot here. TAB arrives as LV_KEY_NEXT, the same as in the simulator.
// Keys not listed here are ignored.
uint32_t map_evdev_key(uint16_t code) {
    switch (code) {
        case KEY_ESC:
            return LV_KEY_ESC;
        case KEY_TAB:
            return LV_KEY_NEXT;
        case KEY_KP2: // sym+S
            return ':';
        case KEY_HIRAGANA: // sym+B
            return '.';
        case KEY_UP:
            return LV_KEY_UP;
        case KEY_DOWN:
            return LV_KEY_DOWN;
        case KEY_LEFT:
            return LV_KEY_LEFT;
        case KEY_RIGHT:
            return LV_KEY_RIGHT;
        case KEY_ENTER:
        case KEY_KPENTER:
            return LV_KEY_ENTER;
        case KEY_BACKSPACE:
            return LV_KEY_BACKSPACE;
        case KEY_SPACE:
            return ' ';
        case KEY_SYSRQ:
            return kKeyPrintScreen;
        case KEY_HELP:
            return kKeyHelp;
        default:
            break;
    }

    // The evdev codes of letters follow the keyboard rows (Q-P, A-L, Z-M),
    // not the alphabet, so they are looked up one by one.
    struct CodeChar {
        uint16_t code;
        char character;
    };
    static constexpr CodeChar kCharacterKeys[] = {
        {KEY_A, 'a'}, {KEY_B, 'b'}, {KEY_C, 'c'}, {KEY_D, 'd'}, {KEY_E, 'e'}, {KEY_F, 'f'},
        {KEY_G, 'g'}, {KEY_H, 'h'}, {KEY_I, 'i'}, {KEY_J, 'j'}, {KEY_K, 'k'}, {KEY_L, 'l'},
        {KEY_M, 'm'}, {KEY_N, 'n'}, {KEY_O, 'o'}, {KEY_P, 'p'}, {KEY_Q, 'q'}, {KEY_R, 'r'},
        {KEY_S, 's'}, {KEY_T, 't'}, {KEY_U, 'u'}, {KEY_V, 'v'}, {KEY_W, 'w'}, {KEY_X, 'x'},
        {KEY_Y, 'y'}, {KEY_Z, 'z'}, {KEY_0, '0'}, {KEY_1, '1'}, {KEY_2, '2'}, {KEY_3, '3'},
        {KEY_4, '4'}, {KEY_5, '5'}, {KEY_6, '6'}, {KEY_7, '7'}, {KEY_8, '8'}, {KEY_9, '9'},
    };
    for (const auto& entry : kCharacterKeys) {
        if (entry.code == code) {
            return static_cast<uint32_t>(entry.character);
        }
    }
    return 0;
}

bool has_nav_keys(int fd) {
    unsigned long key_bits[(KEY_MAX / (sizeof(unsigned long) * 8)) + 1] = {};
    if (ioctl(fd, EVIOCGBIT(EV_KEY, sizeof(key_bits)), key_bits) < 0) {
        return false;
    }

    auto has_key = [&](int code) {
        const auto bits_per_word = static_cast<int>(sizeof(unsigned long) * 8);
        return (key_bits[code / bits_per_word] & (1UL << (code % bits_per_word))) != 0;
    };

    return has_key(KEY_ESC) || has_key(KEY_UP) || has_key(KEY_DOWN) || has_key(KEY_LEFT) ||
           has_key(KEY_RIGHT) || has_key(KEY_ENTER) || has_key(KEY_Z) || has_key(KEY_C) ||
           has_key(KEY_4) || has_key(KEY_5) || has_key(KEY_6) || has_key(KEY_7) ||
           has_key(KEY_8) || has_key(KEY_SYSRQ) || has_key(KEY_HELP);
}

void evdev_read_cb(lv_indev_t* indev, lv_indev_data_t* data) {
    auto* keypad = static_cast<EvdevKeypad*>(lv_indev_get_driver_data(indev));
    data->continue_reading = false;
    if (!keypad) {
        data->state = LV_INDEV_STATE_RELEASED;
        return;
    }

    input_event input{};
    while (read(keypad->fd, &input, sizeof(input)) == sizeof(input)) {
        if (input.type != EV_KEY) {
            continue;
        }

        // Value 2 is the keyboard's own autorepeat; a held key acts once.
        const auto key = map_evdev_key(input.code);
        if (!key || input.value == 2) {
            continue;
        }

        keypad->key = key;
        keypad->state = input.value ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
        keypad->router_event_pending = true;
        keypad->router_key = key;
        keypad->router_pressed = keypad->state == LV_INDEV_STATE_PRESSED;
        data->continue_reading = true;
        break;
    }

    data->key = keypad->key;
    data->state = keypad->state;
}

void evdev_delete_cb(lv_event_t* event) {
    auto* indev = static_cast<lv_indev_t*>(lv_event_get_target(event));
    auto* keypad = static_cast<EvdevKeypad*>(lv_indev_get_driver_data(indev));
    if (!keypad) {
        return;
    }

    if (keypad->fd >= 0) {
        close(keypad->fd);
    }
    delete keypad;
}

lv_indev_t* create_keypad_from_fd(int fd) {
    auto* keypad = new EvdevKeypad;
    keypad->fd = fd;

    auto* indev = lv_indev_create();
    if (!indev) {
        delete keypad;
        close(fd);
        return nullptr;
    }

    lv_indev_set_type(indev, LV_INDEV_TYPE_KEYPAD);
    lv_indev_set_read_cb(indev, evdev_read_cb);
    lv_indev_set_driver_data(indev, keypad);
    lv_indev_add_event_cb(indev, evdev_delete_cb, LV_EVENT_DELETE, nullptr);
    attach_key_router(indev);
    return indev;
}

lv_indev_t* try_create_keypad(const char* path) {
    if (!path || path[0] == '\0') {
        return nullptr;
    }

    const int fd = open(path, O_RDONLY | O_NONBLOCK | O_CLOEXEC);
    if (fd < 0) {
        LV_LOG_WARN("failed to open input device %s: %s", path, strerror(errno));
        return nullptr;
    }

    if (!has_nav_keys(fd)) {
        close(fd);
        return nullptr;
    }

    LV_LOG_INFO("using evdev key input %s", path);
    return create_keypad_from_fd(fd);
}

void discover_keypads(lv_display_t* display) {
    const char* configured_device = APP_KEY_INPUT_DEVICE;
    if (configured_device[0] != '\0') {
        auto* indev = try_create_keypad(configured_device);
        if (indev) {
            lv_indev_set_display(indev, display);
        }
        return;
    }

    auto* dir = opendir("/dev/input");
    if (!dir) {
        LV_LOG_WARN("failed to open /dev/input: %s", strerror(errno));
        return;
    }

    while (auto* entry = readdir(dir)) {
        if (std::strncmp(entry->d_name, "event", 5) != 0) {
            continue;
        }

        std::string path = "/dev/input/";
        path += entry->d_name;
        auto* indev = try_create_keypad(path.c_str());
        if (indev) {
            lv_indev_set_display(indev, display);
        }
    }

    closedir(dir);
}
#endif

} // namespace

void init_key_input(lv_display_t* display) {
#if !USE_DESKTOP
    discover_keypads(display);
#else
    LV_UNUSED(display);
#endif
}

void attach_key_router(lv_indev_t* indev) {
    if (!indev || lv_indev_get_type(indev) != LV_INDEV_TYPE_KEYPAD) {
        return;
    }

    lv_indev_add_event_cb(indev, key_event_cb, LV_EVENT_KEY, nullptr);
}

void register_soft_key(std::size_t index, lv_obj_t* slot) {
    if (index >= soft_key_slots.size()) {
        return;
    }

    soft_key_slots[index] = slot;
}

void unregister_soft_key(std::size_t index, lv_obj_t* slot) {
    if (index >= soft_key_slots.size()) {
        return;
    }

    if (!slot || soft_key_slots[index] == slot) {
        soft_key_slots[index] = nullptr;
    }
}

bool press_soft_key(std::size_t index) {
    if (index >= soft_key_slots.size()) {
        return false;
    }

    auto* slot = soft_key_slots[index];
    if (!slot || !lv_obj_is_valid(slot) || !lv_obj_has_flag(slot, LV_OBJ_FLAG_CLICKABLE)) {
        return false;
    }

    lv_obj_send_event(slot, LV_EVENT_CLICKED, nullptr);
    return true;
}

uint32_t arrow_for_letter(uint32_t key) {
    switch (key) {
        case 'f':
        case 'F':
            return LV_KEY_UP;
        case 'x':
        case 'X':
            return LV_KEY_DOWN;
        case 'z':
        case 'Z':
            return LV_KEY_LEFT;
        case 'c':
        case 'C':
            return LV_KEY_RIGHT;
        default:
            return key;
    }
}

void inject_key(uint32_t key, bool pressed) {
    route_key_state(key, pressed);
}

void set_key_release_listener(KeyReleaseListener listener, void* user_data) {
    key_release_listener = listener;
    key_release_listener_user_data = user_data;
}

void clear_key_release_listener(KeyReleaseListener listener, void* user_data) {
    if (key_release_listener == listener && key_release_listener_user_data == user_data) {
        key_release_listener = nullptr;
        key_release_listener_user_data = nullptr;
    }
}

void set_global_key_listener(GlobalKeyListener listener, void* user_data) {
    global_key_listener = listener;
    global_key_listener_user_data = user_data;
}

void clear_global_key_listener(GlobalKeyListener listener, void* user_data) {
    if (global_key_listener == listener && global_key_listener_user_data == user_data) {
        global_key_listener = nullptr;
        global_key_listener_user_data = nullptr;
    }
}

const char* describe_key(uint32_t key) {
    switch (key) {
        case LV_KEY_ESC:
            return "Esc";
        case LV_KEY_UP:
            return "Up";
        case LV_KEY_DOWN:
            return "Down";
        case LV_KEY_ENTER:
            return "Enter";
        case LV_KEY_BACKSPACE:
            return "Backspace";
        case LV_KEY_LEFT:
            return "Left";
        case LV_KEY_RIGHT:
            return "Right";
        case LV_KEY_NEXT:
            return "Tab";
        case kKeyPrintScreen:
            return "PrintScreen";
        case kKeyHelp:
            return "Help";
        default:
            break;
    }

    static char buffer[24];
    if (key >= 32 && key <= 126) {
        std::snprintf(buffer, sizeof(buffer), "%c", static_cast<char>(key));
    }
    else {
        std::snprintf(buffer, sizeof(buffer), "Key_%u", key);
    }
    return buffer;
}

} // namespace platform
