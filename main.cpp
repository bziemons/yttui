#define _X_OPEN_SOURCE

#include "tui.h"
#include "yt.h"

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <unordered_map>
#include <fstream>

#include <time.h>
#include <stdio.h>

#include <curl/curl.h>
#include <nlohmann/json.hpp>
#include <sqlite3.h>

static std::string user_home;

std::vector<Channel> channels;
std::unordered_map<std::string, std::vector<Video>> videos;

std::optional<size_t> selected_channel;
size_t current_video_count = 0;
size_t selected_video = 0;
size_t videos_per_page = 0;
size_t current_page_count = 0;
size_t title_offset = 0;
bool any_title_in_next_half = false;

#define SC(x) { const int res = (x); if(res != SQLITE_OK && res != SQLITE_ROW && res != SQLITE_DONE) { fprintf(stderr, "%s failed: (%d) %s\n", #x, res, sqlite3_errstr(res)); std::abort(); }}

static termpaint_attr* get_attr(const AttributeSetType type, const bool highlight=false)
{
    return highlight ? attributes[type].highlight : attributes[type].normal;
}

void draw_channel_list(const std::vector<Video> &videos)
{
    const size_t cols = termpaint_surface_width(surface);
    const size_t rows = termpaint_surface_height(surface);

    const size_t column_spacing = 2;

    const size_t status_column = 0;
    const size_t status_width = 1;

    const size_t date_column = status_column + status_width + column_spacing;
    const size_t date_width = std::string("xxxx-xx-xx xx:xx").size();
    const size_t first_name_column = date_column + date_width + column_spacing;
    const size_t last_name_column = cols;
    const size_t name_quater = (last_name_column - first_name_column) / 4;

    const size_t start_row = 2;
    const size_t available_rows = rows - 2;
    videos_per_page = available_rows;

    const int pages = videos.size() / available_rows;
    const int cur_page = selected_video / available_rows;

    const std::string channel_name = std::string("Channel: ") + channels[*selected_channel].name.c_str();
    termpaint_surface_write_with_attr(surface, 0, 0, channel_name.c_str(), get_attr(ASNormal));

    if(pages > 1) {
        std::string text = "(Page " + std::to_string(cur_page + 1) + "/" + std::to_string(pages + 1) + ")";
        const size_t w = string_width(text);
        termpaint_surface_write_with_attr(surface, cols - w, 0, text.c_str(), attributes[ASNormal].normal);
    }

    termpaint_surface_write_with_attr(surface, date_column, 1, "Date", get_attr(ASNormal));
    termpaint_surface_write_with_attr(surface, first_name_column, 1, "Title", get_attr(ASNormal));

    any_title_in_next_half = false;

    size_t cur_entry = 0;
    for(size_t i = cur_page*available_rows; i < videos.size(); i++) {
        const size_t row = start_row + cur_entry;
        const bool selected = i == selected_video;
        const Video &video = videos.at(i);
        termpaint_attr *attr = get_attr(video.flags & kWatched ? ASWatched : ASUnwatched, selected);

        termpaint_surface_clear_rect_with_attr(surface, 0, row, cols, 1, attr);

        std::vector<char> dt(date_width + 10, 0);

        struct tm tm;
        memset(&tm, 0, sizeof(tm));
        if(strptime(video.published.c_str(), "%FT%T%z", &tm) != nullptr) {
            strftime(dt.data(), date_width + 10, "%F %H:%M", &tm);
        }

        termpaint_surface_write_with_attr(surface, date_column, row, dt.data(), attr);

        bool in_this_quater = title_offset * name_quater < video.tui_title_width;
        any_title_in_next_half = any_title_in_next_half || ((title_offset + 2) * name_quater) < video.tui_title_width;
        if(in_this_quater)
            termpaint_surface_write_with_attr_clipped(surface, first_name_column, row, video.title.c_str() + (name_quater * title_offset), attr, first_name_column, last_name_column);
        else
            termpaint_surface_write_with_attr(surface, first_name_column, row, "←", attr);
        std::string status_symbol = "";
        if(video.flags & kWatched) {
            status_symbol = "*";
        }
        if(video.flags & kDownloaded) {
            status_symbol = "↓";
        }
        termpaint_surface_write_with_attr(surface, status_column, row, status_symbol.c_str(), attr);

        if(++cur_entry > available_rows)
            break;
    }

    if(!any_title_in_next_half && title_offset > 0)
        title_offset--;
}

void draw_no_channels_msg()
{
    const size_t cols = termpaint_surface_width(surface);
    const size_t rows = termpaint_surface_height(surface);

    const std::string text("No configured channels.\n"
                           "Press the following keys to add one:\n"
                           "a    Add by name\n"
                           "A    Add by ID\n"
                           "Or press h for help or ^q to quit.");
    {
        std::pair<size_t, size_t> size = string_size(text);
        write_multiline_string((cols - size.first) / 2, (rows - size.second) / 2, text, get_attr(ASNormal));
    }
}

static void pad_to_width(std::string &str, const size_t width)
{
    const size_t current = string_width(str);
    str.append(width-current, ' ');
}

struct action
{
    int type;
    std::string string;
    int modifier;

    std::function<void(void)> func;
    const std::string help;
};

struct helpitem {
    std::string key;
    std::string text;
};

void draw_help(const std::vector<helpitem> &items)
{
    const size_t rows = termpaint_surface_height(surface);

    const size_t rows_per_column = rows / 3 * 2;

    std::vector<std::vector<std::string>> column_texts;

    const size_t text_columns = 1 + items.size() / rows_per_column;

    size_t item = 0;
    for(size_t column=0; column<text_columns; column++) {
        const size_t items_this_column = std::min(items.size() - item, rows_per_column);
        std::vector<std::string> texts;
        size_t key_width = 0;
        size_t text_width = 0;
        for(size_t i=0; i<items_this_column; i++) {
            key_width = std::max(string_width(items[item + i].key), key_width);
            text_width = std::max(string_width(items[item + i].text), text_width);
        }
        for(size_t i=0; i<items_this_column; i++) {
            std::string s = items[item].key;
            pad_to_width(s, key_width);
            s.append(" ");
            s.append(items[item].text);
            pad_to_width(s, key_width + 1 + text_width);
            texts.push_back(s);

            item++;
        }
        column_texts.push_back(texts);
    }

    std::string to_show;
    for(size_t i=0; i<rows_per_column; i++) {
        for(size_t column=0; column<text_columns; column++) {
            if(i >= column_texts[column].size())
                continue;
            to_show.append(column_texts[column][i]);
            if(column+1 < text_columns)
                to_show.append(" ");
        }
        to_show.append("\n");
    }

    message_box("Help", to_show.c_str());
}

static std::unordered_map<std::string, std::string> key_symbols = {
    {"ArrowLeft", "←"},
    {"ArrowUp", "↑"},
    {"ArrowRight", "→"},
    {"ArrowDown", "↓"},
    {"Space", "[Space]"},
};

std::string format_key(const action &action) {
    std::string str;
    if(action.modifier & TERMPAINT_MOD_CTRL)
        str.append("C-");
    if(action.modifier & TERMPAINT_MOD_ALT)
        str.append("M-");
    if(action.type == TERMPAINT_EV_KEY) {
        auto it = key_symbols.find(action.string);
        if(it == key_symbols.end())
            str.append(action.string);
        else
            str.append(it->second);
    } else {
        str.append(action.string);
    }
    return str;
}

bool handle_action(const Event &event, const std::vector<action> &actions)
{
    if(event.type == EV_TIMEOUT)
        return false;

    const auto it = std::find_if(actions.cbegin(), actions.cend(), [&](const action &a) {
        return a.type == event.type
                && a.string == event.string
                && event.modifier == a.modifier;
    });
    if(it == actions.cend()) {
        if(event.type == TERMPAINT_EV_KEY && event.string == "F1") {
            std::vector<helpitem> items = {{"F1", "Display this help"}};
            for(const action &action: actions) {
                if(action.help.empty())
                    continue;
                items.push_back({format_key(action), action.help});
            }
            draw_help(items);
        }
        return false;
    }
    if(it->func)
        it->func();
    return true;
}

sqlite3 *db;

void load_videos_for_channel(const std::string &channelId, bool force=false)
{
    if(videos.find(channelId) != videos.end() && !force)
        return;
    std::vector<Video> &channelVideos = videos[channelId];
    channelVideos.clear();
    if(channels[*selected_channel].id == channelId)
        selected_video = 0;

    sqlite3_stmt *query;
    SC(sqlite3_prepare_v2(db, "SELECT * FROM videos WHERE channelId=?1 ORDER BY published DESC;", -1, &query, nullptr));

    SC(sqlite3_bind_text(query, 1, channelId.c_str(), -1, SQLITE_TRANSIENT));
    while(sqlite3_step(query) == SQLITE_ROW) {
        Video video(query);
        video.tui_title_width = string_width(video.title);
        channelVideos.push_back(video);
    }
    SC(sqlite3_finalize(query));
}

void fetch_videos_for_channel(Channel &ch, bool name_in_title=false)
{
    std::string title("Refreshing");
    if(name_in_title)
        title.append(" ").append(ch.name);
    title.append("…");
    progress_info *info = begin_progress(title, 30);
    ch.fetch_new_videos(db, info);
    end_progress(info);
    load_videos_for_channel(channels[*selected_channel].id, true);
    ch.load_info(db);
}

void select_channel_by_index(const int index) {
    selected_channel = index;
    const Channel &channel = channels.at(*selected_channel);
    selected_video = 0;
    current_video_count = channel.video_count;
    load_videos_for_channel(channel.id);
}

void select_channel_by_name(const std::string &channel_name) {
    auto it = std::find_if(channels.cbegin(), channels.cend(), [&](const Channel &channel){ return channel.name == channel_name; });
    if(it == channels.cend())
        return;
    select_channel_by_index(std::distance(channels.cbegin(), it));
}

void select_channel_by_id(const std::string &channel_id) {
    auto it = std::find_if(channels.cbegin(), channels.cend(), [&](const Channel &channel){ return channel.id == channel_id; });
    if(it == channels.cend())
        return;
    select_channel_by_index(std::distance(channels.cbegin(), it));
}

void add_channel_to_list(Channel &channel)
{
    channel.load_info(db);

    std::string selected_channel_id;
    if(selected_channel) {
        selected_channel_id = channels[*selected_channel].id;
    }

    channels.push_back(channel);
    std::sort(channels.begin(), channels.end(), [](const Channel &a, const Channel &b){ return a.name < b.name; });

    if(selected_channel) {
        const size_t new_index = std::distance(channels.cbegin(), std::find_if(channels.cbegin(), channels.cend(), [&](const Channel &ch){ return ch.id == selected_channel_id; }));
        selected_channel = new_index;
    }
}

void handle_add_channel_by_name()
{
    std::string channel_name = get_string("Add new Channel", "Enter channel name");
    if(channel_name.empty()) {
        return;
    } else if(std::find_if(channels.cbegin(), channels.cend(), [&](const Channel &channel){ return channel.name == channel_name; }) != channels.cend()) {
        message_box("Can't add channel", "A channel with this name is already in the list!");
        return;
    } else {
        Channel ch = Channel::add(db, "forUsername", channel_name);
        if(ch.is_valid()) {
            add_channel_to_list(ch);
            select_channel_by_id(ch.id);
            tp_flush();
            if(message_box("Update now?", "Fetch videos for this channel now?", Button::Yes | Button::No, Button::Yes) == Button::Yes) {
                fetch_videos_for_channel(ch);
            }
        } else {
            message_box("Can't add channel", "There is no channel with this name!");
        }
    }
}

void handle_add_channel_by_id()
{
    std::string channel_id = get_string("Add new Channel", "Enter channel ID");
    if(channel_id.empty()) {
        return;
    } else if(std::find_if(channels.cbegin(), channels.cend(), [&](const Channel &channel){ return channel.id == channel_id; }) != channels.cend()) {
        message_box("Can't add channel", "A channel with this ID is already in the list!");
        return;
    } else {
        Channel ch = Channel::add(db, "id", channel_id);
        if(ch.is_valid()) {
            add_channel_to_list(ch);
            select_channel_by_id(ch.id);
            tp_flush();
            if(message_box("Update now?", "Fetch videos for this channel now?", Button::Yes | Button::No, Button::Yes) == Button::Yes) {
                fetch_videos_for_channel(ch);
            }
        } else {
            message_box("Can't add channel", "There is no channel with this ID!");
        }
    }
}

void handle_watch_video(Video &video, bool mark_only) {
    const std::string url = "https://youtube.com/watch?v=";
    const std::string cmd = "xdg-open \"" + url + video.id + "\" > /dev/null";
    if(!mark_only) {
        int rc = system(cmd.c_str());
        if(rc)
            message_box("Failed to open browser", ("xdg-open failed with error " + std::to_string(rc)).c_str());
    }
    video.set_flag(db, kWatched);
}

void handle_mark_all_videos_watched(Channel &channel) {
    sqlite3_exec(db, "BEGIN TRANSACTION;", nullptr, nullptr, nullptr);
    for(Video &video: videos[channel.id]) {
        video.set_flag(db, kWatched);
    }
    sqlite3_exec(db, "COMMIT TRANSACTION;", nullptr, nullptr, nullptr);
    channel.load_info(db);
}

void action_select_channel() {
    if(channels.size()) {
        std::vector<std::string> names;
        for(const Channel &c: channels)
            names.push_back(c.name + " (" + std::to_string(c.unwatched) + ")");
        const int channel = get_selection("Switch Channel", names, *selected_channel, Align::VCenter | Align::Left);
        if(channel != -1)
            select_channel_by_index(channel);
    } else {
        message_box("Can't select channel", "No channels configured.\n Please configure one.");
    }
}

void action_refresh_channel() {
    Channel &ch = channels[*selected_channel];
    fetch_videos_for_channel(ch);
}

void action_refresh_all_channels() {
    if(message_box("Refresh all channels?", ("Do you want to refresh all " + std::to_string(channels.size()) + " channels?").c_str(), Button::Yes | Button::No, Button::No) != Button::Yes)
        return;
    for(Channel &channel: channels) {
        fetch_videos_for_channel(channel, true);
    }
}

void action_mark_video_watched() {
    Channel &ch = channels[*selected_channel];
    Video &video = videos[ch.id][selected_video];
    video.set_flag(db, kWatched);
    ch.load_info(db);
}

void action_watch_video() {
    Channel &ch = channels[*selected_channel];
    Video &video = videos[ch.id][selected_video];

    const std::string url = "https://youtube.com/watch?v=";
    const std::string cmd = "xdg-open \"" + url + video.id + "\" > /dev/null";
    int rc = system(cmd.c_str());
    if(rc) {
        message_box("Failed to open browser", ("xdg-open failed with error " + std::to_string(rc)).c_str());
    } else {
        video.set_flag(db, kWatched);
        ch.load_info(db);
    }
}

void action_mark_video_unwatched() {
    Channel &ch = channels[*selected_channel];
    Video &selected = videos[ch.id][selected_video];
    selected.set_flag(db, kWatched, false);
    ch.load_info(db);
}

void action_mark_all_videos_watched() {
    Channel &ch = channels[*selected_channel];
    if(message_box("Mark all as watched", ("Do you want to mark all videos of " + ch.name + " as watched?").c_str(), Button::Yes | Button::No, Button::No) != Button::Yes)
        return;
    handle_mark_all_videos_watched(ch);
}

void action_select_prev_channel() {
    if(selected_channel && *selected_channel > 0)
        select_channel_by_index(*selected_channel - 1);
}

void action_select_next_channel() {
    if(selected_channel && *selected_channel < channels.size() - 1)
        select_channel_by_index(*selected_channel + 1);
}

void action_select_prev_video() {
    if(selected_video > 0)
        selected_video--;
}

void action_select_next_video() {
    if(selected_video < current_video_count - 1)
        selected_video++;
}

void action_select_prev_video_page() {
    if(selected_video > 0)
        selected_video -= std::min(selected_video, videos_per_page);
}

void action_select_next_video_page() {
    if(selected_video < current_video_count - 1)
        selected_video++;
}

void action_select_first_video() {
    selected_video = 0;
}

void action_select_last_video() {
    selected_video = current_video_count - 1;
}

void action_scroll_title_left() {
    if(title_offset > 0)
        title_offset--;
}

void action_scroll_title_right() {
    if(any_title_in_next_half)
        title_offset++;
}

void action_show_video_detail() {
    message_box("Details", "Video details go here...");
}

using json = nlohmann::json;
std::optional<json> load_json(const std::string &filename) {
    std::ifstream ifs(filename);
    if(!ifs.is_open())
        return {};
    try {
        json data;
        ifs >> data;
        return data;
    } catch (json::parse_error &err) {
        fprintf(stderr, "Failed to parse JSON file %s: %s\n", filename.c_str(), err.what());
        return {};
    }
}

bool save_json(const std::string &filename, const json &data) {
    std::ofstream ofs(filename);
    if(!ofs.is_open())
        return false;
    try {
        ofs << data;
        return true;
    }  catch (json::exception &err) {
        return false;
    }
}

int main()
{
    user_home = std::string(std::getenv("HOME"));
    std::vector<std::string> config_locations{user_home + "/.config/", "./"};

    curl_global_init(CURL_GLOBAL_ALL);
    tp_init();

    nlohmann::json config;
    for(const std::string &location: config_locations) {
        std::string config_file(location + "yttui.conf");
        auto config_data = load_json(config_file);
        if(config_data) {
            config = *config_data;
            fprintf(stderr, "Using config file %s...\n", config_file.c_str());
            break;
        } else {
            fprintf(stderr, "Can't read config file %s...\n", config_file.c_str());
        }
    }
    if(!config.empty()) {
        if(config.contains("apiKey") && config["apiKey"].is_string()) {
            yt_config.api_key = config["apiKey"];
        } else {
            tui_abort("A YouTube API key is required for this application to function.\n  Please provide one in the config file.");
        }
        if(config.contains("extraHeaders") && config["extraHeaders"].is_array()) {
            for(const json &elem: config["extraHeaders"]) {
                if(elem.contains("key") && elem["key"].is_string() && elem.contains("value") && elem["value"].is_string()) {
                    yt_config.extra_headers.emplace(elem["key"], elem["value"]);
                }
            }
        }
    }

    sqlite3_open("yttool.sqlite", &db);

    sqlite3_stmt *query;
    sqlite3_prepare_v2(db, "SELECT * FROM channels;", -1, &query, nullptr);
    while(sqlite3_step(query) == SQLITE_ROW) {
        Channel channel(query);
        add_channel_to_list(channel);
    }
    sqlite3_finalize(query);

    if(channels.size())
        select_channel_by_index(0);

    bool exit = false;
    std::vector<action> actions = {
        {TERMPAINT_EV_CHAR, "a", 0, handle_add_channel_by_name, "Add channel by name"},
        {TERMPAINT_EV_CHAR, "A", 0, handle_add_channel_by_id, "Add channel by Id"},
        {TERMPAINT_EV_CHAR, "c", 0, action_select_channel, "Select channel"},
        {TERMPAINT_EV_CHAR, "j", 0, action_select_prev_channel, "Select previous channel"},
        {TERMPAINT_EV_CHAR, "k", 0, action_select_next_channel, "Select next channel"},
        {TERMPAINT_EV_CHAR, "r", 0, action_refresh_channel, "Refresh selected channel"},
        {TERMPAINT_EV_CHAR, "R", 0, action_refresh_all_channels, "Refresh all channels"},
        {TERMPAINT_EV_CHAR, "w", 0, action_watch_video, "Watch video"},
        {TERMPAINT_EV_CHAR, "w", TERMPAINT_MOD_ALT, action_mark_video_watched, "Mark video as watched"},
        {TERMPAINT_EV_CHAR, "u", 0, action_mark_video_unwatched, "Mark video as unwatched"},
        {TERMPAINT_EV_CHAR, "W", 0, action_mark_all_videos_watched, "Mark channel as watched"},
        {TERMPAINT_EV_CHAR, "q", TERMPAINT_MOD_CTRL, [&](){exit = true;}, "Quit"},

        {TERMPAINT_EV_KEY, "Space", 0, action_show_video_detail, "Show video details"},
        {TERMPAINT_EV_KEY, "ArrowUp", 0, action_select_prev_video, "Previous video"},
        {TERMPAINT_EV_KEY, "ArrowDown", 0, action_select_next_video, "Next video"},
        {TERMPAINT_EV_KEY, "PageUp", 0, action_select_prev_video_page, "Previous video page"},
        {TERMPAINT_EV_KEY, "PageDown", 0, action_select_next_video_page, "Next video page"},
        {TERMPAINT_EV_KEY, "Home", 0, action_select_first_video, "First video"},
        {TERMPAINT_EV_KEY, "End", 0, action_select_last_video, "Last video"},
        {TERMPAINT_EV_KEY, "ArrowLeft", 0, action_scroll_title_left, "Scroll title left"},
        {TERMPAINT_EV_KEY, "ArrowRight", 0, action_scroll_title_right, "Scroll title right"},
    };

    do {
        termpaint_surface_clear(surface, TERMPAINT_DEFAULT_COLOR, TERMPAINT_DEFAULT_COLOR);
        if(selected_channel)
            draw_channel_list(videos[channels.at(*selected_channel).id]);
        else
            draw_no_channels_msg();
        tp_flush();

        auto event = tp_wait_for_event();
        if(!event)
            abort();

        handle_action(*event, actions);
    } while (!exit);

    tp_shutdown();
    sqlite3_close(db);
    curl_global_cleanup();

    return 0;
}
