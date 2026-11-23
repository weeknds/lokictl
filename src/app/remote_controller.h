/*
 * SPDX-FileCopyrightText: 2026 LokiCTL contributors
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "lvgl.h"

#include <cstdint>
#include <memory>

namespace view {
class ThemeEngine;
}

namespace view::widgets {
class RemoteMark;
class RemotePanel;
}

namespace platform::remote {
class HandOffSource;
class KeyQueue;
class Server;
}

namespace app {

// The screen's side of Remote view. It follows the module's switch
// requests (platform::remote::request_switch(), from the Settings row),
// starts and stops the platform::remote::Server, shows the panel with the
// address and the code (view::widgets::RemotePanel), shows the REMOTE mark
// on every page while a browser is paired (view::widgets::RemoteMark) and
// keeps the module's state subject up to date (off, waiting, viewed).
// Everything here runs on the UI thread; the server does all network work
// on its own thread.
//
// Remote view is never saved: every start is off. It switches itself off
// when the code is not used in 2 minutes, after 5 wrong codes, after 10
// minutes without a request from the paired browser, when the address
// changes, and when the app quits; a new code always needs a key press on
// the Cardputer. A start that fails shows its reason on the panel for 4 s.
//
// While a browser is paired, the keys it sends (POST /key) wait in a queue
// that this controller owns; its own key timer presses one each tick
// through platform::inject_key(), press and release together, so they take
// exactly the path of a real key and no key is ever held. The queue is
// emptied whenever a server starts or stops.
//
// On the device the server reads the framebuffer itself, so the UI thread
// does nothing for the pictures. In the desktop simulator the server asks
// for a picture and this controller hands over an LVGL snapshot of the
// active screen with the top layer (help, messages, the panel and the mark)
// blended over it, from its timer. Desktop builds only: LOKICTL_REMOTE=on
// switches Remote view on at start and prints the code to standard error.
//
// Log lines (states and counts only, never the code, the session, an
// address or a key): "remote view: on, port 8780", "remote view: paired", "remote
// view: wrong code (<n> of 5)" (the latest count each time the timer looks),
// "remote view: off (<why>)" and "remote view: could not start (<why>)";
// desktop builds also log "remote view: switched on by LOKICTL_REMOTE; the
// code goes to standard error".
//
// Create it after the ThemeEngine and destroy it before.
class RemoteController {
public:
    explicit RemoteController(view::ThemeEngine& theme_engine);
    // Calls stop().
    ~RemoteController();

    RemoteController(const RemoteController&) = delete;
    RemoteController& operator=(const RemoteController&) = delete;

    // Switches Remote view off for good: stops following the requests,
    // closes the port and waits for the server's thread (well under a
    // second). Safe to call more than once.
    void stop();

private:
    static void request_cb(lv_observer_t* observer, lv_subject_t* subject);
    static void apply_requests_async(void* user_data);
    static void timer_cb(lv_timer_t* timer);
    static void key_timer_cb(lv_timer_t* timer);

    void apply_requests();
    void switch_on();
    void switch_off();
    void poll();
    void hand_off_frame();
    void resume_timer();
    // Runs the key timer only while a browser is paired; stopping it also
    // forgets the waiting keys.
    void run_key_timer(bool run);
    void press_waiting_key();

    std::unique_ptr<view::widgets::RemotePanel> panel_;
    std::unique_ptr<view::widgets::RemoteMark> mark_;
    // Declared before the servers: their threads may use it until they end.
    std::unique_ptr<platform::remote::KeyQueue> keys_;
    std::unique_ptr<platform::remote::Server> server_;   // the one that runs
    std::unique_ptr<platform::remote::Server> retiring_; // told to stop, not yet ended
    platform::remote::HandOffSource* hand_off_{nullptr}; // owned by server_ (desktop)
    lv_observer_t* request_observer_{nullptr};
    lv_timer_t* timer_{nullptr};
    lv_timer_t* key_timer_{nullptr};
    bool key_timer_running_{false};
    int32_t requests_seen_{0};
    int32_t requests_pending_{0}; // switch requests not yet applied
    bool apply_scheduled_{false};
    int wrong_codes_seen_{0};
    bool listening_seen_{false};
    bool paired_seen_{false};
    bool print_code_{false};
    uint32_t message_started_{0};
    bool message_shown_{false};
    bool stopped_{false};
};

} // namespace app
