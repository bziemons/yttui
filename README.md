yttui
===

This is a TUI tool to monitor YouTube channels for new videos and keep track of which ones you've watched already.
This tool is work in progress and some functions might not work (correctly).

### Building
#### Requirements:
* Linux (other platforms are untested)
* A modern, C++17 capable compiler
* qmake
* make
* pkg-config
* Installed (with development packages is appropriate) and accesible via pkg-config:
    * [libcurl](https://curl.se)
    * [nlohman-json](https://github.com/nlohmann/json/) (at least version 3.5.0)
    * [sqlite3](https://sqlite.org)
    * [termpaint](https://termpaint.namepad.de/)

#### How to build
1. Create a build folder and open a terminal there.
1. Run qmake. E.g. `qmake ..`.
1. Run `make`.
1. You can now start the application by running `./yttui` (but have a look at the configuration options first).


### Getting started
1. Build the application
1. Get a YouTube API key.
1. Create a configuration file.
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
