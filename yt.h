// SPDX-License-Identifier: MIT
#pragma once

#include <map>
#include <optional>
#include <string>
#include <vector>

class sqlite3;
class sqlite3_stmt;
class progress_info;

extern struct yt_config {
    std::string api_key;
    std::map<std::string, std::string> extra_headers;
} yt_config;

class UserFlag
{
public:
    int id;
    std::string name;

    UserFlag(sqlite3_stmt *row);
    void save(sqlite3 *db) const;

    static UserFlag create(sqlite3 *db, const std::string &name);
    static int next_free(sqlite3 *db);
    static std::vector<UserFlag> get_all(sqlite3 *db);

    static constexpr int max_flag_count = (sizeof(uint32_t)*8);

private:
    UserFlag(int id, const std::string &name);
};

enum VideoFlag {
    kNone = 0,
    kWatched = (1<<0),
    kDownloaded = (1<<1),
};

class ChannelFilter {
public:
    int id;
    std::string name;

    uint32_t video_mask;
    uint32_t video_value;

    uint32_t user_mask;
    uint32_t user_value;

    ChannelFilter();
    ChannelFilter(sqlite3_stmt *row);
    void save(sqlite3 *db) const;

    static ChannelFilter add(sqlite3 *db, const std::string &name);
    static std::vector<ChannelFilter> get_all(sqlite3 *db);
private:
    ChannelFilter(const int id, const std::string &name);
};

class Channel
{
public:
    std::string id;
    std::string name;
    bool is_virtual;
    ChannelFilter filter;

    int user_flags;

    Channel(sqlite3_stmt *row);
    static Channel add(sqlite3 *db, const std::string &selector, const std::string &value);
    static Channel add_virtual(const std::string &name, const ChannelFilter &filter);
    static std::vector<Channel> get_all(sqlite3 *db);

    std::string upload_playlist() const;
    int fetch_new_videos(sqlite3 *db, progress_info *info=nullptr, std::optional<std::string> after={}, std::optional<int> max_count={}) const;
    void load_info(sqlite3 *db);
    bool is_valid() const;

    void save_user_flags(sqlite3 *db) const;

    unsigned int unwatched;
    size_t tui_name_width;
private:
    Channel(const std::string &id, const std::string &name);
};

struct Video
{
    std::string id;
    std::string channel_id;
    std::string title;
    std::string description;
    int flags;
    std::string added_to_playlist;
    std::string published;

    Video(sqlite3_stmt *row);
    void set_flag(sqlite3 *db, VideoFlag flag, bool value=true);
    static std::vector<Video> get_all_for_channel(const std::string &channel_id);
    static std::vector<Video> get_all_with_filter(const ChannelFilter &filter);

    size_t tui_title_width;
};
