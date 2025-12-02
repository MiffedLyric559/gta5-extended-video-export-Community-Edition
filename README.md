# EVER - Extended Video Export Revived

[![License](https://img.shields.io/badge/license-Apache%202.0-blue.svg)](LICENSE)
[![Contributions welcome](https://img.shields.io/badge/contributions-welcome-brightgreen.svg)](CONTRIBUTING.md)
[![Build status](https://img.shields.io/badge/status-active-success.svg)](#)

> A community-maintained enhancement tool for Grand Theft Auto V's Rockstar Editor, enabling high-quality video exports with advanced rendering controls.

---

## Important Notice

**Development Status:** Active development is currently underway. The project is in a very early stage of development and there are know issues that will be addressed. (see [Known issues](#known-issues))

---

## Table of Contents

- [Overview](#overview)
- [Project Goals](#project-goals)
- [Features](#features)
- [Installation](#installation)
- [Usage](#usage)
- [Known issues](#known-issues)
- [Building from Source](#building-from-source)
- [Contributing](#contributing)
- [Community](#community)
- [License & Attribution](#license--attribution)
- [Acknowledgments](#acknowledgments)

---

## Overview

**EVER (Extended Video Export Revived)** is an actively maintained fork of the original [EVE mod by Ali Alidoust](https://github.com/ali-alidoust/gta5-extended-video-export). This project continues the development and improvement of one of the most powerful cinematic export tools available for GTA V's Rockstar Editor.

**EVER** transforms the native Rockstar Editor export capabilities by providing:

- **Higher quality video output** with customizable encoding settings
- **Advanced rendering controls** for professional-grade cinematic production
- **Audio and video synchronization** for high frame rate videos
- **Extended format support** and export customization options

This project ensures the tool remains compatible with modern game updates while introducing new features and improvements driven by the modding community.

---

## Project Goals

The project focuses on the following core objectives:

- **Compatibility:** Maintain full compatibility with the latest GTA V and FiveM builds
- **Stability:** Refactor and optimize the codebase for improved reliability and performance
- **Extensibility:** Expand customization options for video export and rendering pipelines
- **Accessibility:** Simplify installation, configuration, and usage for all skill levels
- **Collaboration:** Foster an open development environment welcoming contributions from the community

---

## Features

- High-quality video export from Rockstar Editor clips
- Customizable encoding parameters and output formats
- Support for high frame rates and resolution exports
- Motion blur and visual enhancement options
- Reshade integration for advanced post-processing effects
- Watermark removal for cleaner exports
- Audio and video synchronization improvements
- Optimized for both GTA V and FiveM

---

## Installation

Installing **EVER** is just as easy as before:

**Required:** You need to have Voukoder installed in order for the plugin to work, you can grab the installation file from: [here](https://web.archive.org/web/20241216004346/https://github.com/Vouk/voukoder/releases/tag/13.4.1).

#### For GTA V:

1. Download the latest release from the [releases page](https://github.com/MiffedLyric559/gta5-extended-video-export-Community-Edition/releases)
2. Install [ScriptHookV](http://www.dev-c.com/gtav/scripthookv/)
3. Install [Reshade](https://reshade.me/)
4. Unzip the latest release and place the contents in your GTA V installation folder
5. Start GTA V and either edit the `ExtendedVideoExport.ini` file located in the `EVE` folder in GTA V or press the `Home` button and navigate to `addons` to adjust settings.

#### For FiveM:

1. Download the latest release from the [releases page](https://github.com/MiffedLyric559/gta5-extended-video-export-Community-Edition/releases)
2. Install [Reshade](https://reshade.me/)
3. Press: `Windows + R` and type: `%localappdata%\FiveM` and press `Enter`.
4. Navigate into the `FiveM Application` and go to the `plugins` folder.
5. Unzip the latest release and place the contents into the `plugins` folder.
6. Start FiveM and either edit the `ExtendedVideoExport.ini` file located in the `EVE` folder in FiveM or press the `Home` button and navigate to `addons` to adjust settings.

---

## Usage

Using **EVER** is very simple and similar to the original EVE mod.
Inside the `.ini` file or in the `Home` menu of Reshade you can adjust various settings related to EVER.

- `enable_mod`: Enable or disable EVER.
- `auto_reload_config`: Automatically reloads the `ExtendedVideoExport.ini` whenever a new export is started.
- `output_folder`: The folder where the exported videos will be saved (default location is in the Videos folder).
- `log_level`: The level of logging that should be displayed in the console.
- `fps`: The frame rate of the exported video.
- `motion_blur_samples`: How many samples the plugin should take to apply the motion blur effect.
- `motion_blur_strength`: The strength of the motion blur effect.
- `export_openexr`: Enable or disable the export of the video in the OpenEXR format.
- `disable_watermark`: Enable or disable the Rockstar watermark.

**Note**: if you have the FPS & motion blur sampling set to values that exceeds 60, the EVER plugin will perform 2 renedr passes in order to save the audio to the video file.
During the first pass, only the audio will be captured and will be stored in memory.
Once you reach the "Export complete" screen, do not touch anything and instead just wait while the Rockstar Editor does some cleanup in the background.
Once the Rockstar Editor is done, the second pass will automatically start and you will then notice that the rendering will take much longer time, in this pass only the video will be captured with your settings and once the second pass is complete the final video will be saved in the output folder.

---

## Known issues

- There is no clear indication that 2 rendering passes will be done if the FPS & motion blur sampling is set to values that exceeds 60.
- When a export is fully complete and the user tries to load another project, the game will crash.
- FPS counters and other overlays will be included in the exported video.
- No option to separate the audio from the video.
- Native UI and game frames will flicker in the second pass but the finished video will be fine.
- The game might crash if trying to export very long videos at the second pass completion.

---

## Building from Source

**Prerequisites:**

- Visual Studio 2019 or newer with C++ development tools
- CMake 3.15 or higher
- vcpkg (included as submodule)

**Build Steps:**

1. Clone the repository with submodules:

   ```bash
   git clone --recursive https://github.com/MiffedLyric559/gta5-extended-video-export-Community-Edition.git
   cd gta5-extended-video-export-Community-Edition
   ```

2. Initialize and update submodules (if not cloned recursively):

   ```bash
   git submodule update --init --recursive
   ```

3. Download the ScriptHookV SDK from [here](http://www.dev-c.com/gtav/scripthookv/) and unzip into: `ScriptHookV` inside the main repository folder.

4. Download the Voukoder from [here](https://web.archive.org/web/20241216004346/https://github.com/Vouk/voukoder) (Wayback Machine) and unzip the archive and copy the contents of the `Voukoder` into a folder named: `voukoder` inside the main repository folder.

5. Run the build script:
   ```bash
   build.bat
   ```

**Note:** The build process uses vcpkg to manage dependencies. The first build may take considerable time as dependencies are downloaded and compiled.

---

## Contributing

We welcome contributions from developers, testers, and content creators. Whether you want to fix bugs, add features, improve documentation, or provide feedback, your input helps shape the future of this project.

**How to Contribute:**

1. Fork the repository
2. Create a feature branch (`git checkout -b feature/your-feature-name`)
3. Make your changes and commit (`git commit -am 'Add new feature'`)
4. Push to your branch (`git push origin feature/your-feature-name`)
5. Open a Pull Request

For detailed contribution guidelines, see [CONTRIBUTING.md](CONTRIBUTING.md) (coming soon).

---

## Community

**EVER** is built by creators, for creators. We encourage you to:

- Share your exported videos and projects using **EVER**
- Report bugs and suggest features through GitHub Issues
- Join discussions about improvements and best practices
- Help other users in the community

Your feedback and engagement directly influence the development roadmap and help us prioritize features that matter most to the GTA V cinematic community.

---

## License & Attribution

This project is based on the original EVE mod developed by [Ali Alidoust](https://github.com/ali-alidoust), released under the **Apache License 2.0**.

All modifications, enhancements, and additions made in this **EVER** project are © 2025 [MiffedLyric](https://github.com/MiffedLyric559), [TheGreyRaven](https://github.com/TheGreyRaven), and contributors, and are released under the same Apache License 2.0.

For complete license terms, see the [LICENSE](LICENSE) file.

---

## Acknowledgments

- **Ali Alidoust** - Original EVE mod creator and developer
- **GTA V Modding Community** - Ongoing support, testing, and contributions
- **Rockstar Editor Creators** - The talented cinematographers and content creators who inspire this project
- **MiffedLyric & Miffed Films** - Intiating project revival and continued development leadership
- **TheGreyRaven** - Project revival and continued development leadership
- **All Contributors** - Everyone who has contributed code, testing, documentation, and feedback

---
