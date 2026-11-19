//
// Header-only CanStream test double, backed by an in-memory frame queue instead of a
// file/socket. Lets code that depends on CanStream (CanSandbox, TransportProtocol
// consumers, etc.) be exercised deterministically in tests without real files or CAN
// hardware. Header-only so it never ends up compiled into the production can_streams
// library -- define_lib only globs *.cpp for compilation.
//

#ifndef J1939_MOCK_CAN_STREAM_H
#define J1939_MOCK_CAN_STREAM_H

#include "can_stream.h"

#include <deque>

class MockCanStream : public CanStream {
public:
    MockCanStream() { writable_ = true; }

    // --- Test setup ---
    // Queues a frame to be returned by a future get() call, in order.
    void enqueueFrame(const J1939_frame &frame) { to_read_.push_back(frame); }
    // Makes open() report this stream as a real (hardware-like) port, matching
    // SocketCanStream -- needed to exercise CanSandbox's reconnect-on-realPort logic.
    void setRealPort(bool value) { real_port = value; }
    // Directly controls dataAvailable()/ready(), independent of the read queue --
    // needed for output-only mocks, where readiness isn't about pending reads.
    void setReady(bool ready) { ready_ = ready; }
    // Makes the next open() call fail, then resets back to succeeding.
    void failNextOpen() { fail_next_open_ = true; }
    // Makes every put() call fail (simulating a broken/full output).
    void failPut(bool fail = true) { fail_put_ = fail; }
    // Makes preassembled() report true, like a gateway format (ActisenseStream) whose
    // frames are already complete messages.
    void setPreassembled(bool value) { preassembled_ = value; }

    // --- Test inspection ---
    const std::vector<J1939_frame> &written() const { return written_; }
    const std::vector<std::string> &openCalls() const { return open_calls_; }
    size_t rewindCount() const { return rewind_count_; }

    // --- CanStream overrides ---
    bool open(const std::string &input, bool /*write*/ = false) override {
        open_calls_.push_back(input);
        if (fail_next_open_) {
            fail_next_open_ = false;
            return false;
        }
        ready_ = true;
        return true;
    }

    void close() override { ready_ = false; }

    void rewind() override { rewind_count_++; }

    bool dataAvailable() const override { return ready_; }

    bool preassembled() const override { return preassembled_; }

    bool put(const J1939_frame &frame) const override {
        if (fail_put_) {
            return false;
        }
        written_.push_back(frame);
        return true;
    }

    bool get(J1939_frame *frame) override {
        if (to_read_.empty()) {
            return false;
        }
        *frame = to_read_.front();
        to_read_.pop_front();
        if (to_read_.empty()) {
            // Mirrors real file EOF timing: the same read that returns the last
            // frame is the one that makes dataAvailable() go false afterward.
            ready_ = false;
        }
        return true;
    }

private:
    std::deque<J1939_frame> to_read_;
    mutable std::vector<J1939_frame> written_; // put() is const in CanStream
    std::vector<std::string> open_calls_;
    bool fail_next_open_ = false;
    bool fail_put_ = false;
    bool preassembled_ = false;
    // Ready by default -- setInputStream()/setOutputStream() deliberately don't call
    // open() on the caller's behalf, so a freshly constructed mock needs to already be
    // usable. Tests that need to start not-ready (e.g. reconnect-logic tests) call
    // setReady(false) explicitly.
    bool ready_ = true;
    size_t rewind_count_ = 0;
};

#endif //J1939_MOCK_CAN_STREAM_H
