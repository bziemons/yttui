yttui
===

This is a TUI tool to monitor YouTube channels for new videos and keep track of which ones you've watched already.
This tool is work in progress and some functions might not work (correctly).

### Building
#### Requirements:
* Linux (other platforms are untested and likely require additional work)
* A modern, C++17 capable compiler
* meson
* ninja
* pkg-config
* Installed (it may also be required to install separate development packages) and accesible via pkg-config:
    * [libcurl](https://curl.se)
    * [nlohmann-json](https://github.com/nlohmann/json) (at least version 3.5.0)
    * [sqlite3](https://sqlite.org)
    * [termpaint](https://github.com/termpaint/termpaint)

#### How to build
1. Create a build folder.
1. Configure the project by running e.g. `meson setup /path/to/build/dir` in the source directory. See `meson setup --help` for available configuration options.
1. Build the application with `meson compile -C /path/to/build/dir`.
1. You can now start the application by running `/path/to/build/dir/yttui` (but have a look at the configuration options first).
1. Optionally you can install the application by running `meson install -C /path/to/build/dir`.


### Getting started
1. Build (and optionally install) the application
1. Get a YouTube API key.
1. Create a configuration file in JSON format.
    - You can either put it next to the application binary or in `$HOME/.config/yttui.conf`
    - Have a look at `yttui.conf.example`. It contains all possible configuration options and is a good place to start.
    - Configuration default values are described in "Configuration options".
1. Start the application. You can press `F1` at any time to get help and `C-q` (holding down the control key and pressing q) to quit.


### Configuration options
|Option | Description | Default value | Required |
|-------|-------------|---------------|--------- |
| apiKey | YouTube API Key | | ✓ |
| extraHeaders | Extra HTTP headers to send to YouTube. This is a JSON array of objects containing `"key"` and `"value"`. Will be sent with each API requres. | `[]`  | ✘  |
| database | Path of channel/video database | $HOME/.local/share/yttui.db | ✘ |
| watchCommand | Command executed to watch a video. `{{vid}}` will be replaced by the Id of the video to watch. | `["xdg-open", "https://youtube.com/watch?v={{vid}}"]` | ✘ |
| notifications | Object describing notification settings | `{}` | ✘ |
| autoRefreshInterval | Automatically refresh all channels every X seconds (and after 30 seconds of inactivity). -1 to disable. | -1 | ✘ |

#### Notifcation options
The `notifications` entry can have the following sub-options:

|Option | Description | Default value | Required |
|-------|-------------|---------------|--------- |
| channelNewVideoCommand | Gets executed when refreshing a single channel and there is one new videos. `{{channelName}}` will be replaced with the name of updated channel, `{{title}}` with the title of the new video. | `[]` | ✘ |
| channelNewVideosCommand | Gets executed when refreshing a single channel and there are multiple new videos. `{{channelName}}` will be replaced with the name of updated channel, `{{newVideos}}` with the number of new videos. | `[]` | ✘ |
| channelsNewVideosCommand | Gets executed when refreshing multiple channels and there are new videos. `{{updatedChannels}}` will be replaced with the number of updated channels, `{{newVideos}}` with the number of new videos across all refreshed channels. | `[]` | ✘ |
