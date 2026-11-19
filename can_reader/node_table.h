//
// Passive J1939 / ISO 11783 / NMEA 2000 address-claim tracking: which NAME is behind each
// source address (SA), kept correct while addresses change mid-stream.
//

#ifndef J1939_NODE_TABLE_H
#define J1939_NODE_TABLE_H

#include "j1939_frame.h"
#include "j1939_name.h"

#include <functional>
#include <map>
#include <stdint.h>

// All times are frame timestamps (J1939_frame::time_ns_), never the wall clock, so replaying a
// log gives the same result every time. The table's clock is the highest timestamp seen so
// far; a timestamp that goes backwards never re-opens or closes a window.
struct NodeTableConfig {
    // How long a contested SA stays undecided (ISO 11783-5 lets a node 250 ms to defend).
    uint64_t contest_window_ns = 500000000ULL;
    // A node with no traffic or claims for this long gets NodeTimedOut. 0 = never.
    // Checked lazily, at most every stale_timeout_ns / 16 of frame time.
    uint64_t stale_timeout_ns = 0;
    // How long a Commanded Address waits for the target's claim of the new SA.
    uint64_t command_window_ns = 1250000000ULL;
    // ClaimPingPong: this many changes of claiming NAME on one SA...
    uint8_t pingpong_changes = 4;
    // ...within this span; then at most one ClaimPingPong per SA per pingpong_quiet_ns.
    uint64_t pingpong_span_ns = 2000000000ULL;
    uint64_t pingpong_quiet_ns = 5000000000ULL;
};

enum class NodeState {
    Active,      // owns sa_ (and duplicate_sa_, if flagged DuplicateName)
    Contending,  // claimed sa_ from a lower NAME; waiting for the holder to defend or not
    Displaced,   // lost its SA; expected to claim another or send Cannot Claim. Has no SA.
    CannotClaim, // sent Cannot Claim (claim from SA 254). Has no SA.
    Offline,     // held an SA, didn't defend it, so presumed gone. Record kept, no SA.
    TimedOut,    // no traffic for stale_timeout_ns. Keeps sa_ until someone else claims it.
};

// How a contest for an SA ended (ContestResolved::branch).
enum class Branch {
    None,
    LowerNameWins,      // challenger had the lower NAME and took the SA at once
    HolderDefended,     // challenger had the higher NAME; holder re-claimed within the window
    HolderSilent,       // challenger had the higher NAME; holder didn't defend: challenger wins
    ChallengerWithdrew, // challenger had the higher NAME and moved on / gave up by itself
};

enum class NodeEventType {
    NodeAppeared,       // first sighting of a NAME (sa = its SA, maybe 254), or a timed-out
                        // node whose SA carries traffic again
    NodeAddressChanged, // name moved old_sa -> sa (old_sa = 254 if it had none); commanded
    AddressReassigned,  // sa went from other_name to name
    CannotClaim,        // name sent Cannot Claim; old_sa = the SA it gave up, if any
    NodeTimedOut,       // name; sa = its SA (254 if none)
    AddressContested,   // name claimed sa while other_name held it; window opened
    ContestResolved,    // sa: winner name, loser other_name, how = branch
    NonCompliantNode,   // name lost sa but claimed it again after the window; other_name =
                        // the owner. (Only claims count: data frames on sa can't be told apart.)
    DuplicateName,      // name is active on both sa and old_sa
    ClaimPingPong,      // claiming NAME on sa keeps changing (name / other_name: last two)
    CommandedAddress,   // name commanded to sa by source_sa; old_sa = its SA at that time
    Attributed,         // re-attribution hook: frames on sa in [from_ns, to_ns] that were
                        // UNRESOLVED/CONTESTED belong to name
};

struct NodeEvent {
    NodeEvent(NodeEventType t, uint64_t time) : type(t), time_ns(time) {}
    NodeEventType type;
    uint64_t time_ns;
    uint8_t sa = kNullAddress;
    uint8_t old_sa = kNullAddress;
    uint8_t source_sa = kNullAddress;
    uint64_t name = 0;
    uint64_t other_name = 0;
    Branch branch = Branch::None;
    bool commanded = false;
    uint64_t from_ns = 0;
    uint64_t to_ns = 0;
};

struct Node {
    uint64_t name_;
    NodeState state_;
    uint8_t sa_;           // kNullAddress when it has none
    uint8_t duplicate_sa_; // second SA while flagged DuplicateName, else kNullAddress
    uint64_t first_seen_ns_;
    uint64_t last_seen_ns_;  // last claim or frame attributed to this node
    uint64_t last_claim_ns_;
    // Staleness runs from here. Refreshed by its claims and by traffic on an SA it owns;
    // set to the displacement time when Displaced (traffic on the old SA is the winner's).
    uint64_t liveness_ns_;
    // The SA it last lost, and until when re-claiming it is still part of that contest.
    uint8_t lost_sa_;
    uint64_t lost_deadline_ns_;
    uint8_t previous_sa_;    // last SA it actually owned (old_sa of NodeAddressChanged)
};

struct Resolution {
    enum Kind { Resolved, Unresolved, Contested } kind;
    uint64_t name;      // Resolved: the NAME. Contested: the provisional owner.
    uint64_t candidate; // Contested: the other NAME.
};

struct NodeTableStats {
    uint64_t claims = 0;
    uint64_t malformed_claims = 0;   // length != 8, or from SA 255
    uint64_t cannot_claims = 0;
    uint64_t claim_requests = 0;     // Request (59904) for Address Claimed seen
    uint64_t commanded = 0;
    uint64_t malformed_commanded = 0;
    uint64_t commands_expired = 0;   // target never claimed the commanded SA
    uint64_t contests = 0;
    uint64_t resolved[5] = {};       // indexed by Branch
    uint64_t reassignments = 0;
    uint64_t non_compliant = 0;
    uint64_t duplicates = 0;
    uint64_t pingpongs = 0;
    uint64_t timeouts = 0;
};

// Listens only; never transmits. Threading: single-threaded and not thread-safe, like the
// rest of the pipeline (CanSandbox, TransportProtocol); the event callback runs
// synchronously inside handleFrame()/flush(). Per-frame work does no allocation; memory is
// allocated only when a NAME is seen for the first time.
//
// TODO(suspected-non-compliant): optional, off-by-default heuristic -- keep per-NAME PGN
// signatures and emit a low-confidence SuspectedNonCompliant when an SA starts carrying PGNs
// that only the NAME displaced from it ever sent.
// TODO(own-address): claiming an address of our own is deliberately not implemented.
class NodeTable {
public:
    explicit NodeTable(NodeTableConfig config = NodeTableConfig());

    void setEventCallback(std::function<void(const NodeEvent &)> cb) { callback_ = std::move(cb); }

    // Feed every frame, after transport-protocol reassembly (Commanded Address arrives as
    // one 9-byte message).
    void handleFrame(const J1939_frame &frame);
    // Moves the clock to now_ns and closes whatever is due by then, e.g. at end of stream
    // flush(last_frame_time + contest_window_ns) treats the end as silence.
    void flush(uint64_t now_ns);

    // Who sent `frame`. Call after handleFrame(frame). An address claim resolves to the NAME
    // it carries.
    Resolution resolve(const J1939_frame &frame) const;

    const Node *bySa(uint8_t sa) const;
    const Node *byName(uint64_t name) const;
    // In NAME order.
    void forEach(const std::function<void(const Node &)> &cb) const;
    const NodeTableStats &stats() const { return stats_; }
    const NodeTableConfig &config() const { return config_; }

    // Active mode helper: Request for Address Claimed, from the null address (we don't claim
    // one) to global. Sending it is the caller's job.
    static J1939_frame makeAddressClaimRequest(uint64_t time_ns = 0);

private:
    struct Slot {
        Node *owner = nullptr;       // provisional owner while contested
        Node *rival = nullptr;       // the other party while contested
        bool contested = false;
        bool lower_wins = false;     // contest kind: rival is the displaced (higher) holder
        uint64_t deadline_ns = 0;
        // Span of frames on this SA not yet attributed (no owner, or contested).
        bool has_span = false;
        uint64_t span_from_ns = 0;
        uint64_t span_to_ns = 0;
        // DuplicateName: the NAME that last left this SA voluntarily, and when.
        Node *moved_away = nullptr;
        uint64_t moved_away_ns = 0;
        // ClaimPingPong.
        Node *last_claimer = nullptr;
        uint64_t changes_ns[16] = {};
        uint8_t change_count = 0;
        uint8_t change_head = 0;
        uint64_t last_pingpong_ns = 0;
        bool pingpong_reported = false;
    };
    struct PendingCommand {
        bool used = false;
        uint64_t name = 0;
        uint8_t sa = kNullAddress;
        uint64_t deadline_ns = 0;
    };

    void advance(uint64_t time_ns);
    void handleClaim(const J1939_frame &frame);
    void handleCommandedAddress(const J1939_frame &frame);
    void handleTraffic(uint8_t sa, uint64_t time_ns);

    Node &nodeFor(uint64_t name, uint64_t time_ns, bool *created);
    void emit(const NodeEvent &event);
    void emitAddressChange(Node &node, uint8_t old_sa, uint8_t new_sa, uint64_t time_ns,
                           bool created, bool commanded);
    bool takeCommand(const Node &node, uint8_t sa);

    uint8_t lastOwnedSa(const Node &node) const;
    void leaveContests(Node &node, uint8_t except_sa, uint64_t time_ns);
    void takeSlot(Node &node, uint8_t sa);
    // Drops `sa` from the node's addresses (not the slot); returns whether it still has one.
    bool dropSa(Node &node, uint8_t sa);
    // Gives up every SA the node owns; `moved` marks them for DuplicateName detection.
    void releaseAll(Node &node, bool moved, uint64_t time_ns);
    void attributeSpan(uint8_t sa, Node &to, uint64_t time_ns);
    void openContest(uint8_t sa, Node &owner, Node &rival, bool lower_wins, uint64_t time_ns);
    void resolveContest(uint8_t sa, Branch branch, uint64_t time_ns);
    void displace(Node &node, uint8_t sa, uint64_t time_ns, uint64_t deadline_ns);
    void notePingPong(uint8_t sa, Node &claimer, uint64_t time_ns);
    void expireContests();
    void expireCommands();
    void expireStale();

    NodeTableConfig config_;
    std::function<void(const NodeEvent &)> callback_;
    std::map<uint64_t, Node> nodes_;
    Slot slots_[256];
    PendingCommand commands_[8];
    NodeTableStats stats_;
    uint64_t now_ns_ = 0;
    uint64_t next_deadline_ns_ = UINT64_MAX; // earliest contest/command deadline
    uint64_t next_stale_scan_ns_ = 0;
};

const char *toString(NodeEventType type);
const char *toString(Branch branch);
const char *toString(NodeState state);

#endif //J1939_NODE_TABLE_H
