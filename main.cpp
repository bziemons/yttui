// SPDX-License-Identifier: MIT
#define _X_OPEN_SOURCE

#include "tui.h"
#include "yt.h"
#include "db.h"
#include "subprocess.h"

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <unordered_map>
#include <fstream>

#include <time.h>
#include <libgen.h>
#include <stdio.h>

#include <curl/curl.h>
#include <nlohmann/json.hpp>

static std::string user_home;

std::vector<Channel> channels;
std::unordered_map<std::string, std::vector<Video>> videos;

size_t selected_channel;
size_t current_video_count = 0;
size_t selected_video = 0;
size_t videos_per_page = 0;
size_t current_page_count = 0;
size_t title_offset = 0;
bool any_title_in_next_half = false;
bool clear_channels_on_change = false;

static termpaint_attr* get_attr(const AttributeSetType type, const bool highlight=false)
{
    return highlight ? attributes[type].highlight : attributes[type].normal;
}

void draw_channel_list(const std::vector<Video> &videos)
{
    const size_t cols = termpaint_surface_width(surface);
    const size_t rows = termpaint_surface_height(surface);

    const size_t column_spacing = 2;

    const size_t date_column = 0;
    const size_t date_width = std::string("xxxx-xx-xx xx:xx").size();
    const size_t first_name_column = date_column + date_width + column_spacing;
    const size_t last_name_column = cols;
    const size_t name_quater = (last_name_column - first_name_column) / 4;

    const size_t start_row = 2;
    const size_t available_rows = rows - 2;
    videos_per_page = available_rows;

    const int pages = videos.size() / available_rows;
    const int cur_page = selected_video / available_rows;

    const std::string channel_name = std::string("Channel: ") + channels[selected_channel].name.c_str();
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
                           "a    Add channel by name\n"
                           "A    Add channel by ID\n"
                           "Or press F1 for help or C-q to quit.");
    {
        std::pair<size_t, size_t> size = string_size(text);
        write_multiline_string((cols - size.first) / 2, (rows - size.second) / 2, text, get_attr(ASNormal));
    }
}

void load_videos_for_channel(const Channel &channel, bool force=false)
{
    if(!force && videos.find(channel.id) != videos.end() && !videos[channel.id].empty())
        return;

    std::vector<Video> &channelVideos = videos[channel.id];
    if(channel.is_virtual) {
        channelVideos = Video::get_all_with_flag_value(channel.virtual_flag, channel.virtual_flag_value);
    } else {
        channelVideos = Video::get_all_for_channel(channel.id);
    }

    for(Video &video: channelVideos) {
        video.tui_title_width = string_width(video.title);
    }

    if(channels[selected_channel].id == channel.id)
        selected_video = 0;
}

int fetch_videos_for_channel(Channel &channel, bool name_in_title=false)
{
    if(channel.is_virtual) {
        std::vector<Video> &channelVideos = videos[channel.id];
        channelVideos = Video::get_all_with_flag_value(channel.virtual_flag, channel.virtual_flag_value);
        for(Video &video: channelVideos) {
            video.tui_title_width = string_width(video.title);
        }
        return channelVideos.size();
    }

    std::string title("Refreshing");
    if(name_in_title)
        title.append(" ").append(channel.name);
    title.append("…");
    progress_info *info = begin_progress(title, 30);
    const int new_video_count = channel.fetch_new_videos(db, info);
    end_progress(info);
    load_videos_for_channel(channels[selected_channel], true);
    channel.load_info(db);

    return new_video_count;
}

bool startswith(const std::string &str, const std::string &with)
{
    const size_t len = with.length();
    return str.substr(0, len) == with;
}

std::string replace(const std::string &str, const std::string &what, const std::string &with)
{
    const size_t replace_length = what.size();
    std::string out = str;
    size_t pos = 0;
    while((pos = out.find(what, pos)) != std::string::npos) {
        out.replace(pos, replace_length, with);
    }
    return out;
}

void select_channel_by_index(const int index) {
    if(clear_channels_on_change) {
        for(auto &[k, v]: videos) {
            v.clear();
        }
    }
    selected_channel = index;
    const Channel &channel = channels.at(selected_channel);
    selected_video = 0;
    load_videos_for_channel(channel, clear_channels_on_change);
    current_video_count = videos[channel.id].size();
    clear_channels_on_change = channel.is_virtual;
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
    if(selected_channel < channels.size())
        selected_channel_id = channels[selected_channel].id;
    channels.push_back(channel);

    std::sort(channels.begin(), channels.end(), [](const Channel &a, const Channel &b){ if(a.is_virtual != b.is_virtual) { return a.is_virtual > b.is_virtual; } return a.name < b.name; });

    if(!selected_channel_id.empty()) {
        const size_t new_index = std::distance(channels.cbegin(), std::find_if(channels.cbegin(), channels.cend(), [&](const Channel &ch){ return ch.id == selected_channel_id; }));
        selected_channel = new_index;
    }
}

void make_virtual_unwatched_channel()
{
    Channel channel = Channel::add_virtual("All Unwatched", kWatched, false);
    std::vector<Video> &channelVideos = videos[channel.id];
    channelVideos = Video::get_all_with_flag_value(channel.virtual_flag, channel.virtual_flag_value);
    for(Video &video: channelVideos) {
        video.tui_title_width = string_width(video.title);
    }
    add_channel_to_list(channel);
}

void action_add_channel_by_name()
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

void action_add_channel_by_id()
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

void action_select_channel() {
    if(channels.size()) {
        std::vector<std::string> names;
        for(const Channel &c: channels)
            names.push_back(c.name + " (" + std::to_string(c.unwatched) + ")");
        const int channel = get_selection("Switch Channel", names, selected_channel, Align::VCenter | Align::Left);
        if(channel != -1)
            select_channel_by_index(channel);
    } else {
        message_box("Can't select channel", "No channels configured.\n Please configure one.");
    }
}

bool run_command(const std::vector<std::string> &cmd, const std::vector<std::pair<std::string, std::string>> &placeholders={}) {
    const size_t cmd_size = cmd.size();

    if(!cmd_size)
        return true;

    const char *cmdline[cmd_size + 1];
    for(size_t c=0; c<cmd_size; c++) {
        std::string arg = cmd[c];
        for(size_t p=0; p<placeholders.size(); p++) {
            const auto &[what, with] = placeholders.at(p);
            arg = replace(arg, what, with);
        }
        cmdline[c] = strdup(arg.c_str());
    }
    cmdline[cmd_size] = nullptr;

    subprocess_s proc;
    const int rc = subprocess_create(cmdline, subprocess_option_inherit_environment, &proc);
    if(rc != 0) {
        const std::string message = cmd.at(0) + " failed with error " + std::to_string(rc);
        message_box("Failed to run command", message);
    }
    subprocess_join(&proc, nullptr);
    for(size_t i=0; i<cmd_size; i++) {
        free((void*)cmdline[i]);
    }
    return rc == 0;
}

std::vector<std::string> notify_channel_new_video_command;
std::vector<std::string> notify_channel_new_videos_command;
std::vector<std::string> notify_channels_new_videos_command;

void action_refresh_channel() {
    Channel &ch = channels.at(selected_channel);
    const int new_videos = fetch_videos_for_channel(channels.at(selected_channel));
    if(new_videos == 0)
        return;
    if(new_videos == 1 && !notify_channel_new_video_command.empty()) {
        run_command(notify_channel_new_video_command, {
                        {"{{channelName}}", ch.name},
                        {"{{videoTitle}}", videos[ch.id].front().title},
                    });
    } else if(notify_channel_new_videos_command.size()) {
        run_command(notify_channel_new_videos_command, {
                        {"{{channelName}}", ch.name},
                        {"{{newVideos}}", std::to_string(new_videos)}
                    });
    }

}

void action_refresh_all_channels() {
    if(message_box("Refresh all channels?", ("Do you want to refresh all " + std::to_string(channels.size()) + " channels?").c_str(), Button::Yes | Button::No, Button::No) != Button::Yes)
        return;
    int updated_channels = 0;
    int new_videos = 0;
    for(Channel &channel: channels) {
        const int count = fetch_videos_for_channel(channel, true);
        new_videos += count;
        if(count)
            updated_channels++;
    }
    if(updated_channels && new_videos && !notify_channels_new_videos_command.empty()) {
        run_command(notify_channels_new_videos_command, {
                        {"{{updatedChannels}}", std::to_string(updated_channels)},
                        {"{{newVideos}}", std::to_string(new_videos)}
                    });
    }
}

void action_mark_video_watched() {
    Channel &ch = channels.at(selected_channel);
    Video &video = videos[ch.id][selected_video];
    video.set_flag(db, kWatched);
    ch.load_info(db);
}

std::vector<std::string> watch_command = {"xdg-open", "https://youtube.com/watch?v={{vid}}"};

void action_watch_video() {
    Channel &ch = channels.at(selected_channel);
    Video &video = videos[ch.id][selected_video];

    if(run_command(watch_command, {{"{{vid}}", video.id}})) {
        video.set_flag(db, kWatched);
        ch.load_info(db);
    }
}

void action_mark_video_unwatched() {
    Channel &ch = channels.at(selected_channel);
    Video &selected = videos[ch.id][selected_video];
    selected.set_flag(db, kWatched, false);
    ch.load_info(db);
}

void action_mark_all_videos_watched() {
    Channel &ch = channels.at(selected_channel);
    if(message_box("Mark all as watched", ("Do you want to mark all videos of " + ch.name + " as watched?").c_str(), Button::Yes | Button::No, Button::No) != Button::Yes)
        return;
    {
        db_transaction transaction;
        for(Video &video: videos[ch.id]) {
            video.set_flag(db, kWatched);
        }
    }
    ch.load_info(db);
}

void action_select_prev_channel() {
    if(selected_channel > 0)
        select_channel_by_index(selected_channel - 1);
}

void action_select_next_channel() {
    if(selected_channel < channels.size() - 1)
        select_channel_by_index(selected_channel + 1);
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
        selected_video += std::min(current_video_count - 1 - selected_video, videos_per_page);
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
        tui_abort("Failed to parse JSON file %s: %s", filename.c_str(), err.what());
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

static std::string get_module_path() {
    std::string str;
    char *exe = realpath("/proc/self/exe", nullptr);
    if(exe) {
        str = std::string(exe);
        free(exe);
        exe = nullptr;
    } else {
        perror("realpath");
    }
    exe = dirname((char*)str.c_str());

    return std::string(exe);
}

void config_get_string_list(std::vector<std::string> &buffer, const json &obj, const std::string &key) {
    if(!obj.contains(key) || !obj[key].is_array())
        return;
    buffer.clear();

    for(const json &elem: obj[key]) {
        if(!elem.is_string()) {
            tui_abort("Configuration error: " + key + " element " + elem.dump() + " is not a string but " + elem.type_name() + ".");
        }
        buffer.push_back(elem);
    }
}

int main()
{
    const std::string module_path = get_module_path();
    user_home = std::string(std::getenv("HOME"));
    std::string database_filename = user_home + "/.local/share/yttui.db";

    curl_global_init(CURL_GLOBAL_ALL);
    tp_init();

    const std::vector<std::string> config_locations{user_home + "/.config", module_path};
    std::string config_file;
    nlohmann::json config;
    for(const std::string &location: config_locations) {
        config_file = location + "/yttui.conf";
        auto config_data = load_json(config_file);
        if(config_data) {
            config = *config_data;
            break;
        }
    }
    if(config.count("apiKey") && config["apiKey"].is_string()) {
        yt_config.api_key = config["apiKey"];
    } else {
        tui_abort("A YouTube API key is required for this application to function.\nPlease provide one in the config file.\n\nCurrent config file:\n" + config_file);
    }
    if(config.count("extraHeaders") && config["extraHeaders"].is_array()) {
        for(const json &elem: config["extraHeaders"]) {
            if(elem.count("key") && elem["key"].is_string() && elem.count("value") && elem["value"].is_string()) {
                yt_config.extra_headers.emplace(elem["key"], elem["value"]);
            }
        }
    }
    if(config.count("database") && config["database"].is_string()) {
        database_filename = replace(config["database"], "$HOME", user_home);
    }
    config_get_string_list(watch_command, config, "watchCommand");
    if(config.contains("notifications") && config["notifications"].is_object()) {
        const json &notifications = config["notifications"];
        config_get_string_list(notify_channel_new_video_command, notifications, "channelNewVideoCommand");
        config_get_string_list(notify_channel_new_videos_command, notifications, "channelNewVideosCommand");
        config_get_string_list(notify_channels_new_videos_command, notifications, "channelsNewVideosCommand");
    }

    db_init(database_filename);
    make_virtual_unwatched_channel();
    for(Channel &channel: Channel::get_all(db)) {
        add_channel_to_list(channel);
    }

    if(channels.size())
        select_channel_by_index(0);

    bool exit = false;
    bool force_repaint = false;
    std::vector<action> actions = {
        {TERMPAINT_EV_CHAR, "a", 0, action_add_channel_by_name, "Add channel by name"},
        {TERMPAINT_EV_CHAR, "A", 0, action_add_channel_by_id, "Add channel by Id"},
        {TERMPAINT_EV_CHAR, "c", 0, action_select_channel, "Select channel"},
        {TERMPAINT_EV_CHAR, "j", 0, action_select_prev_channel, "Select previous channel"},
        {TERMPAINT_EV_CHAR, "k", 0, action_select_next_channel, "Select next channel"},
        {TERMPAINT_EV_CHAR, "r", 0, action_refresh_channel, "Refresh selected channel"},
        {TERMPAINT_EV_CHAR, "R", 0, action_refresh_all_channels, "Refresh all channels"},
        {TERMPAINT_EV_CHAR, "w", 0, action_watch_video, "Watch video"},
        {TERMPAINT_EV_CHAR, "w", TERMPAINT_MOD_ALT, action_mark_video_watched, "Mark video as watched"},
        {TERMPAINT_EV_CHAR, "u", 0, action_mark_video_unwatched, "Mark video as unwatched"},
        {TERMPAINT_EV_CHAR, "W", 0, action_mark_all_videos_watched, "Mark channel as watched"},
        {TERMPAINT_EV_CHAR, "q", TERMPAINT_MOD_CTRL, [&](){ exit = true; }, "Quit"},

        //{TERMPAINT_EV_KEY, "Enter", 0, action_show_video_detail, "Show video details"},
        {TERMPAINT_EV_KEY, "ArrowUp", 0, action_select_prev_video, "Previous video"},
        {TERMPAINT_EV_KEY, "ArrowDown", 0, action_select_next_video, "Next video"},
        {TERMPAINT_EV_KEY, "PageUp", 0, action_select_prev_video_page, "Previous video page"},
        {TERMPAINT_EV_KEY, "PageDown", 0, action_select_next_video_page, "Next video page"},
        {TERMPAINT_EV_KEY, "Home", 0, action_select_first_video, "First video"},
        {TERMPAINT_EV_KEY, "End", 0, action_select_last_video, "Last video"},
        {TERMPAINT_EV_KEY, "ArrowLeft", 0, action_scroll_title_left, "Scroll title left"},
        {TERMPAINT_EV_KEY, "ArrowRight", 0, action_scroll_title_right, "Scroll title right"},
        {TERMPAINT_EV_CHAR, "l", TERMPAINT_MOD_CTRL, [&](){ force_repaint = true; }, "Force redraw"},
    };

    do {
        termpaint_surface_clear(surface, TERMPAINT_DEFAULT_COLOR, TERMPAINT_DEFAULT_COLOR);
        draw_channel_list(videos[channels.at(selected_channel).id]);
        tp_flush(force_repaint);
        force_repaint = false;

        auto event = tp_wait_for_event();
        if(!event)
            abort();

        tui_handle_action(*event, actions);
    } while (!exit);

    tp_shutdown();
    db_shutdown();
    curl_global_cleanup();

    return 0;
}
