# Battery Qualification Bench Firmware
Battery qualification is a crucial step when building a CubeSat. The team must purchase a batch of cells and evaluate them to ensure they pass the NR-SRD-139 requirement.

The firmware is agnostic to the actual sequence of tests and only responds to commands from the [GUI](https://github.com/scsd-cdh/battery_test_gui). It implements the [Battery Cell Bench Protocol](docs/PROTOCOL.md) so that the GUI can properly control it and request data.

## Peripherals

