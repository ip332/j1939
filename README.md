# J1939

[![Build](https://github.com/ip332/j1939/actions/workflows/build.yml/badge.svg)](https://github.com/ip332/j1939/actions/workflows/build.yml)
[![License: MIT](https://img.shields.io/badge/license-MIT-blue.svg)](LICENSE)

A C++14 library for encoding and decoding J1939/CAN frames, plus a DBC compiler. For embedded targets, `-c_path` produces a plain-C const database with no constructors, allocation, or runtime DBC parsing. The C++ PGN map and host tools use dynamic allocation. This project does not establish safety certification.

## Summary

The `J1939` repository provides a library for encoding and decoding CAN frames, along with tools to compile DBC files into a compact, generated data format.

## Background

There are two different scenarios where you might want to read a J1939 stream:
* **Message-centric**, when you know exactly which messages you need to read. This is how a typical ECU (engine, brakes, FLR, ACC, etc.) works: it knows how to handle a small subset of messages and ignores the rest.
* **Streaming**, when you want to read all messages, either to log them to a file or to share them with other components in the system.

Both scenarios could be addressed with the same implementation, but the message-centric case should be designed to meet the performance and memory constraints typical of automotive ECUs. In this case, the application code declares a variable for each message it cares about (e.g., TSC1, XBR) and ignores the rest of the CAN traffic. In streaming mode, however, declaring an individual variable for every message type would be impractical, so this mode needs a generic API backed by detailed metadata about all messages and signals.

Regardless of the use case, encoding and decoding should behave consistently across all modules and subsystems. One option is to parse a DBC file at runtime, as the roscan project does, but this approach may not be acceptable for safety-critical applications. Compiling the DBC at build time also allows more thorough validation.

This project therefore provides a DBC compiler that generates a **plain-C database**, **portable accessors**, a **C++ PGN map**, and a legacy **packed union** — from one or more input files.

## Content

* **J1939** — the library combining the DBC parser, runtime data types, and the J1939 encoding/decoding functionality. Built as a static library.
* **dbc_compiler** — the DBC compiler itself, which processes multiple input files and generates the output formats described below.
* **tests** — the unit test suite covering most of the library's components.
* **can_reader** — a collection of classes, built as a static library, that handle long (multi-frame) messages and reading/writing a physical CAN port via the SocketCAN API.
* **options** — a lightweight command-line parser. May be replaced with **gflags** in the future (TBD).
* **console** — a simple console application that reads a log file and prints its content in a human-readable format, demonstrating how the parser can be used.

### Building and running

Dependencies:
* cmake, a C++14 compiler
* gtest — ```apt install libgtest-dev```

The following commands build the code and place three executables (*all_tests*, *can_sandbox*, and *dbc_compiler*) into the **bin** folder. Building inside the `build` directory keeps the repository root free of CMake artifacts:
```bash
mkdir build
cd build
cmake ..
make all
```
#### Running unit tests

This is the simplest part:
```bash
VirtualBox:~/src/J1939$ bin/all_tests 
Running main() from /build/googletest-j5yxiC/googletest-1.10.0/googletest/src/gtest_main.cc
[==========] Running 20 tests from 5 test suites.
[----------] Global test environment set-up.
[----------] 5 tests from DbcParserTest
[ RUN      ] DbcParserTest.SimpleSignalFormatTest
[       OK ] DbcParserTest.SimpleSignalFormatTest (0 ms)
[ RUN      ] DbcParserTest.SignalWithValueFormatTest
       * * *
[----------] Global test environment tear-down
[==========] 20 tests from 5 test suites ran. (3 ms total)
[  PASSED  ] 20 tests.
VirtualBox:~/src/J1939$
```
`SocketCanStream` is tested separately, against a real (virtual) SocketCAN interface,
since that needs `CAP_NET_ADMIN` and the `vcan` kernel module rather than just a plain
build. Using the Docker image (see below):
```bash
sudo modprobe vcan
docker build -t j1939 .
docker run --rm --cap-add=NET_ADMIN j1939 cmake --build build --target vcan_tests
```
Some tests replay real NMEA 2000 captures from [canboat](https://github.com/canboat/canboat).
Short excerpts are committed under `testdata/canboat/`, and the tests that use them always run.
The full captures (~216 MB) are not committed. Fetch them with
`scripts/fetch_test_logs.sh`; until you do, those tests skip. See
[testdata/README.md](testdata/README.md).
#### Using can_sandbox

This tool decodes CAN traffic log files in several text formats (ASC, LOG, TRC, OUT, ROS, and Actisense `.raw`/`.all` as written by canboat's actisense-serial) and prints the content to the console. A sample log file is provided.
```bash
VirtualBox:~/src/J1939$ bin/can_sandbox
Usage: can_sandbox <options>
   where <options> could be:
    -in <arg>		Defines the input device (can0) or a log file (asc|log|out|trc|txt|raw|all) to read the data from.
    -out <arg>		Defines the CAN interface to play back the log file into or a file name to record the stream into (.log)
    -dbc <arg>		Defines a DBC file which should be used to parse CAN frames.
    -ext		Treat input messages as extended CAN frames (by default the extended bit from the CAN ID is used)
    -all		Show unset fields, i.e. filled with all 1
    -quiet		Do not show the stream content.
    -nodes		Track address claims: print node events, the NAME behind each frame, and the final SA <-> NAME table.
    -nodes-report <arg>	Write a JSON summary of the address-claim table to this file.
    -contest-window <arg>	Milliseconds a contested address stays undecided (default 500).
    -node-timeout <arg>	Seconds without traffic before a node times out (default 0 = never).
    -request-claims	Active mode: send a Request for Address Claimed on the input CAN interface at startup (never on a file).

```
##### Address claims (J1939-81 / ISO 11783-5 / NMEA 2000)
`-nodes` replays or listens passively and keeps a table of which NAME (PGN 60928) is behind each
source address, following contests, Cannot Claim and Commanded Address as the devices resolve
them. Each frame gets a `NAME:`, `CONTESTED:` or `UNRESOLVED` tag, and events such as
`NodeAddressChanged`, `AddressReassigned` and `ContestResolved branch=HolderDefended` are printed
as they happen. See [can_reader/node_table.h](can_reader/node_table.h) for the rules and events.
```bash
bin/can_sandbox -in testdata/canboat/nac3_reset_reclaim.raw -quiet -nodes
```
##### Decoding a CAN stream from a log file or a physical CAN port (SocketCAN interface)
```bash
virtualBox:~/src/J1939$ bin/can_sandbox -in candump.out -dbc tests/dbc/j1939.dbc
         0.000000 EEC1 CAN ID:CF00400(217056256) Data(8): FFFFFF0C1CFFF47D { EngSpeed: 897 EngStarterMode: 4 EngDemandPercentTorque: 0 }
         0.001000 UNDEFINED CAN ID:CFF6600(218064384) Data(8): 8103FF70000000FF (...p....)
         0.002000 DM01 CAN ID:18FECA00(419351040) Data(8): 73FF38F0FF01FFFF { AmberWarningLampStatus: 0 MalfunctionIndicatorLampStatus: 1 DTC1: 33550392 }
         0.003000 UNDEFINED CAN ID:C09FF21(201981729) Data(8): 73FFFFFFFFFFFFFF (s.......)
        * * *
        23.613000 EBC1 CAN ID:18F0010B(418382091) Data(8): CFFFFFFFFFCD0BFF { AntiLockBrakingActive: 0 ABSFullyOperational: 1 ABS_EBSAmberWarningSignal: 0 SrcAddrssOfCntrllngDvcFrBrkeCtrl: 11 }
        23.614000 EBC2 CAN ID:18FEBF0B(419348235) Data(8): 0000FFFF7DFFFFFF { FrontAxleSpeed: 0 RelativeSpeedRearAxle1LeftWheel: 0 }
        23.615000 EBC1 CAN ID:18F00117(418382103) Data(8): FFFF3CFFFFFFFFFF { ABSOffroadSwitch: 0 TractionCtrlOverrideSwitch: 0 }
        23.616000 EEC1 CAN ID:CF00400(217056256) Data(8): FFFFFF0000FFF07D { EngSpeed: 0 EngStarterMode: 0 EngDemandPercentTorque: 0 }
Average bus load: 8000.47 bytes/sec; duration: 23.615 sec.
                  1000.08 msg/sec
Average bus load: 8000.47 bytes/sec; duration: 23.615 sec.
                  1000.08 msg/sec
```

##### Plain-C database (embedded lookup)

```bash
bin/dbc_compiler -in tests/dbc/j1939.dbc -c_path generated_db.c
```

This writes `generated_db.c` and `generated_db.h`. Compile the generated C file
and `j1939_parser/j1939_db.c` with the target's C11 compiler and include the generated
header. All database storage is const arrays and a string blob, with a
`const j1939_db_t` entry point declared in the generated header. Loading the tables
runs no constructors and calls no allocator. Use `-no-strings` to omit names.

`j1939_db_find` looks up a CAN ID/PGN; `j1939_signal_by_spn` searches the sorted
SPN index across the database. `BA_ "SPN"` attributes supply SPN numbers (0 when
absent). `j1939_signal_by_name` searches one message, and
`j1939_messages_by_name` returns all matching messages. Multiplex alternatives
are retained, including those sharing a start bit; use `j1939_signal_active`
before decoding one. Extended multiplex ranges (`SG_MUL_VAL_`) and nested
switch selection are not implemented.

##### Portable message accessors

```bash
bin/dbc_compiler -in input.dbc -accessor_path payloads.h
```

This optional standalone C11/C++14 header emits explicit byte shifts and masks
for each signal, including mixed Intel/Motorola messages and multiplex alternatives.
There is no aggregate message union or runtime library dependency. Names use
`j1939_<message>_<decimal CAN ID>_<signal>` followed by `_raw`, `_store_raw`,
or `_phys`. Raw stores preserve all other payload bits. `_phys` applies signed
interpretation, scale and offset; stores take raw wire values. Byte-aligned Intel
signals wider than 64 bits get `_get_bytes` and `_set_bytes` instead.
`VAL_` labels become prefixed `UINT64_C` defines; punctuation becomes underscores,
and colliding labels receive a numeric suffix.

By default all accessors are available. To include only selected messages, define
`J1939_ACCESSORS_SELECT` and `J1939_ACCESSORS_<message>_<decimal CAN ID>` before
including the header. Provide a payload at least as large as the message DLC.
Accessors read or write the named bits directly; the caller selects the active
multiplex alternative and sets the switch separately.

##### PGN format (C++ host tools)
This format produces a single file where **all input DBC files** are compiled into one `std::map`:
```c++
std::map<uint32_t, PGN> j1939_dbc = {
        . . .
};
```
The map, strings, vectors, and enum maps allocate during initialization. Use the
plain-C database for allocation-free embedded lookup.

The `J1939` class uses this map to decode/encode arbitrary CAN frames (see `can_sandbox.cpp` for details).
Note: by default, the name of the first input DBC file is used to build the map name (e.g., `j1939.dbc` → `j1939_dbc`). This can be overridden with the `-name` command-line option.

##### Legacy union format
This legacy format packs a byte buffer and a bitfield struct into an unnamed union, which is saved to a header file.
*Note: a single header file is generated for all listed input DBC files.*
```c++
#define CTLEGO_MESSAGE_ID	0x8c210253
#define CTLEGO_MESSAGE_DLC	6

struct ctlego_message_t {
	union {
	    uint8_t data[CTLEGO_MESSAGE_DLC];
	    struct {
		uint64_t vehicle_speed : 16;
		uint64_t control_engine : 1;
		uint64_t yaw_rate : 16;
		uint64_t trans_current_gear : 8;
	    } fields __attribute__((packed));
	};
#ifdef __cplusplus

	double vehicle_speed() const { return fields.vehicle_speed * 0.001 + 0; }
	void vehicle_speed(double value) { fields.vehicle_speed = (value - 0) / 0.001; }

	uint8_t control_engine() const { return fields.control_engine; }
	void control_engine(uint8_t value) { fields.control_engine = value; }

	double yaw_rate() const { return fields.yaw_rate * 0.00012207 + -3.92; }
	void yaw_rate(double value) { fields.yaw_rate = (value - -3.92) / 0.00012207; }

	double trans_current_gear() const { return fields.trans_current_gear * 1 + -125; }
	void trans_current_gear(double value) { fields.trans_current_gear = (value - -125) / 1; }

	void fillFrameWith(uint8_t value = 0xFF) { memset(data, value, sizeof(data)); }
#endif

};
```
This header can be used from C code if necessary, though C++ also gets the convenience of getters/setters and the `fillFrameWith()` method.

The DBC compiler also generates a second structure that places all messages into a single union to simplify processing:
```c++
struct all_xyz_messages_t {
    const uint16_t max_dlc_ = 8;
    union {
	// Use this buffer to read the data from the CAN stream into. Mind the buffer size.
	uint8_t buffer_[max_dlc_]; 
	// Once you place the CAN frame into the buffer_[], use CAN ID to see if this is the
	// message you need and select the corresponding field below.
	ctlwdbrn_message_t ctlwdbrn_;
	ccvs2_message_t ccvs2_;
	ctlego_message_t ctlego_;
    . . .
    };
};
```
*Note: the name of that union is taken from the first input DBC file's name (e.g., `xyz_messages.dbc` → `all_xyz_messages_t`).*

*Portability:* `fields` is a compiler-specific bitfield layout. C and C++ leave the allocation order of bitfields, the storage unit they pack into, and whether a bitfield may cross that unit up to the implementation. This output uses `uint64_t` and `int64_t` bitfields, anonymous padding whose widths follow the DBC start bits, and `__attribute__((packed))` (GCC and Clang). Motorola (`@0`) signals are emitted by walking start bits from the end of the payload. A message that mixes `@0` and `@1` is rejected. The `data[]` / `fields` overlay, and the `all_*` union of every message, follows that compiler's bitfield rules. The portable lookup table is the plain-C database from `dbc_compiler -c_path`: const structs, with the bytes read and written by `j1939_db.c`.

## License
This project is licensed under the MIT License -- see [LICENSE](LICENSE) for details.
