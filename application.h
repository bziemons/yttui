#pragma once

#include <functional>
#include <string>

struct application_host {
    std::function<bool()> quit = nullptr;
    std::function<void(const std::string &channel, const std::string &title)> notify_channel_single_video = nullptr;
    std::function<void(const std::string &channel, const int count)> notify_channel_multiple_videos = nullptr;
    std::function<void(const int channels, const int count)> notify_channels_multiple_videos = nullptr;
};

void run_standalone();
void run_embedded(int pty_fd, application_host *host);
