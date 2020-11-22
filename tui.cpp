#include "tui.h"

#include <algorithm>
#include <deque>
#include <string>
#include <vector>

termpaint_integration *integration;
termpaint_terminal *terminal;
termpaint_surface *surface;

AttributeSet attributes[ASetTypeCount];

std::deque<Event> eventqueue;

static void convert_tp_event(void *, termpaint_event *tp_event) {
    Event e;
    if (tp_event->type == TERMPAINT_EV_CHAR) {
        e.type = tp_event->type;
        e.modifier = tp_event->c.modifier;
        e.string = std::string(tp_event->c.string, tp_event->c.length);
        eventqueue.push_back(e);
    } else if (tp_event->type == TERMPAINT_EV_KEY) {
        e.type = tp_event->type;
        e.modifier = tp_event->key.modifier;
        e.string = std::string(tp_event->key.atom, tp_event->key.length);
        eventqueue.push_back(e);
    }
}

std::optional<Event> wait_for_event(termpaint_integration *integration, int timeout) {
    while (eventqueue.empty()) {
        bool ok = false;
        if(timeout > 0)
            ok = termpaintx_full_integration_do_iteration_with_timeout(integration, &timeout);
        else
            ok = termpaintx_full_integration_do_iteration(integration);
        if (!ok) {
            return {}; // or some other error handling
        } else if(timeout == 0) {
            Event e;
            e.type = EV_TIMEOUT;
            eventqueue.push_back(e);
        }
    }
    Event e = eventqueue.front();
    eventqueue.pop_front();
    return e;
}

void tp_init()
{
    auto new_attr_set = [](AttributeSet &set, int color, int style = 0) {
        set.normal = termpaint_attr_new(color, TERMPAINT_DEFAULT_COLOR);
        termpaint_attr_set_style(set.normal, style);
        set.highlight = termpaint_attr_clone(set.normal);
        termpaint_attr_set_style(set.highlight, TERMPAINT_STYLE_INVERSE | style);
    };

    new_attr_set(attributes[ASNormal], TERMPAINT_DEFAULT_COLOR);
    new_attr_set(attributes[ASWatched], TERMPAINT_DEFAULT_COLOR);
    new_attr_set(attributes[ASUnwatched], TERMPAINT_DEFAULT_COLOR, TERMPAINT_STYLE_BOLD);

    integration = termpaintx_full_integration_setup_terminal_fullscreen( "+kbdsig +kbdsigint +kbdsigtstp", convert_tp_event, nullptr, &terminal);
    surface = termpaint_terminal_get_surface(terminal);
    termpaint_terminal_set_cursor_visible(terminal, false);
}

void tp_shutdown()
{
    auto free_attr_set = [](AttributeSet &set) {
        termpaint_attr_free(set.normal);
        termpaint_attr_free(set.highlight);
    };

    free_attr_set(attributes[ASNormal]);
    free_attr_set(attributes[ASWatched]);
    free_attr_set(attributes[ASUnwatched]);

    termpaint_terminal_free_with_restore(terminal);
}

void tp_flush()
{
    termpaint_terminal_flush(terminal, false);
}

std::optional<Event> tp_wait_for_event()
{
    return wait_for_event(integration, 0);
}

static std::string repeated(const int n, const std::string &what)
{
    std::string out;
    for(int i=0; i<n; i++)
        out.append(what);
    return out;
}

size_t string_width(const std::string &str)
{
    termpaint_text_measurement *m = termpaint_text_measurement_new(surface);
    termpaint_text_measurement_feed_utf8(m, str.c_str(), str.length(), true);
    const int width = termpaint_text_measurement_last_width(m);
    termpaint_text_measurement_free(m);
    return width;
}

static void resolve_align(const Align align, const int width, const int height, const int xmin, const int xmax, const int ymin, const int ymax, int &x, int &y)
{
    int available_width = xmax-xmin;
    x = xmin;
    if(((int)align & 0x0f) == (int)Align::HCenter)
        x = (available_width - width) / 2;
    else if(((int)align & 0x0f) == (int)Align::Right)
        x = xmax - width;

    int available_rows = ymax - ymin;
    y = ymin;
    if(((int)align & 0xf0) == (int)Align::VCenter)
        y = (available_rows - height) / 2;
    else if(((int)align & 0xf0) == (int)Align::Bottom)
        y = ymax - height;
}

static void draw_box_with_caption(int x, int y, int w, int h, const std::string &caption=std::string())
{
    termpaint_surface_clear_rect(surface, x, y, w, h, TERMPAINT_DEFAULT_COLOR, TERMPAINT_DEFAULT_COLOR);
    const int fill = w - 2;
    const int endy = y+h;

    for(int yy = y; yy<endy; yy++)
    {
        std::string s;
        if(yy == y) {
            s = "┌" + repeated(fill, "─") + "┐";
        } else if(yy + 1 == endy) {
            s = "└" + repeated(fill, "─") + "┘";
        } else {
            s = "│" + repeated(fill, " ") + "│";
        }
        termpaint_surface_write_with_attr(surface, x, yy, s.c_str(), attributes[ASNormal].normal);
    }
    if(!caption.empty())
        termpaint_surface_write_with_attr(surface, x+2, y, caption.c_str(), attributes[ASNormal].normal);
}

int get_selection(const std::string &caption, const std::vector<std::string> &entries, size_t selected, const Align align)
{
    selected = std::max(size_t(0), std::min(entries.size(), selected));

    size_t cols_needed = string_width(caption);
    for(const auto &e: entries)
        cols_needed = std::max(cols_needed, string_width(e));

    cols_needed += 4; // Border and left/right padding

    const int rows_needed = entries.size()+2; // Number of entries and top/bottom border

    const int cols = termpaint_surface_width(surface);
    const int rows = termpaint_surface_height(surface);

    int x, y;
    resolve_align(align, cols_needed, rows_needed, 0, cols, 0, rows, x, y);

    while (true) {
        draw_box_with_caption(x, y, cols_needed, rows_needed, caption);
        int yy = y+1;

        size_t cur_entry = 0;
        for(const auto &e: entries) {
            termpaint_attr *attr = cur_entry == selected ? attributes[ASNormal].highlight : attributes[ASNormal].normal;
            termpaint_surface_write_with_attr(surface, x+2, yy++, e.c_str(), attr);
            cur_entry++;
        }
        termpaint_terminal_flush(terminal, false);

        auto event = wait_for_event(integration, 0);
        if(!event)
            abort();

        if(event->type == TERMPAINT_EV_CHAR) {
            if(event->string.length() == 1) {
                char c = event->string[0];
                if(c>'0' && c<='9') {
                    size_t idx = c - '0';
                    if(idx > entries.size())
                        continue;
                    return idx - 1;
                }
            }
        } else if (event->type == TERMPAINT_EV_KEY) {
            if (event->string == "Escape") {
                return -1;
            } else if(event->string == "ArrowUp") {
                if(selected > 0)
                    selected--;
            } else if(event->string == "ArrowDown") {
                if(selected < entries.size() - 1)
                    selected++;
            } else if(event->string == "Enter" || event->string == "NumpadEnter") {
                return static_cast<int>(selected);
            }
        }
    }

    return -1;
}

Align operator|(const Align &a, const Align &b)
{
    return Align((int)a | (int)b);
}

Button operator|(const Button &a, const Button &b)
{
    return Button((int)a | (int)b);
}

Button operator&(const Button &a, const Button &b)
{
    return Button((int)a & (int)b);
}

std::string get_string(const std::string &caption, const std::string &text, const Align align)
{
    const int cols = termpaint_surface_width(surface);
    const int rows = termpaint_surface_height(surface);

    const size_t rows_needed = 3 + !text.empty();
    const size_t cols_needed = cols/2;

    int x, y;
    resolve_align(align, cols_needed, rows_needed, 0, cols, 0, rows, x, y);

    const int input_row = y + 1 + !text.empty();

    std::string input;
    size_t input_pos = 0;
    termpaint_terminal_set_cursor_visible(terminal, true);
    termpaint_terminal_set_cursor_style(terminal, TERMPAINT_CURSOR_STYLE_BAR, true);

    while(true) {
        draw_box_with_caption(x, y, cols_needed, rows_needed, caption);
        if(!text.empty())
            termpaint_surface_write_with_attr(surface, x + 1, y + 1, text.c_str(), attributes[ASNormal].normal);
        termpaint_surface_write_with_attr(surface, x + 1, input_row, input.c_str(), attributes[ASNormal].normal);
        termpaint_terminal_set_cursor_position(terminal, x + 1 + input_pos, input_row);

        termpaint_terminal_flush(terminal, false);

        auto event = wait_for_event(integration, 0);
        if(!event)
            abort();

        if(event->type == TERMPAINT_EV_CHAR) {
            if(event->string == "a" && event->modifier & TERMPAINT_MOD_CTRL) {
                input_pos = 0;
            } else if (event->string == "e" && event->modifier & TERMPAINT_MOD_CTRL) {
                input_pos = input.size();
            } else {
                if(input_pos + 1 == cols_needed - 1)
                    continue;
                input.insert(input_pos, event->string);
                input_pos++;
            }
        } else if (event->type == TERMPAINT_EV_KEY) {
            if (event->string == "Escape") {
                input.clear();
                break;
            } else if(event->string == "Backspace") {
                if(!input.empty()) {
                    input.erase(input_pos - 1, 1);
                    input_pos--;
                }
            } else if(event->string == "Delete") {
                if(input_pos < input.size()) {
                    input.erase(input_pos, 1);
                }
            } else if(event->string == "ArrowLeft") {
                if(input_pos > 0)
                    input_pos--;
            } else if(event->string == "ArrowRight") {
                if(input_pos < input.size())
                    input_pos++;
            } else if(event->string == "Enter" || event->string == "NumpadEnter") {
                break;
            }
        }
    }

    termpaint_terminal_set_cursor_visible(terminal, false);
    termpaint_terminal_set_cursor_style(terminal, TERMPAINT_CURSOR_STYLE_TERM_DEFAULT, false);
    return input;
}

std::vector<std::string> split(const std::string &str, const char delim, const unsigned int max_splits=0)
{
    std::vector<std::string> parts;

    std::string::const_iterator prev = str.cbegin();
    std::string::const_iterator cur = str.cbegin();
    unsigned int splits_done = 0;

    do {
        cur = std::find(prev, str.cend(), delim);
        if(max_splits!=0 && splits_done>=max_splits) {
            parts.emplace_back(prev, str.cend());
            break;
        }
        parts.emplace_back(prev, cur);
        splits_done++;
        if(cur!=str.cend())
            cur++;
        prev = cur;
    } while(cur!=str.cend());

    return parts;
}

struct button_info {
    Button button;
    std::string string;
    size_t width;
};

static std::vector<button_info> all_buttons = {
    {Button::Ok, "Ok", 2},
    {Button::Cancel, "Cancel", 6},
    {Button::Yes, "Yes", 3},
    {Button::No, "No", 2},
};
static const char *button_gfx_left[]={"[", " "};
const char *button_gfx_right[]={"]", " "};

Button message_box(const std::string &caption, const std::string &text, const Button buttons, const Button default_button, const Align align)
{
    std::vector<button_info> active_buttons;

    size_t buttons_width = 0;
    for(const button_info &info: all_buttons) {
        if((buttons & info.button) == info.button) {
            active_buttons.push_back(info);
            buttons_width += info.width + 2 + 1;
        }
    }
    if(buttons_width)
        buttons_width -= 1;
    size_t selected_button = std::distance(active_buttons.begin(),
                                           std::find_if(active_buttons.begin(), active_buttons.end(),
                                                        [default_button](const button_info &info){ return info.button == default_button; }));

    size_t width = std::max(string_width(caption), buttons_width);

    const std::vector<std::string> lines = split(text, '\n');
    for(const std::string &line: lines) {
        width = std::max(width, string_width(line));
    }

    const size_t cols = termpaint_surface_width(surface);
    const size_t rows = termpaint_surface_height(surface);

    const size_t rows_needed = 4 + lines.size();
    const size_t cols_needed = width + 4;

    int x, y;
    resolve_align(align, cols_needed, rows_needed, 0, cols, 0, rows, x, y);

    while(true) {
        draw_box_with_caption(x, y, cols_needed, rows_needed, caption);

        for(size_t i=0; i<lines.size(); i++) {
            termpaint_surface_write_with_attr(surface, x + 2, y + 1 + i, lines[i].c_str(), attributes[ASNormal].normal);
        }


        int button_x = x + 2;
        for(size_t btn=0; btn<active_buttons.size(); btn++) {
            const bool button_selected = selected_button == btn;
            termpaint_attr *attr = button_selected ? attributes[ASNormal].highlight : attributes[ASNormal].normal;

            termpaint_surface_write_with_attr(surface, button_x, y + rows_needed - 2, button_gfx_left[button_selected], attr);
            termpaint_surface_write_with_attr(surface, button_x + 1, y + rows_needed - 2, active_buttons[btn].string.c_str(), attr);
            termpaint_surface_write_with_attr(surface, button_x + active_buttons[btn].width + 1, y + rows_needed - 2, button_gfx_right[button_selected], attr);

            button_x += active_buttons[btn].width + 2 + 1;
        }

        termpaint_terminal_flush(terminal, false);

        auto event = wait_for_event(integration, 0);
        if(!event)
            abort();

        if (event->type == TERMPAINT_EV_KEY) {
            if (event->string == "Escape" || event->string == "Enter" || event->string == "NumpadEnter") {
                break;
            } else if(event->string == "ArrowLeft" && selected_button > 0) {
                selected_button--;
            } else if(event->string == "ArrowRight" && selected_button < active_buttons.size() - 1) {
                selected_button++;
            }
        }
    }

    return active_buttons[selected_button].button;
}

void tp_pause()
{
    termpaint_terminal_pause(terminal);
}

void tp_unpause()
{
    termpaint_terminal_unpause(terminal);
}

struct progress_info
{
    Align align;
    size_t width, height;
    std::string caption;
    int value, maxvalue;
};

static void draw_progress(progress_info *info)
{
    const size_t cols = termpaint_surface_width(surface);
    const size_t rows = termpaint_surface_height(surface);

    int x, y;
    resolve_align(info->align, info->width, info->height, 0, cols, 0, rows, x, y);

    draw_box_with_caption(x, y, info->width, info->height, info->caption);

    size_t progress_w = info->width - 4;
    float progress = info->value / (float)info->maxvalue;
    int full_blocks = progress_w * progress;
    int partial_block = ((progress_w * progress) - full_blocks) * 8;

    std::string draw = repeated(full_blocks, "█");

    switch(partial_block) {
    case 1:
        draw.append("▏"); break;
    case 2:
        draw.append("▎"); break;
    case 3:
        draw.append("▍"); break;
    case 4:
        draw.append("▌"); break;
    case 5:
        draw.append("▋"); break;
    case 6:
        draw.append("▊"); break;
    case 7:
        draw.append("▉"); break;
    default:
        break;
    }

    termpaint_surface_write_with_attr(surface, x + 2, y + 1, draw.c_str(), attributes[ASNormal].normal);
    termpaint_terminal_flush(terminal, false);
}

progress_info* begin_progress(const std::string &caption, const int width, const Align align)
{
    progress_info *info = new progress_info;

    info->align = align;
    info->width = width + 4;
    info->height = 3;
    info->caption = caption;
    info->value = 0;
    info->maxvalue = 100;

    draw_progress(info);

    return info;
}

void update_progress(progress_info *info, int value, int maxvalue)
{
    info->value = value;
    info->maxvalue = maxvalue;
    draw_progress(info);
}

void end_progress(progress_info *info)
{
    delete info;
}

std::pair<size_t, size_t> string_size(const std::string &str)
{
    size_t width = 0;
    const std::vector<std::string> lines = split(str, '\n');
    for(const std::string &line: lines) {
        width = std::max(width, string_width(line));
    }
    return {width, lines.size()};
}

void write_multiline_string(const int x, const int y, const std::string &str, termpaint_attr *attr)
{
    const std::vector<std::string> lines = split(str, '\n');
    for(size_t i=0; i<lines.size(); i++) {
        termpaint_surface_write_with_attr(surface, x, y + i, lines[i].c_str(), attr);
    }
}

static std::string simple_wrap(const std::string &text, const size_t desired_width)
{
    std::string out;
    size_t current_line_width = 0;
    for(const std::string &word: split(text, ' ')) {
        size_t w = string_width(word);
        if(current_line_width + w < desired_width) {
            out.append(word).append(" ");
        } else {
            current_line_width = 0;
            out.append("\n").append(word).append(" ");
        }
        current_line_width += w + 1;
    }
    return out;
}

void tui_abort(std::string message)
{
    const size_t cols = termpaint_surface_width(surface);

    message_box("Error", simple_wrap(message, cols/2));
    exit(1);
}
