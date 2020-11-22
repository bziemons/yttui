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
extern Align operator|(const Align &a, const Align &b);

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
extern Button operator|(const Button &a, const Button &b);
extern Button operator&(const Button &a, const Button &b);

extern void tp_init();
extern void tp_shutdown();
extern void tp_flush();
extern void tp_pause();
extern void tp_unpause();
extern std::optional<Event> tp_wait_for_event();

struct action
{
    int type;
    std::string string;
    int modifier;

    std::function<void(void)> func;
    std::string help;
};
extern bool tui_handle_action(const Event &event, const std::vector<action> &actions);

extern size_t string_width(const std::string &str);
extern std::pair<size_t, size_t> string_size(const std::string &str);
extern void write_multiline_string(const int x, const int y, const std::string &str, termpaint_attr *attr);

extern Button message_box(const std::string &caption, const std::string &text, const Button buttons=Button::Ok, const Button default_button=Button::Ok, const Align align=Align::Center);
extern int get_selection(const std::string &caption, const std::vector<std::string> &choices, size_t selected=0, const Align align=Align::Center);
extern std::string get_string(const std::string &caption, const std::string &text=std::string(), const Align align=Align::Center);

struct progress_info;
extern progress_info* begin_progress(const std::string &caption, const int width, const Align align=Align::Center);
extern void update_progress(progress_info *info, const int val, const int maxval);
extern void end_progress(progress_info *info);

extern void tui_abort(std::string message);
extern void tui_abort(const char *fmt, ...);
