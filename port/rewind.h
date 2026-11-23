// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Tommy McLeroy. Part of cps2recomp.
//
// Rewind's states (docs/PRACTICE.md, Port::RewindKeep): a whole state of the board is about 350 KB, and most of it is
// the same from one kept state to the next. So the newest is kept whole and each older one only as how it differs
// from the one after it: the bytes XORed, the runs of zero bytes packed (StateRing::Pack). Going back is taking the
// newest and remaking the one before it from it, a few kilobytes of work.
#pragma once

#include <cstdint>
#include <cstring>
#include <deque>
#include <vector>

namespace port {

class StateRing {
public:
    // A kept state: the frame it is the state before, the controls that frame ran with, the state
    struct Input {
        uint16_t players[4] = {};
        bool service = false, test = false;
    };
    struct Kept {
        uint64_t frame = 0;
        Input input;
        std::vector<uint8_t> state;
    };

    void Clear() {
        entries_.clear();
        bytes_ = 0;
    }
    bool Empty() const { return entries_.empty(); }
    size_t Size() const { return entries_.size(); }
    size_t Bytes() const { return bytes_; }
    uint64_t NewestFrame() const { return entries_.empty() ? 0 : entries_.back().frame; }
    uint64_t OldestFrame() const { return entries_.empty() ? 0 : entries_.front().frame; }

    // The newest, kept whole; the one that was the newest becomes how it differs from it. At most `most` kept and
    // `mostBytes` of memory: the oldest go.
    void Push(uint64_t frame, const Input &input, std::vector<uint8_t> &&state, size_t most, size_t mostBytes) {
        if (!entries_.empty()) {
            Entry &last = entries_.back();
            if (last.whole) {
                std::vector<uint8_t> packed;
                Pack(last.data, state, packed);
                bytes_ -= last.data.size();
                last.data = std::move(packed);
                last.whole = false;
                bytes_ += last.data.size();
            }
        }
        bytes_ += state.size();
        entries_.push_back({frame, input, std::move(state), true});
        while (entries_.size() > 1 && (entries_.size() > most || bytes_ > mostBytes)) {
            bytes_ -= entries_.front().data.size();
            entries_.pop_front();
        }
    }

    // The newest taken out (false: none kept), and the one before it made whole
    bool Pop(Kept &out) {
        if (entries_.empty()) {
            return false;
        }
        Entry &last = entries_.back();
        out.frame = last.frame;
        out.input = last.input;
        out.state = std::move(last.data);
        bytes_ -= out.state.size();
        entries_.pop_back();
        if (!entries_.empty() && !entries_.back().whole) {
            Entry &before = entries_.back();
            std::vector<uint8_t> whole;
            Unpack(out.state, before.data, whole);
            bytes_ -= before.data.size();
            before.data = std::move(whole);
            before.whole = true;
            bytes_ += before.data.size();
        }
        return true;
    }

    // Only the kept states before `frame` (the ones after it are what happened before going back to it)
    void DropFrom(uint64_t frame) {
        Kept dropped;
        while (!entries_.empty() && entries_.back().frame >= frame) {
            Pop(dropped);
        }
    }

    // `older` as it differs from `newer`: its size [4], then runs of [same bytes: 4][differing bytes: 4][those
    // XORed]. (The game's sources' part of a state can change its length: past the end of `newer`, its bytes are 0.)
    static void Pack(const std::vector<uint8_t> &older, const std::vector<uint8_t> &newer, std::vector<uint8_t> &out) {
        out.clear();
        const size_t n = older.size();
        const uint8_t *a = older.data();
        std::vector<uint8_t> padded;
        const uint8_t *b = newer.data();
        if (newer.size() < n) {
            padded.assign(n, 0);
            std::memcpy(padded.data(), newer.data(), newer.size());
            b = padded.data();
        }
        Put(out, static_cast<uint32_t>(n));
        size_t i = 0;
        while (i < n) {
            const size_t zerosFrom = i;
            while (i < n && a[i] == b[i]) {
                i++;
            }
            const uint32_t zeros = static_cast<uint32_t>(i - zerosFrom);
            if (i == n) {
                if (zeros) {
                    Put(out, zeros);
                    Put(out, 0);
                }
                break;
            }
            // (the differing bytes, and any few the same among them: a run of 8 or more ends it)
            const size_t from = i;
            size_t same = 0;
            while (i < n && same < 8) {
                same = a[i] == b[i] ? same + 1 : 0;
                i++;
            }
            if (same >= 8) {
                i -= same;
            }
            Put(out, zeros);
            Put(out, static_cast<uint32_t>(i - from));
            for (size_t k = from; k < i; k++) {
                out.push_back(a[k] ^ b[k]);
            }
        }
    }
    static void Unpack(const std::vector<uint8_t> &newer, const std::vector<uint8_t> &packed, std::vector<uint8_t> &out) {
        if (packed.size() < 4) {
            out = newer;
            return;
        }
        out = newer;
        out.resize(Get(packed, 0), 0);
        size_t at = 4, i = 0;
        while (at + 8 <= packed.size()) {
            const uint32_t zeros = Get(packed, at), literal = Get(packed, at + 4);
            at += 8;
            i += zeros;
            for (uint32_t k = 0; k < literal && i < out.size() && at < packed.size(); k++) {
                out[i++] ^= packed[at++];
            }
        }
    }

private:
    struct Entry {
        uint64_t frame;
        Input input;
        std::vector<uint8_t> data;
        bool whole;
    };
    static void Put(std::vector<uint8_t> &out, uint32_t v) {
        const uint8_t bytes[4] = {static_cast<uint8_t>(v), static_cast<uint8_t>(v >> 8), static_cast<uint8_t>(v >> 16),
                                  static_cast<uint8_t>(v >> 24)};
        out.insert(out.end(), bytes, bytes + 4);
    }
    static uint32_t Get(const std::vector<uint8_t> &in, size_t at) {
        return static_cast<uint32_t>(in[at]) | static_cast<uint32_t>(in[at + 1]) << 8 |
               static_cast<uint32_t>(in[at + 2]) << 16 | static_cast<uint32_t>(in[at + 3]) << 24;
    }
    std::deque<Entry> entries_;
    size_t bytes_ = 0;
};

} // namespace port
