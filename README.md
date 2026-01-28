# Wyoming C Satellite

Wyoming C Satellite is library which implements remote voice satellite
using the [Wyoming protocol](https://github.com/rhasspy/wyoming), written in C.

It's mainly designed for embedded systems, but can be used on full-fledged computers as well.

# Building

Since every embedded SDK uses different build system (CMake, Make, etc...), there is no prefered
and integrated build system. Just add .c files you need from `lib` folder and add `include` to includes.

Additionally, we need to also add required third-party libraries, which are listed below.

Library will also look for `wyoming_user.h` file. This file not only contains compile-time settings through macros,
but it's also used to include all libraries headers, which are used by satellite itself, as on every platform,
the location of libraries headers can vary.

Take look on `example` project which does all of this in CMake.

## Third-party libraries

- cJSON - Wyoming protocol is using JSONs heavily, so we need library which can both parse and serialize JSONs.
For this cJSON was chosen, as it's written in C, supports both parsing and serialize, 
and allows hooking allocation calls. cJSON is very popular library, so many SDKs are bundled with it.
If not, just add it to the project according to the cJSON build instructions.
([this](https://github.com/gamelaster/wyoming-test-tools/blob/main/wyoming_satellite_poc/CMakeLists.txt#L8) might be useful)
- POSIX Sockets library - we need a way to host TCP server, for this, we need POSIX compatible library.
On most of embedded systems, LwIP is used, which offers POSIX compatibility.
- Dynamic Allocation functions - Even this is targeted to embedded systems, there is still need for dynamic memory
allocation for receiving audio samples. It is recommended to have separate pool of memory for this. Additionally, cJSON
uses malloc/free as well (can be overridden).
