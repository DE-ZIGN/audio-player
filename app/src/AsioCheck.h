#pragma once

#include <ap/engine/output/asio_probe.h>

#include <string>

namespace ap::app {

// Some ASIO drivers crash the process that loads them (task 1-4), so a driver
// is first loaded in a child process: this program started with
// "--asio-probe-one {CLSID}" (see runAsioProbeChild). A driver that passed
// is remembered for the rest of the run and not checked again.
//
// Blocks for as long as the child takes (up to about 10 s): call it off the
// message thread.
ap::engine::AsioProbeResult checkAsioDriver(const ap::engine::AsioDriverInfo& driver);

// The child process's side: loads the driver with this CLSID, writes one
// result line to the standard output and returns the exit code.
int runAsioProbeChild(const std::string& clsid);

// Japanese, for the user, when the check did not pass.
std::string asioProblemText(const ap::engine::AsioProbeResult& result);

} // namespace ap::app
