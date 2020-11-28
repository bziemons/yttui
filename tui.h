// SPDX-License-Identifier: MIT
#pragma once

#include <termpaintx.h>
#define EV_TIMEOUT 0xffff
#define EV_IGNORE 0xfffe

#include <functional>
#include <optional>
#include <string>
#include <vector>

struct Event {
    int type;
    int modifier;
    std::string string;
};

enum class Align {
    HCenter = 0x01,
    Left    = 0x02,
    Right   = 0x04,

    VCenter = 0x10,
    Top     = 0x20,
    Bottom  = 0x40,

    Center = HCenter | VCenter,
};
Align operator|(const Align &a, const Align &b);

extern termpaint_surface *surface;

enum AttributeSetType {
    ASNormal,
    ASWatched,
    ASUnwatched,
    ASetTypeCount,
};

struct AttributeSet {
    termpaint_attr *normal;
    termpaint_attr *highlight;
};
extern AttributeSet attributes[ASetTypeCount];

enum class Button {
    Ok = (1<<0),
    Cancel = (1<<1),
    Yes = (1<<2),
    No = (1<<3),
};
Button operator|(const Button &a, const Button &b);
Button operator&(const Button &a, const Button &b);

void tp_init();
void tp_shutdown();
void tp_flush(const bool force=false);
void tp_pause();
void tp_unpause();
std::optional<Event> tp_wait_for_event();

struct action
{
    int type;
    std::string string;
    int modifier;

    std::function<void(void)> func;
    std::string help;
};
bool tui_handle_action(const Event &event, const std::vector<action> &actions);

size_t string_width(const std::string &str);
std::pair<size_t, size_t> string_size(const std::string &str);
void write_multiline_string(const int x, const int y, const std::string &str, termpaint_attr *attr);
std::string text_wrap(const std::string &text, const size_t desired_width);

Button message_box(const std::string &caption, const std::string &text, const Button buttons=Button::Ok, const Button default_button=Button::Ok, const Align align=Align::Center);
int get_selection(const std::string &caption, const std::vector<std::string> &choices, size_t selected=0, const Align align=Align::Center);
std::string get_string(const std::string &caption, const std::string &text=std::string(), const Align align=Align::Center);

struct progress_info;
progress_info* begin_progress(const std::string &caption, const int width, const Align align=Align::Center);
void update_progress(progress_info *info, const int val, const int maxval);
void end_progress(progress_info *info);

void tui_abort(std::string message);
void tui_abort(const char *fmt, ...);
