# ASUS Control Center

An open-source Linux control center for ASUS laptops, designed as a community-driven alternative to MyASUS.

## Overview

ASUS Control Center provides a graphical interface for viewing laptop hardware information and accessing supported system controls on Linux.

The project is being developed and tested on an ASUS Vivobook Pro 15 N6506CU running Fedora Linux.

## Features

- **Hardware information:** CPU, RAM, storage, graphics, display, and battery details.
- **Battery controls:** Supported charge limits and charging policies.
- **Performance profiles:** Quiet, Balanced, and Performance profiles where supported.
- **GPU mode:** ASUS GPU MUX switching where supported; changes may require a reboot.
- **Keyboard backlight:** Supported backlight levels.
- **Display tools:** Software dimming and night-light integration.
- **System monitoring:** Available temperature and fan-speed readings.
- **Polkit integration:** Privileged operations through a dedicated helper.
- **ASUS services:** Integration with `asusd` when available.

**Note:** Available features depend on the laptop model, firmware, kernel, installed services, and hardware support. Not every control has been tested on every device.

## Requirements

- Linux
- A supported GTK development/runtime environment
- A C/C++ build toolchain and `make`
- ASUS-specific services and interfaces for applicable controls
- Polkit for privileged operations

Exact package requirements may vary by distribution.

## Build

Clone the repository:

```bash
git clone https://github.com/Mostafamomen/asus-control-center.git
cd asus-control-center
```

Build the application:

```bash
make
```

Run the read-only hardware report:

```bash
./asus-control-center --self-test
```

Run the project's test target:

```bash
make test
```

## Installation

Review `install.sh` before running it. Installation may require administrator privileges and installs system-level components for supported controls.

```bash
./install.sh
```

Only install software you trust. Review the helper and Polkit policy before granting privileged access.

## Hardware Compatibility

Initial development and testing target:

- **Laptop:** ASUS Vivobook Pro 15 N6506CU
- **Operating system:** Fedora Linux
- **Desktop environment:** GTK-based Linux desktop

Other ASUS models may work, but compatibility has not been established for every device.

## Project Status

This project is under active development. Hardware detection and the read-only self-test have been tested on the development laptop. Individual hardware-writing operations require separate testing.

## Contributing

Bug reports, testing on other ASUS laptops, and code contributions are welcome. Please include your laptop model, Linux distribution, kernel version, and relevant logs when reporting an issue.

## License

See the `LICENSE` file in this repository.

---

**ASUS Control Center** — community-built ASUS laptop controls for Linux.
