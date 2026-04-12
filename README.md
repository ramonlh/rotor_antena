# rotor_antena

ESP32-based two-axis antenna rotor controller with integrated web interface and satellite-tracking features.

## Overview

`rotor_antena` is a modular project for controlling an antenna rotor in **azimuth** and **elevation** using an ESP32-compatible platform.

The system provides:

- manual rotor control from a web browser
- absolute azimuth/elevation positioning
- satellite tracking support
- simulation mode
- satellite favorites and visibility tools
- OTA and FTP support
- modular configuration files for pins, network, services, and motion parameters

The codebase is organized into separate functional modules to make maintenance, debugging, and future expansion easier.

---

## Main Features

- **Two-axis rotor control**
  - azimuth
  - elevation

- **Built-in web interface**
  - current antenna position display
  - manual jog movement
  - absolute target positioning
  - return to origin
  - satellite selection
  - follow / unfollow satellite
  - configuration page
  - favorites management

- **Satellite tools**
  - load and manage favorite satellites
  - search satellites
  - show satellites visible now
  - future pass visibility support
  - radio information per satellite

- **Operating modes**
  - real mode
  - simulation mode

- **Network services**
  - HTTP web server
  - OTA update support
  - FTP support

- **Modular architecture**
  - motion control
  - web UI
  - state management
  - network services
  - storage
  - satellite logic

---

## Repository Structure

```text
rotor_antena/
├── rotor_antena.ino           # Main sketch
├── app_state.*                # Global application state
├── imu_sensor.*               # IMU reading and processing
├── rotor_io.*                 # Low-level rotor I/O
├── rotor_axes.*               # Per-axis motion control
├── rotor_positioning.*        # Positioning and coordinated orientation
├── web_pages.*                # HTML generation
├── web_handlers.*             # HTTP handlers
├── web_routes.*               # Route registration
├── web_ui.*                   # Web UI setup
├── network_services.*         # WiFi, OTA, HTTP and related services
├── storage_services.*         # Storage and persistence helpers
├── sat_catalog.*              # Satellite catalog
├── sat_tracking.*             # Tracking logic
├── sat_fetch.*                # Data retrieval
├── sat_favorites.*            # Favorite satellites
├── sat_search.*               # Search
├── sat_visibility.*           # Future visibility
├── sat_visible_now.*          # Currently visible satellites
├── sat_radio_info.*           # Satellite radio info
├── satellite_tracker.*        # Additional tracking logic
├── state_*.*
├── config.h
├── config_core.h
├── config_pins.h
├── config_network.h
├── config_services.h
├── config_motion.h
├── config_secrets_template.h
└── LICENSE
```

---

## Requirements

### Hardware

This project is intended for an **ESP32 / Arduino-compatible platform** driving a two-axis antenna rotor.

Typical hardware includes:

- azimuth motor drive
- elevation motor drive
- orientation/position sensor
- WiFi-capable controller
- motor driver electronics
- mechanical rotor structure

### Software

- Arduino IDE or another ESP32-compatible build environment
- ESP32 board support package
- libraries used by the project may include:
  - `ArduinoOTA`
  - `WebServer`
  - `HTTPClient`
  - `Arduino_JSON`
  - `Wire`
  - `UnixTime`
  - `JY901_Serial`

Check that the library versions you use are compatible with your ESP32 core and IMU hardware.

---

## Configuration

Project configuration is split into dedicated headers:

- `config_core.h`
- `config_pins.h`
- `config_network.h`
- `config_services.h`
- `config_motion.h`

A template for private configuration is included:

- `config_secrets_template.h`

### Recommended setup

1. Copy `config_secrets_template.h` to a private file such as `config_secrets.h`
2. Fill in your WiFi credentials and any sensitive settings
3. Include that file from the appropriate configuration headers
4. Add the private file to `.gitignore`

---

## Build and Flash

1. Open `rotor_antena.ino` in Arduino IDE
2. Select the correct ESP32 board
3. Review memory and upload options
4. Adjust the `config_*.h` files to match your hardware
5. Compile and upload the firmware

Before building, verify at least:

- motor control pins
- network parameters
- motion limits and thresholds
- IMU configuration
- OTA / FTP settings

---

## Startup Sequence

On boot, the firmware initializes:

- serial console
- IMU
- rotor I/O pins
- WiFi
- web interface
- HTTP server
- OTA
- initial tracking data
- storage
- satellite favorites
- FTP server

During the main loop, the firmware services:

- OTA handling
- web server
- FTP server
- elevation movement
- azimuth movement
- coordinated orientation
- simulation updates

---

## Web Interface

The web UI is the main operating interface.

### Available actions

- open the main status page
- select a satellite
- enable or disable satellite following
- return to origin
- send absolute azimuth/elevation targets
- jog the rotor manually:
  - up
  - down
  - left
  - right
- refresh tracking data
- switch between real and simulation mode
- open the configuration page
- manage favorites
- search satellites
- display satellites visible now

---

## Current Status

The project already has a clearly modular structure, with separate areas for:

- motion control
- web interface
- satellite functions
- network services
- storage
- shared runtime state

That makes the project easier to document, test, and extend.

---

## Suggested Future Improvements

- full hardware documentation
- wiring diagram
- bill of materials
- screenshots of the web interface
- quick-start guide
- calibration procedure
- azimuth/elevation adjustment procedure
- troubleshooting section
- installation notes for a specific ESP32 board and IMU model

---

## License

This project is released under the **GPL-3.0** license.

See the `LICENSE` file for details.

---

## Author

**Ramón Lorenzo**

---

## Notes

This README is based on the current public repository structure and aims to document the overall design and purpose of the project.

It would be useful to expand it later with:

- exact hardware used
- IMU model and wiring details
- motor drivers and control electronics
- calibration instructions
- web interface screenshots
