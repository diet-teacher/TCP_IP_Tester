#pragma once
#include <cstdint>
#include <cstring>
#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

// Wire format is deliberately independent of struct padding and host endianness.
namespace wire {
using Bytes = std::vector<uint8_t>;
constexpr size_t HeaderSize = 16;
constexpr uint32_t MaxPayload = 65536;
constexpr uint32_t MaxImageBytes = 64 * 1024 * 1024;
constexpr uint32_t ImageHeaderSize = 24;
enum Type : uint16_t { Text = 1, Integer = 2, Real = 3, Ack = 4, Image = 5 };
inline void put(Bytes& b, uint64_t n, int width) {
    for (int i = width - 1; i >= 0; --i) b.push_back(uint8_t(n >> (i * 8)));
}
inline uint64_t get(const uint8_t* p, int width) {
    uint64_t n = 0;
    for (int i = 0; i < width; ++i) n = (n << 8) | p[i];
    return n;
}
inline bool utf8(const Bytes& b) {
    size_t i = 0;
    while (i < b.size()) {
        uint32_t c = b[i++], min = 0; int n = 0;
        if (c < 128) continue;
        if (c >= 0xC2 && c <= 0xDF) { n = 1; c &= 31; min = 128; }
        else if (c >= 0xE0 && c <= 0xEF) { n = 2; c &= 15; min = 2048; }
        else if (c >= 0xF0 && c <= 0xF4) { n = 3; c &= 7; min = 65536; }
        else return false;
        while (n--) { if (i == b.size() || (b[i] & 0xC0) != 0x80) return false; c = (c << 6) | (b[i++] & 63); }
        if (c < min || c > 0x10FFFF || (c >= 0xD800 && c <= 0xDFFF)) return false;
    }
    return true;
}
struct Frame { uint16_t type; uint32_t sequence; Bytes payload; };
inline void validate(uint16_t type, size_t size) {
    if (size > (type == Image ? MaxImageBytes + ImageHeaderSize : MaxPayload)) throw std::runtime_error("payload exceeds limit");
    if (type < Text || type > Image) throw std::runtime_error("unknown message type");
    if (type == Image && size < ImageHeaderSize) throw std::runtime_error("missing image metadata");
    if ((type == Integer && size != 4) || (type == Real && size != 8) || (type == Ack && size != 0))
        throw std::runtime_error("payload length does not match type");
}
struct ImageInfo { uint32_t width, height, channels, bits, stride, size; };
inline void validateImage(const ImageInfo& m) {
    if (!m.width || !m.height || m.width > 16384 || m.height > 16384 ||
        (m.channels != 1 && m.channels != 3 && m.channels != 4) || m.bits != 8 ||
        uint64_t(m.width) * m.channels != m.stride ||
        uint64_t(m.stride) * m.height != m.size || m.size > MaxImageBytes)
        throw std::runtime_error("invalid image dimensions/channels/bits/stride/size");
}
inline ImageInfo imageInfo(const Bytes& p) {
    if (p.size() < ImageHeaderSize) throw std::runtime_error("missing image metadata");
    ImageInfo m{uint32_t(get(p.data(),4)),uint32_t(get(p.data()+4,4)),uint32_t(get(p.data()+8,4)),
        uint32_t(get(p.data()+12,4)),uint32_t(get(p.data()+16,4)),uint32_t(get(p.data()+20,4))};
    validateImage(m);
    if (p.size() != ImageHeaderSize + size_t(m.size)) throw std::runtime_error("image byte count mismatch");
    return m;
}
inline Bytes imagePayload(const ImageInfo& m, const Bytes& pixels) {
    validateImage(m);
    if (pixels.size() != m.size) throw std::runtime_error("image pixels mismatch");
    Bytes p; p.reserve(ImageHeaderSize + pixels.size());
    for (auto v : {m.width,m.height,m.channels,m.bits,m.stride,m.size}) put(p,v,4);
    p.insert(p.end(),pixels.begin(),pixels.end()); return p;
}
inline Bytes encode(const Frame& f) {
    validate(f.type, f.payload.size());
    if (f.type == Text && !utf8(f.payload)) throw std::runtime_error("invalid UTF-8");
    if (f.type == Image) imageInfo(f.payload);
    Bytes b{'T','C','P','T'};
    put(b, 1, 2); put(b, f.type, 2); put(b, f.sequence, 4); put(b, f.payload.size(), 4);
    b.insert(b.end(), f.payload.begin(), f.payload.end()); return b;
}
class Decoder {
    Bytes buffer;
public:
    size_t pending() const { return buffer.size(); }
    void clear() { buffer.clear(); }
    template<class Fn> void feed(const uint8_t* data, size_t size, Fn receive) {
        // Caller reads bounded chunks. Retain only the incomplete frame.
        buffer.insert(buffer.end(), data, data + size);
        size_t offset = 0;
        while (buffer.size() - offset >= HeaderSize) {
            const auto* p = buffer.data() + offset;
            if (std::memcmp(p, "TCPT", 4)) throw std::runtime_error("bad magic (expected TCPT)");
            if (get(p + 4, 2) != 1) throw std::runtime_error("unsupported protocol version");
            auto type = uint16_t(get(p + 6, 2)); auto length = uint32_t(get(p + 12, 4));
            validate(type, length);
            if (buffer.size() - offset < HeaderSize + length) break;
            Frame f{type, uint32_t(get(p + 8, 4)), Bytes(p + HeaderSize, p + HeaderSize + length)};
            if (type == Text && !utf8(f.payload)) throw std::runtime_error("invalid UTF-8");
            if (type == Image) imageInfo(f.payload);
            receive(f); offset += HeaderSize + length;
        }
        buffer.erase(buffer.begin(), buffer.begin() + offset);
    }
};
inline std::string hex(const Bytes& b) {
    std::ostringstream s; s << std::hex << std::uppercase << std::setfill('0');
    for (auto c : b) s << std::setw(2) << unsigned(c) << ' ';
    return s.str();
}
}
