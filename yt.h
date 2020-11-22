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

class Channel
{
public:
    std::string id;
    std::string name;

    Channel(sqlite3_stmt *row);
    static Channel add(sqlite3 *db, const std::string &selector, const std::string &value);
    static std::vector<Channel> get_all(sqlite3 *db);

    std::string upload_playlist() const;
    void fetch_new_videos(sqlite3 *db, progress_info *info=nullptr, std::optional<std::string> after={}, std::optional<int> max_count={});
    void load_info(sqlite3 *db);
    void videos_watched(int count);
    bool is_valid() const;

    unsigned int video_count;
    unsigned int unwatched;
private:
    Channel(const std::string &id, const std::string &name);
};

enum VideoFlag {
    kNone = 0,
    kWatched = (1<<0),
    kDownloaded = (1<<1),
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

    size_t tui_title_width;
};
