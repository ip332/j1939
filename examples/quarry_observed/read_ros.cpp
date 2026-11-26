// Link the explicitly supplied legacy RosStream, with this checkout's stream/frame base.
#include "j1939_db.h"
#include "ros_stream.h"
#include <iomanip>
#include <iostream>
int main(int argc, char **argv) {
  if (argc != 2)
    return 2;
  RosStream stream;
  if (!stream.open(argv[1]))
    return 1;
  J1939_frame f;
  unsigned count = 0;
  while (stream.dataAvailable()) {
    f.reset();
    if (!stream.get(&f))
      continue;
    uint32_t pgn;
    uint8_t sa, da, pri;
    j1939_id_unpack(f.canID(), f.extended_, &pgn, &sa, &da, &pri);
    if (pgn != f.pgn_)
      return 3;
    std::cout << count++ << ' ' << f.time_ns_ << ' ' << f.canID() << ' ' << pgn
              << ' ' << unsigned(sa) << ' ' << f.dlc_ << ' ';
    for (unsigned i = 0; i < f.dlc_; ++i)
      std::cout << std::hex << std::setw(2) << std::setfill('0')
                << unsigned(f.buffer_[i]);
    std::cout << std::dec << '\n';
  }
}
