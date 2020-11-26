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

enum VideoFlag {
    kNone = 0,
    kWatched = (1<<0),
    kDownloaded = (1<<1),
};

class Channel
{
public:
    std::string id;
    std::string name;
    bool is_virtual;
    VideoFlag virtual_flag;
    bool virtual_flag_value;

    Channel(sqlite3_stmt *row);
    static Channel add(sqlite3 *db, const std::string &selector, const std::string &value);
    static Channel add_virtual(const std::string &name, const VideoFlag virtual_flag=kNone, const bool virtual_flag_value=true);
    static std::vector<Channel> get_all(sqlite3 *db);

    std::string upload_playlist() const;
    void fetch_new_videos(sqlite3 *db, progress_info *info=nullptr, std::optional<std::string> after={}, std::optional<int> max_count={});
    void load_info(sqlite3 *db);
    bool is_valid() const;

    unsigned int video_count;
    unsigned int unwatched;
private:
    Channel(const std::string &id, const std::string &name);
};

struct Video
{
    std::string id;
    std::string title;
    std::string description;
    int flags;
    std::string published;

    Video(sqlite3_stmt *row);
    void set_flag(sqlite3 *db, VideoFlag flag, bool value=true);
    static std::vector<Video> get_all_for_channel(const std::string &channel_id);
    static std::vector<Video> get_all_with_flag_value(const VideoFlag flag, const int value);

    size_t tui_title_width;
};
