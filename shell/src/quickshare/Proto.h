#pragma once

// Just enough protocol buffers for Quick Share: writing fields in order
// (varints, length-delimited bytes and nested messages) and reading a
// message into its fields. The frames it speaks are proto2 and small.
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace m3::quickshare::proto {

using Bytes = std::string; // raw bytes, as protobuf's own bytes fields

class Writer {
public:
    Writer& varint(int field, uint64_t v) {
        tag(field, 0);
        put(v);
        return *this;
    }
    Writer& sint(int field, int64_t v) { return varint(field, static_cast<uint64_t>(v)); } // int32/int64: two's complement
    Writer& boolean(int field, bool v) { return varint(field, v ? 1 : 0); }
    Writer& bytes(int field, const Bytes& v) {
        tag(field, 2);
        put(v.size());
        m_out += v;
        return *this;
    }
    Writer& message(int field, const Writer& w) { return bytes(field, w.str()); }
    const Bytes& str() const { return m_out; }

private:
    void tag(int field, int wire) { put(static_cast<uint64_t>(field) << 3 | static_cast<uint64_t>(wire)); }
    void put(uint64_t v) {
        while (v >= 0x80) {
            m_out += static_cast<char>((v & 0x7f) | 0x80);
            v >>= 7;
        }
        m_out += static_cast<char>(v);
    }
    Bytes m_out;
};

// A message's fields by number; a field may repeat
class Reader {
public:
    struct Value {
        uint64_t number = 0; // varint (and fixed) fields
        Bytes bytes;         // length-delimited fields
    };

    // False if the bytes aren't a well-formed message
    bool parse(const Bytes& data) {
        m_fields.clear();
        size_t i = 0;
        while (i < data.size()) {
            uint64_t key = 0;
            if (!get(data, i, key)) return false;
            const int field = static_cast<int>(key >> 3), wire = static_cast<int>(key & 7);
            Value v;
            if (wire == 0) {
                if (!get(data, i, v.number)) return false;
            } else if (wire == 2) {
                uint64_t len = 0;
                if (!get(data, i, len) || len > data.size() - i) return false;
                v.bytes = data.substr(i, len);
                i += len;
            } else if (wire == 1 || wire == 5) {
                const size_t n = wire == 1 ? 8 : 4;
                if (i + n > data.size()) return false;
                for (size_t k = 0; k < n; ++k) v.number |= static_cast<uint64_t>(static_cast<uint8_t>(data[i + k])) << (8 * k);
                i += n;
            } else {
                return false;
            }
            m_fields[field].push_back(std::move(v));
        }
        return true;
    }
    bool has(int field) const { return m_fields.contains(field); }
    uint64_t number(int field, uint64_t fallback = 0) const {
        const auto it = m_fields.find(field);
        return it == m_fields.end() ? fallback : it->second.front().number;
    }
    int64_t sint(int field, int64_t fallback = 0) const { return static_cast<int64_t>(number(field, static_cast<uint64_t>(fallback))); }
    Bytes bytes(int field) const {
        const auto it = m_fields.find(field);
        return it == m_fields.end() ? Bytes{} : it->second.front().bytes;
    }
    std::vector<Bytes> all(int field) const {
        std::vector<Bytes> out;
        if (const auto it = m_fields.find(field); it != m_fields.end())
            for (const auto& v : it->second) out.push_back(v.bytes);
        return out;
    }
    std::optional<Reader> message(int field) const {
        Reader r;
        if (!has(field) || !r.parse(bytes(field))) return std::nullopt;
        return r;
    }

private:
    static bool get(const Bytes& data, size_t& i, uint64_t& v) {
        v = 0;
        for (int shift = 0; shift < 64; shift += 7) {
            if (i >= data.size()) return false;
            const auto b = static_cast<uint8_t>(data[i++]);
            v |= static_cast<uint64_t>(b & 0x7f) << shift;
            if (!(b & 0x80)) return true;
        }
        return false;
    }
    std::map<int, std::vector<Value>> m_fields;
};

} // namespace m3::quickshare::proto
