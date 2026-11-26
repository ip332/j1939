// File-only validation shim. No CAN socket can be opened or transmitted on.
#include "socket_can_stream.h"
#include <stdexcept>
int RealCanSocketApi::socket() { throw std::runtime_error("sockets disabled"); }
unsigned RealCanSocketApi::ifNameToIndex(const std::string&) { return 0; }
int RealCanSocketApi::bind(int, unsigned) { return -1; }
ssize_t RealCanSocketApi::write(int, const canfd_frame&, size_t) { return -1; }
ssize_t RealCanSocketApi::read(int, canfd_frame*, size_t) { return -1; }
void RealCanSocketApi::close(int) {}
bool SocketCanStream::open(const std::string&, bool) { throw std::runtime_error("sockets disabled"); }
bool SocketCanStream::put(const J1939_frame&) const { throw std::runtime_error("sockets disabled"); }
bool SocketCanStream::get(J1939_frame*) { return false; }
