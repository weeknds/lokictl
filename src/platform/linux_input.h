#pragma once

#include "lvgl.h"

#include <cstddef>
#include <cstdint>

namespace platform {

// Reads the keyboard (evdev on the device, the simulator's keypad on the
// desktop) and times long presses. It does not decide what a key does: the
// app's key listener does (app::KeyController).

// Keys 4-8, left to right: slot 0 is key 4.
constexpr std::size_t kSoftKeySlotCount = 5;
constexpr uint32_t kSpecialKeyBase = 0x200000U;
constexpr uint32_t kKeyPrintScreen = kSpecialKeyBase | 18U;
constexpr uint32_t kKeyHelp = kSpecialKeyBase | 19U;

// Called when a key goes down (long_pressed false) and once more when the
// same key is still held 700 ms later (long_pressed true). The second call
// comes for every key, so a listener that acts on it must check which key
// it is.
using GlobalKeyListener = void (*)(uint32_t key,
                                   const char* key_name,
                                   bool long_pressed,
                                   void* user_data);
// Called when the held key is let go.
using KeyReleaseListener = void (*)(uint32_t key, const char* key_name, void* user_data);

void init_key_input(lv_display_t* display);
void attach_key_router(lv_indev_t* indev);

// The clickable slot of soft key `index` (0-4 for keys 4-8) on the page
// that is shown. unregister_soft_key() only clears the slot while it still
// holds `slot` (or when `slot` is nullptr), because a rebuilt page registers
// its keys before the old page goes.
void register_soft_key(std::size_t index, lv_obj_t* slot);
void unregister_soft_key(std::size_t index, lv_obj_t* slot);
// Sends a click to soft key `index` (0-4 for keys 4-8). Returns false and
// does nothing when no slot is registered there or the slot is not
// clickable (an unused or switched-off key).
bool press_soft_key(std::size_t index);

// The arrow a letter stands for when the user is not typing: f/F up, x/X
// down, z/Z left, c/C right (the same keys give the arrows with fn). Any
// other key comes back unchanged.
uint32_t arrow_for_letter(uint32_t key);

// Feeds one key change through exactly the path of a real key: the key
// listener, the long-press timer and the release listener. Used by
// snapshot --keys; call it on the UI thread.
void inject_key(uint32_t key, bool pressed);

void set_key_release_listener(KeyReleaseListener listener, void* user_data);
void clear_key_release_listener(KeyReleaseListener listener, void* user_data);
void set_global_key_listener(GlobalKeyListener listener, void* user_data);
void clear_global_key_listener(GlobalKeyListener listener, void* user_data);
const char* describe_key(uint32_t key);

} // namespace platform
