//
// Passive J1939 / ISO 11783 / NMEA 2000 address-claim tracking.
//
// How a claim of SA X by NAME B is handled (see node_table.h for the event meanings):
//  - X free: B takes it (releasing any SA it had).
//  - X held by A, B < A: B takes X at once and A is Displaced. The contest window stays open
//    so frames on X are CONTESTED until A moves on or the window closes.
//  - X held by A, B > A: X is CONTESTED, A keeps it provisionally. A re-claiming X within the
//    window -> A keeps it (HolderDefended), B Displaced. B claiming elsewhere -> withdrew.
//    Window closes without a defence -> X goes to B and A is presumed Offline.
//  - A Displaced node claiming its lost SA after the window is NonCompliantNode.
//  - X held by a TimedOut node: taken without a contest.
//

#include "node_table.h"

#include <algorithm>

NodeTable::NodeTable(NodeTableConfig config) : config_(config) {
    config_.pingpong_changes = std::max<uint8_t>(2, std::min<uint8_t>(16, config_.pingpong_changes));
}

void NodeTable::emit(const NodeEvent &event) {
    if (callback_) {
        callback_(event);
    }
}

Node &NodeTable::nodeFor(uint64_t name, uint64_t time_ns, bool *created) {
    auto it = nodes_.find(name);
    if (it != nodes_.end()) {
        *created = false;
        return it->second;
    }
    Node node;
    node.name_ = name;
    node.state_ = NodeState::Active;
    node.sa_ = kNullAddress;
    node.duplicate_sa_ = kNullAddress;
    node.first_seen_ns_ = node.last_seen_ns_ = node.last_claim_ns_ = node.liveness_ns_ = time_ns;
    node.lost_sa_ = kNullAddress;
    node.lost_deadline_ns_ = 0;
    node.previous_sa_ = kNullAddress;
    *created = true;
    return nodes_.emplace(name, node).first->second;
}

void NodeTable::handleFrame(const J1939_frame &frame) {
    advance(frame.time_ns_);
    if (frame.pgn_ == kPgnAddressClaimed) {
        handleClaim(frame);
        return;
    }
    if (frame.pgn_ == kPgnRequest) {
        if (frame.dlc_ >= 3 && frame.buffer_[0] == 0x00 && frame.buffer_[1] == 0xEE &&
            frame.buffer_[2] == 0x00) {
            stats_.claim_requests++;
        }
    } else if (frame.pgn_ == kPgnCommandedAddress) {
        handleCommandedAddress(frame);
    }
    handleTraffic(frame.src_, now_ns_);
}

void NodeTable::flush(uint64_t now_ns) {
    advance(now_ns);
}

void NodeTable::advance(uint64_t time_ns) {
    if (time_ns > now_ns_) {
        now_ns_ = time_ns;
    }
    if (now_ns_ > next_deadline_ns_) {
        expireContests();
        expireCommands();
        next_deadline_ns_ = UINT64_MAX;
        for (const Slot &slot : slots_) {
            if (slot.contested) {
                next_deadline_ns_ = std::min(next_deadline_ns_, slot.deadline_ns);
            }
        }
        for (const PendingCommand &cmd : commands_) {
            if (cmd.used) {
                next_deadline_ns_ = std::min(next_deadline_ns_, cmd.deadline_ns);
            }
        }
    }
    if (config_.stale_timeout_ns != 0 && now_ns_ >= next_stale_scan_ns_) {
        expireStale();
    }
}

// --- claims ---

uint8_t NodeTable::lastOwnedSa(const Node &node) const {
    if ((node.state_ == NodeState::Active || node.state_ == NodeState::TimedOut) &&
        node.sa_ != kNullAddress) {
        return node.sa_;
    }
    return node.previous_sa_;
}

void NodeTable::handleClaim(const J1939_frame &frame) {
    const uint8_t x = frame.src_;
    const uint64_t t = now_ns_;
    uint64_t name = 0;
    if (frame.dlc_ != 8 || x == kGlobalAddress ||
        !J1939Name::rawFromBytes(frame.buffer_, frame.dlc_, &name)) {
        stats_.malformed_claims++;
        return;
    }
    stats_.claims++;
    bool created = false;
    Node &b = nodeFor(name, t, &created);
    b.last_claim_ns_ = b.last_seen_ns_ = b.liveness_ns_ = t;
    if (created) {
        NodeEvent e{NodeEventType::NodeAppeared, t};
        e.sa = x;
        e.name = b.name_;
        emit(e);
    }
    const uint8_t old_sa = lastOwnedSa(b);

    if (x == kNullAddress) {
        leaveContests(b, x, t);
        releaseAll(b, false, t);
        b.state_ = NodeState::CannotClaim;
        stats_.cannot_claims++;
        NodeEvent e{NodeEventType::CannotClaim, t};
        e.sa = kNullAddress;
        e.old_sa = old_sa;
        e.name = b.name_;
        emit(e);
        return;
    }

    Slot &s = slots_[x];
    if (s.moved_away != nullptr && s.moved_away != &b) {
        s.moved_away = nullptr; // someone else claimed X in between: not a duplicate
    }
    notePingPong(x, b, t);
    if (b.state_ == NodeState::Displaced && b.lost_sa_ == x && t > b.lost_deadline_ns_ &&
        s.owner != nullptr && s.owner != &b) {
        // Still insisting on an SA it lost, which the winner still holds. (If the winner has
        // since left, claiming the now-free SA is fine.)
        stats_.non_compliant++;
        NodeEvent e{NodeEventType::NonCompliantNode, t};
        e.sa = x;
        e.name = b.name_;
        e.other_name = s.owner->name_;
        emit(e);
    }
    leaveContests(b, x, t);
    const bool commanded = takeCommand(b, x);

    if (s.contested) {
        if (s.owner == &b) {
            if (!s.lower_wins) {
                resolveContest(x, Branch::HolderDefended, t);
            }
            return; // the lower-NAME winner repeating its claim: nothing changes
        }
        if (s.rival == &b && !s.lower_wins) {
            return; // the challenger repeating its claim: still waiting for the holder
        }
        // Either the displaced higher holder is back within the window (a new contest against
        // the lower winner), or a third NAME claims X: settle the open contest for the current
        // owner first, then treat this claim against that owner.
        resolveContest(x, s.lower_wins ? Branch::LowerNameWins : Branch::None, t);
    }

    if (s.owner == &b) {
        // Re-claim of an SA it owns (e.g. answering a Request), or of its other SA while
        // flagged DuplicateName (that one becomes the primary).
        if (b.sa_ != x) {
            std::swap(b.sa_, b.duplicate_sa_);
        }
        if (b.state_ == NodeState::TimedOut) {
            b.state_ = NodeState::Active;
            NodeEvent e{NodeEventType::NodeAppeared, t};
            e.sa = x;
            e.name = b.name_;
            emit(e);
        }
        return;
    }

    if (s.owner != nullptr && s.owner->state_ != NodeState::TimedOut) {
        Node &a = *s.owner;
        releaseAll(b, true, t);
        if (b.name_ < a.name_) {
            displace(a, x, t, t + config_.contest_window_ns);
            takeSlot(b, x);
            openContest(x, b, a, true, t);
            stats_.reassignments++;
            NodeEvent e{NodeEventType::AddressReassigned, t};
            e.sa = x;
            e.name = b.name_;
            e.other_name = a.name_;
            emit(e);
            emitAddressChange(b, old_sa, x, t, created, commanded);
        } else {
            b.state_ = NodeState::Contending;
            b.sa_ = x;
            openContest(x, a, b, false, t);
        }
        return;
    }

    if (s.owner != nullptr) {
        // Held by a timed-out node: presumed gone, so no contest.
        Node &a = *s.owner;
        releaseAll(b, true, t);
        dropSa(a, x);
        takeSlot(b, x);
        stats_.reassignments++;
        NodeEvent e{NodeEventType::AddressReassigned, t};
        e.sa = x;
        e.name = b.name_;
        e.other_name = a.name_;
        emit(e);
        emitAddressChange(b, old_sa, x, t, created, commanded);
        return;
    }

    // X is free.
    if (s.moved_away == &b && t - s.moved_away_ns <= config_.contest_window_ns &&
        b.state_ == NodeState::Active && b.sa_ != kNullAddress && b.sa_ != x) {
        // B left X for Y and claims X again while still on Y, with nobody else claiming X in
        // between: two devices are using the same NAME. Keep both bindings.
        const uint8_t y = b.sa_;
        s.moved_away = nullptr;
        s.owner = &b;
        b.sa_ = x;
        b.duplicate_sa_ = y;
        // Reported once per pair: while both bindings exist, claims on either SA take the
        // "owns it already" path above and never get here.
        stats_.duplicates++;
        NodeEvent e{NodeEventType::DuplicateName, t};
        e.sa = x;
        e.old_sa = y;
        e.name = b.name_;
        emit(e);
        attributeSpan(x, b, t);
        return;
    }
    releaseAll(b, true, t);
    takeSlot(b, x);
    emitAddressChange(b, old_sa, x, t, created, commanded);
    attributeSpan(x, b, t);
}

void NodeTable::leaveContests(Node &node, uint8_t except_sa, uint64_t time_ns) {
    const uint8_t candidates[3] = {node.sa_, node.duplicate_sa_, node.lost_sa_};
    for (uint8_t sa : candidates) {
        if (sa >= kNullAddress || sa == except_sa) {
            continue;
        }
        Slot &s = slots_[sa];
        if (!s.contested || (s.owner != &node && s.rival != &node)) {
            continue;
        }
        if (s.lower_wins) {
            // Either the displaced holder moved on, or the lower-NAME winner did: the contest is
            // decided either way.
            resolveContest(sa, Branch::LowerNameWins, time_ns);
        } else if (s.rival == &node) {
            node.sa_ = kNullAddress;
            resolveContest(sa, Branch::ChallengerWithdrew, time_ns);
        } else {
            // The holder walked away from its own contested SA: the challenger gets it.
            resolveContest(sa, Branch::HolderSilent, time_ns);
        }
    }
}

void NodeTable::emitAddressChange(Node &node, uint8_t old_sa, uint8_t new_sa, uint64_t time_ns,
                                  bool created, bool commanded) {
    if (created || old_sa == new_sa) {
        return; // NodeAppeared already reported where it first showed up
    }
    NodeEvent e{NodeEventType::NodeAddressChanged, time_ns};
    e.sa = new_sa;
    e.old_sa = old_sa;
    e.name = node.name_;
    e.commanded = commanded;
    emit(e);
}

void NodeTable::takeSlot(Node &node, uint8_t sa) {
    Slot &s = slots_[sa];
    s.owner = &node;
    s.moved_away = nullptr;
    node.sa_ = sa;
    node.state_ = NodeState::Active;
    node.lost_sa_ = kNullAddress;
}

bool NodeTable::dropSa(Node &node, uint8_t sa) {
    if (node.duplicate_sa_ == sa) {
        node.duplicate_sa_ = kNullAddress;
        node.previous_sa_ = sa;
    } else if (node.sa_ == sa) {
        node.sa_ = node.duplicate_sa_;
        node.duplicate_sa_ = kNullAddress;
        node.previous_sa_ = sa;
    }
    return node.sa_ != kNullAddress;
}

void NodeTable::releaseAll(Node &node, bool moved, uint64_t time_ns) {
    const uint8_t owned[2] = {node.sa_, node.duplicate_sa_};
    for (uint8_t sa : owned) {
        if (sa >= kNullAddress) {
            continue;
        }
        Slot &s = slots_[sa];
        if (s.owner == &node) { // a Contending node's sa_ isn't its own yet
            s.owner = nullptr;
            if (moved) {
                s.moved_away = &node;
                s.moved_away_ns = time_ns;
            }
            node.previous_sa_ = sa;
        }
    }
    node.sa_ = node.duplicate_sa_ = kNullAddress;
}

void NodeTable::displace(Node &node, uint8_t sa, uint64_t time_ns, uint64_t deadline_ns) {
    if (!dropSa(node, sa)) {
        node.state_ = NodeState::Displaced;
        // Its staleness clock starts now: it has no SA whose traffic could refresh it.
        node.liveness_ns_ = time_ns;
    }
    node.lost_sa_ = sa;
    node.lost_deadline_ns_ = deadline_ns;
}

// --- contests ---

void NodeTable::openContest(uint8_t sa, Node &owner, Node &rival, bool lower_wins,
                            uint64_t time_ns) {
    Slot &s = slots_[sa];
    s.contested = true;
    s.owner = &owner;
    s.rival = &rival;
    s.lower_wins = lower_wins;
    s.deadline_ns = time_ns + config_.contest_window_ns;
    next_deadline_ns_ = std::min(next_deadline_ns_, s.deadline_ns);
    stats_.contests++;
    NodeEvent e{NodeEventType::AddressContested, time_ns};
    e.sa = sa;
    e.name = lower_wins ? owner.name_ : rival.name_; // the challenger
    e.other_name = lower_wins ? rival.name_ : owner.name_;
    emit(e);
}

void NodeTable::resolveContest(uint8_t sa, Branch branch, uint64_t time_ns) {
    Slot &s = slots_[sa];
    Node *winner = s.owner;
    Node *loser = s.rival;
    const uint64_t deadline = s.deadline_ns;
    s.contested = false;
    s.rival = nullptr;
    stats_.resolved[static_cast<int>(branch)]++;

    if (branch == Branch::HolderDefended || branch == Branch::None) {
        // The challenger lost (or was superseded by a third claimant).
        loser->sa_ = kNullAddress;
        loser->state_ = NodeState::Displaced;
        loser->liveness_ns_ = time_ns;
        loser->lost_sa_ = sa;
        loser->lost_deadline_ns_ = deadline;
    } else if (branch == Branch::HolderSilent) {
        winner = loser;
        loser = s.owner;
        if (!dropSa(*loser, sa)) {
            loser->state_ = NodeState::Offline;
        }
        const uint8_t old_sa = winner->previous_sa_;
        takeSlot(*winner, sa);
        stats_.reassignments++;
        NodeEvent r{NodeEventType::AddressReassigned, time_ns};
        r.sa = sa;
        r.name = winner->name_;
        r.other_name = loser->name_;
        emit(r);
        if (old_sa != kNullAddress) {
            emitAddressChange(*winner, old_sa, sa, time_ns, false, false);
        }
    }
    // LowerNameWins / ChallengerWithdrew: ownership is already right.

    NodeEvent e{NodeEventType::ContestResolved, time_ns};
    e.sa = sa;
    e.name = winner->name_;
    e.other_name = loser->name_;
    e.branch = branch;
    emit(e);
    attributeSpan(sa, *winner, time_ns);
}

void NodeTable::attributeSpan(uint8_t sa, Node &to, uint64_t time_ns) {
    Slot &s = slots_[sa];
    if (!s.has_span) {
        return;
    }
    s.has_span = false;
    to.last_seen_ns_ = std::max(to.last_seen_ns_, s.span_to_ns);
    NodeEvent e{NodeEventType::Attributed, time_ns};
    e.sa = sa;
    e.name = to.name_;
    e.from_ns = s.span_from_ns;
    e.to_ns = s.span_to_ns;
    emit(e);
}

void NodeTable::expireContests() {
    for (int sa = 0; sa < kNullAddress; sa++) {
        Slot &s = slots_[sa];
        if (s.contested && now_ns_ > s.deadline_ns) {
            resolveContest(static_cast<uint8_t>(sa),
                           s.lower_wins ? Branch::LowerNameWins : Branch::HolderSilent,
                           s.deadline_ns);
        }
    }
}

void NodeTable::notePingPong(uint8_t sa, Node &claimer, uint64_t time_ns) {
    Slot &s = slots_[sa];
    if (s.last_claimer != nullptr && s.last_claimer != &claimer) {
        const uint8_t n = config_.pingpong_changes;
        s.changes_ns[s.change_head] = time_ns;
        s.change_head = static_cast<uint8_t>((s.change_head + 1) % n);
        if (s.change_count < n) {
            s.change_count++;
        }
        // With the ring full, change_head now points at the oldest of the last n changes.
        if (s.change_count == n && time_ns - s.changes_ns[s.change_head] <= config_.pingpong_span_ns &&
            (!s.pingpong_reported || time_ns - s.last_pingpong_ns >= config_.pingpong_quiet_ns)) {
            s.pingpong_reported = true;
            s.last_pingpong_ns = time_ns;
            stats_.pingpongs++;
            NodeEvent e{NodeEventType::ClaimPingPong, time_ns};
            e.sa = sa;
            e.name = claimer.name_;
            e.other_name = s.last_claimer->name_;
            emit(e);
        }
    }
    s.last_claimer = &claimer;
}

// --- commanded address ---

void NodeTable::handleCommandedAddress(const J1939_frame &frame) {
    uint64_t name = 0;
    if (frame.dlc_ != 9 || frame.buffer_[8] >= kNullAddress ||
        !J1939Name::rawFromBytes(frame.buffer_, frame.dlc_, &name)) {
        stats_.malformed_commanded++;
        return;
    }
    stats_.commanded++;
    const uint8_t new_sa = frame.buffer_[8];
    // Reuse a free entry, or else the one closest to expiring.
    PendingCommand *slot = &commands_[0];
    for (PendingCommand &cmd : commands_) {
        if (!cmd.used || cmd.name == name) {
            slot = &cmd;
            break;
        }
        if (cmd.deadline_ns < slot->deadline_ns) {
            slot = &cmd;
        }
    }
    if (slot->used && slot->name != name) {
        stats_.commands_expired++;
    }
    slot->used = true;
    slot->name = name;
    slot->sa = new_sa;
    slot->deadline_ns = now_ns_ + config_.command_window_ns;
    next_deadline_ns_ = std::min(next_deadline_ns_, slot->deadline_ns);

    const Node *target = byName(name);
    NodeEvent e{NodeEventType::CommandedAddress, now_ns_};
    e.sa = new_sa;
    e.old_sa = target != nullptr ? target->sa_ : kNullAddress;
    e.source_sa = frame.src_;
    e.name = name;
    emit(e);
}

bool NodeTable::takeCommand(const Node &node, uint8_t sa) {
    for (PendingCommand &cmd : commands_) {
        if (cmd.used && cmd.name == node.name_ && cmd.sa == sa) {
            cmd.used = false;
            return true;
        }
    }
    return false;
}

void NodeTable::expireCommands() {
    for (PendingCommand &cmd : commands_) {
        if (cmd.used && now_ns_ > cmd.deadline_ns) {
            cmd.used = false;
            stats_.commands_expired++;
        }
    }
}

// --- traffic and staleness ---

void NodeTable::handleTraffic(uint8_t sa, uint64_t time_ns) {
    if (sa >= kNullAddress) {
        return;
    }
    Slot &s = slots_[sa];
    if (s.contested || s.owner == nullptr) {
        if (!s.has_span) {
            s.has_span = true;
            s.span_from_ns = time_ns;
        }
        s.span_to_ns = std::max(s.span_to_ns, time_ns);
        return;
    }
    Node &owner = *s.owner;
    owner.last_seen_ns_ = std::max(owner.last_seen_ns_, time_ns);
    owner.liveness_ns_ = std::max(owner.liveness_ns_, time_ns);
    if (owner.state_ == NodeState::TimedOut) {
        owner.state_ = NodeState::Active;
        NodeEvent e{NodeEventType::NodeAppeared, time_ns};
        e.sa = sa;
        e.name = owner.name_;
        emit(e);
    }
}

void NodeTable::expireStale() {
    next_stale_scan_ns_ = now_ns_ + std::max<uint64_t>(1, config_.stale_timeout_ns / 16);
    for (auto &entry : nodes_) {
        Node &node = entry.second;
        const bool can_go_stale = node.state_ == NodeState::Active ||
                                  node.state_ == NodeState::Displaced ||
                                  node.state_ == NodeState::CannotClaim;
        if (can_go_stale && now_ns_ - node.liveness_ns_ > config_.stale_timeout_ns) {
            node.state_ = NodeState::TimedOut;
            stats_.timeouts++;
            NodeEvent e{NodeEventType::NodeTimedOut, now_ns_};
            e.sa = node.sa_;
            e.name = node.name_;
            emit(e);
        }
    }
}

// --- lookups ---

Resolution NodeTable::resolve(const J1939_frame &frame) const {
    uint64_t name = 0;
    if (frame.pgn_ == kPgnAddressClaimed && frame.dlc_ == 8 &&
        J1939Name::rawFromBytes(frame.buffer_, frame.dlc_, &name)) {
        return {Resolution::Resolved, name, 0};
    }
    if (frame.src_ >= kNullAddress) {
        return {Resolution::Unresolved, 0, 0};
    }
    const Slot &s = slots_[frame.src_];
    if (s.contested) {
        return {Resolution::Contested, s.owner->name_, s.rival->name_};
    }
    if (s.owner != nullptr) {
        return {Resolution::Resolved, s.owner->name_, 0};
    }
    return {Resolution::Unresolved, 0, 0};
}

const Node *NodeTable::bySa(uint8_t sa) const {
    return slots_[sa].owner;
}

const Node *NodeTable::byName(uint64_t name) const {
    auto it = nodes_.find(name);
    return it == nodes_.end() ? nullptr : &it->second;
}

void NodeTable::forEach(const std::function<void(const Node &)> &cb) const {
    for (const auto &entry : nodes_) {
        cb(entry.second);
    }
}

J1939_frame NodeTable::makeAddressClaimRequest(uint64_t time_ns) {
    J1939_frame frame;
    const uint8_t data[3] = {0x00, 0xEE, 0x00};
    frame.setFrom(CAN_EFF_FLAG | (6u << 26) | (0xEAu << 16) | (kGlobalAddress << 8) | kNullAddress,
                  data, 3);
    frame.time_ns_ = time_ns;
    return frame;
}

const char *toString(NodeEventType type) {
    switch (type) {
    case NodeEventType::NodeAppeared: return "NodeAppeared";
    case NodeEventType::NodeAddressChanged: return "NodeAddressChanged";
    case NodeEventType::AddressReassigned: return "AddressReassigned";
    case NodeEventType::CannotClaim: return "CannotClaim";
    case NodeEventType::NodeTimedOut: return "NodeTimedOut";
    case NodeEventType::AddressContested: return "AddressContested";
    case NodeEventType::ContestResolved: return "ContestResolved";
    case NodeEventType::NonCompliantNode: return "NonCompliantNode";
    case NodeEventType::DuplicateName: return "DuplicateName";
    case NodeEventType::ClaimPingPong: return "ClaimPingPong";
    case NodeEventType::CommandedAddress: return "CommandedAddress";
    case NodeEventType::Attributed: return "Attributed";
    }
    return "?";
}

const char *toString(Branch branch) {
    switch (branch) {
    case Branch::None: return "Superseded";
    case Branch::LowerNameWins: return "LowerNameWins";
    case Branch::HolderDefended: return "HolderDefended";
    case Branch::HolderSilent: return "HolderSilent";
    case Branch::ChallengerWithdrew: return "ChallengerWithdrew";
    }
    return "?";
}

const char *toString(NodeState state) {
    switch (state) {
    case NodeState::Active: return "Active";
    case NodeState::Contending: return "Contending";
    case NodeState::Displaced: return "Displaced";
    case NodeState::CannotClaim: return "CannotClaim";
    case NodeState::Offline: return "Offline";
    case NodeState::TimedOut: return "TimedOut";
    }
    return "?";
}
