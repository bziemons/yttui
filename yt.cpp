// SPDX-License-Identifier: MIT
#include "yt.h"

#include <sqlite3.h>
#include <nlohmann/json.hpp>
#include <curl/curl.h>

#include "tui.h"
#include "db.h"

using json = nlohmann::json;
struct yt_config yt_config;

static size_t curl_writecallback(void *data, size_t size, size_t nmemb, void *userp)
{
    size_t to_add = size * nmemb;
    std::vector<unsigned char> *buffer = reinterpret_cast<std::vector<unsigned char>*>(userp);
    const size_t cur_size = buffer->size();
    buffer->resize(cur_size + to_add);
    memcpy(buffer->data() + cur_size, data, to_add);

    return to_add;
}

static json api_request(const std::string &url, std::map<std::string, std::string> params)
{
    CURL *curl = curl_easy_init();
    curl_slist *headers = nullptr;
    for(const auto &[header, value]: yt_config.extra_headers) {
        headers = curl_slist_append(headers, (header + ": " + value).c_str());
    }

    CURLU *u = curl_url();
    curl_url_set(u, CURLUPART_URL, url.c_str(), 0);
    for(const auto &[k, v]: params) {
        std::string p = k + "=" + v;
        curl_url_set(u, CURLUPART_QUERY, p.c_str(), CURLU_APPENDQUERY|CURLU_URLENCODE);
    }
    char *real_url;
    curl_url_get(u, CURLUPART_URL, &real_url, 0);

    std::vector<unsigned char> data;

    curl_easy_setopt(curl, CURLOPT_URL, real_url);
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curl_writecallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void *)&data);

    //curl_easy_setopt(curl, CURLOPT_VERBOSE, 1L);
    curl_easy_perform(curl);
    curl_free(real_url);

    data.push_back(0);

    int http_response = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_response);

    curl_url_cleanup(u);
    curl_easy_cleanup(curl);
    if(headers)
        curl_slist_free_all(headers);

    try {
        return json::parse(data);
    } catch (json::exception &err) {
        tui_abort("Failed to parse YouTube API response:\n%s", err.what());
    }
    return {};
}

Channel::Channel(sqlite3_stmt *row): id(get_string(row, 0)), name(get_string(row, 1)), is_virtual(false), virtual_flag(kNone), virtual_flag_value(false)
{
}

Channel::Channel(const std::string &id, const std::string &name): id(id), name(name), is_virtual(false), virtual_flag(kNone), virtual_flag_value(false)
{
}

Channel Channel::add(sqlite3 *db, const std::string &selector, const std::string &value)
{
    std::map<std::string, std::string> params = {
        {"part", "snippet"},
        {selector, value},
        {"key", yt_config.api_key},
    };

    const json response = api_request("https://content.googleapis.com/youtube/v3/channels", params);

    // Error responses dont have pageInfo items
    if(!response.count("pageInfo")) {
        return Channel("", "");
    }

    const json page_info = response["pageInfo"];
    const bool any_results = page_info["totalResults"] > 0;

    if(!any_results) {
        return Channel("", "");
    }

    const std::string channel_id = response["items"][0]["id"];
    const std::string channel_name = response["items"][0]["snippet"]["title"];

    sqlite3_stmt *query;
    SC(sqlite3_prepare_v2(db, "INSERT INTO channels(channelId, name) VALUES(?1, ?2);", -1, &query, nullptr));
    SC(sqlite3_bind_text(query, 1, channel_id.c_str(), -1, SQLITE_TRANSIENT));
    SC(sqlite3_bind_text(query, 2, channel_name.c_str(), -1, SQLITE_TRANSIENT));
    sqlite3_step(query);
    SC(sqlite3_finalize(query));

    return Channel(channel_id, channel_name);
}

Channel Channel::add_virtual(const std::string &name, const VideoFlag virtual_flag, const bool virtual_flag_value)
{
    std::string id = name;
    std::transform(id.begin(), id.end(), id.begin(), [](char c){ return std::isalnum(c) ? std::tolower(c) : '-'; });
    Channel channel("virtual-" + id, name);
    channel.is_virtual = true;
    channel.virtual_flag = virtual_flag;
    channel.virtual_flag_value = virtual_flag_value;
    return channel;
}

std::vector<Channel> Channel::get_all(sqlite3 *db)
{
    std::vector<Channel> channels;

    sqlite3_stmt *query;
    SC(sqlite3_prepare_v2(db, "SELECT * FROM channels;", -1, &query, nullptr));
    while(sqlite3_step(query) == SQLITE_ROW) {
        channels.emplace_back(query);
    }
    SC(sqlite3_finalize(query));

    return channels;
}

std::string Channel::upload_playlist() const
{
    return "UU" + id.substr(2);
}

bool video_is_known(sqlite3 *db, const std::string &channel_id, const std::string &video_id)
{
    sqlite3_stmt *query;
    SC(sqlite3_prepare_v2(db, "SELECT 1 FROM videos WHERE channelId=?1 AND videoId=?2 LIMIT 1;", -1, &query, nullptr));
    SC(sqlite3_bind_text(query, 1, channel_id.c_str(), -1, SQLITE_TRANSIENT));
    SC(sqlite3_bind_text(query, 2, video_id.c_str(), -1, SQLITE_TRANSIENT));

    bool known = false;
    const int res = sqlite3_step(query);
    if(res == SQLITE_ROW) {
        known =  sqlite3_column_int(query, 0) > 0;
    } else if(res == SQLITE_DONE) {
        known = false;
    }
    SC(sqlite3_finalize(query));

    return known;
}

void add_video(sqlite3 *db, const json &snippet, const std::string &channel_id) {
    const std::string video_id = snippet["resourceId"]["videoId"];
    const std::string title = snippet["title"];
    const std::string description = snippet["description"];
    const int flags = 0;
    const std::string published = snippet["publishedAt"];

    sqlite3_stmt *query;
    SC(sqlite3_prepare_v2(db, "INSERT INTO videos (videoId, channelId, title, description, flags, published) values(?1,?2,?3,?4,?5,?6);", -1, &query, nullptr));
    SC(sqlite3_bind_text(query, 1, video_id.c_str(), -1, SQLITE_TRANSIENT));
    SC(sqlite3_bind_text(query, 2, channel_id.c_str(), -1, SQLITE_TRANSIENT));
    SC(sqlite3_bind_text(query, 3, title.c_str(), -1, SQLITE_TRANSIENT));
    SC(sqlite3_bind_text(query, 4, description.c_str(), -1, SQLITE_TRANSIENT));
    SC(sqlite3_bind_int(query, 5, flags));
    SC(sqlite3_bind_text(query, 6, published.c_str(), -1, SQLITE_TRANSIENT));
    sqlite3_step(query);
    SC(sqlite3_finalize(query));
}


int Channel::fetch_new_videos(sqlite3 *db, progress_info *info, std::optional<std::string> after, std::optional<int> max_count)
{
    const std::string playlist_id = upload_playlist();
    std::map<std::string, std::string> params = {
        {"part", "snippet"},
        {"playlistId", playlist_id},
        {"maxResults", "50"},
        {"key", yt_config.api_key},
    };

    db_transaction transaction;

    int processed = 0;
    bool abort = false;
    while(true) {
        const json response = api_request("https://content.googleapis.com/youtube/v3/playlistItems", params);

        if(response.empty())
            break;

        for(auto &item: response["items"]) {
            auto snippet = item["snippet"];
            std::string channel_id = snippet["channelId"];
            std::string video_id = snippet["resourceId"]["videoId"];
            std::string title = snippet["title"];

            if(after) {
                auto publishedAt = snippet["publishedAt"];
                if(publishedAt < *after) {
                    //fprintf(stderr, "Stopping at video '%s': Too old.\r\n", title.c_str());
                    abort = true;
                    break;
                }
            }

            if(video_is_known(db, channel_id, video_id)) {
                //fprintf(stderr, "Stopping at video '%s': Already known.\r\n", title.c_str());
                abort = true;
                break;
            }

            add_video(db, snippet, channel_id);
            //fprintf(stderr, "New video: '%s': %s.\r\n", title.c_str(), video_id.c_str());
            processed++;
            if(max_count && processed >= *max_count) {
                abort = true;
                break;
            }
        }

        const json page_info = response["pageInfo"];
        const int results = page_info["totalResults"];
        if(info)
            update_progress(info, processed, results);

        if(!abort && response.count("nextPageToken")) {
            params["pageToken"] = response["nextPageToken"];
            //fprintf(stderr, "Processed %d. Next page...\r\n", processed);
        } else {
            break;
        }
    }

    return processed;
}

void Channel::load_info(sqlite3 *db)
{
    unwatched = 0;

    if(is_virtual) {
        return;
    }

    sqlite3_stmt *query;
    SC(sqlite3_prepare_v2(db, "SELECT flags, count(*) as videos FROM videos where channelId = ?1 GROUP by flags;", -1, &query, nullptr));
    SC(sqlite3_bind_text(query, 1, id.c_str(), -1, SQLITE_TRANSIENT));
    while(sqlite3_step(query) == SQLITE_ROW) {
        const int flags = sqlite3_column_int(query, 0);
        const int count = sqlite3_column_int(query, 1);

        if((flags & kWatched) == 0)
            unwatched += count;
    }
    SC(sqlite3_finalize(query));
}

bool Channel::is_valid() const
{
    return !id.empty() && !name.empty();
}

Video::Video(sqlite3_stmt *row): id(get_string(row, 0)), title(get_string(row, 2)), description(get_string(row, 3)),
    flags(sqlite3_column_int(row, 4)), published(get_string(row, 5)), tui_title_width(0)
{
}

void Video::set_flag(sqlite3 *db, VideoFlag flag, bool value)
{
    sqlite3_stmt *query;
    if(value){
        SC(sqlite3_prepare_v2(db, "UPDATE videos SET flags = flags | ?1 WHERE videoID = ?2;", -1, &query, nullptr));
        flags |= flag;
    } else {
        SC(sqlite3_prepare_v2(db, "UPDATE videos SET flags = flags & ~?1 WHERE videoID = ?2;", -1, &query, nullptr));
        flags &= ~flag;
    }
    SC(sqlite3_bind_int(query, 1, flag));
    SC(sqlite3_bind_text(query, 2, id.c_str(), -1, SQLITE_TRANSIENT));
    SC(sqlite3_step(query));
    SC(sqlite3_finalize(query));
}

std::vector<Video> Video::get_all_for_channel(const std::string &channel_id)
{
    std::vector<Video> videos;

    sqlite3_stmt *query;
    SC(sqlite3_prepare_v2(db, "SELECT * FROM videos WHERE channelId=?1 ORDER BY published DESC;", -1, &query, nullptr));
    SC(sqlite3_bind_text(query, 1, channel_id.c_str(), -1, SQLITE_TRANSIENT));

    while(sqlite3_step(query) == SQLITE_ROW) {
        videos.emplace_back(query);
        Video video(query);
    }
    SC(sqlite3_finalize(query));

    return videos;
}

std::vector<Video> Video::get_all_with_flag_value(const VideoFlag flag, const int value)
{
    std::vector<Video> videos;

    sqlite3_stmt *query;
    SC(sqlite3_prepare_v2(db, "SELECT * FROM videos WHERE flags & ?1 = ?2 ORDER BY published DESC;", -1, &query, nullptr));
    SC(sqlite3_bind_int(query, 1, flag));
    SC(sqlite3_bind_int(query, 2, value));

    while(sqlite3_step(query) == SQLITE_ROW) {
        videos.emplace_back(query);
        Video video(query);
    }
    SC(sqlite3_finalize(query));

    return videos;
}
