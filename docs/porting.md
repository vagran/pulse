# Porting guide

Porting is performed by providing a set of target-specific functions required for the proper
operation of Pulse components. See [`pulse/port.h`](../src/include/pulse/port.h) for the complete
list of required interfaces. Each interface can be implemented either as a function or as a
preprocessor macro. The framework expects a `pulse_port.h` file to be available in the include
search path, where all required definitions are provided. The port must supply interrupt control
(enable/disable) and a mechanism for entering low-power sleep mode.

Several pre-implemented ports are also provided and can be selected by specifying the `PULSE_PORT`
CMake variable when using the supplied CMake configuration. See the [`ports`directory](../src/ports)
for the list of supported targets.
