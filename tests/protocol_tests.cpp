#include "protocol.h"
#include <iostream>
#include <limits>
void check(bool ok) { if (!ok) throw std::runtime_error("test assertion failed"); }
int main() {
    using namespace wire;
    Frame f{Integer, 0x01020304, {0xFF,0xFF,0xFF,0xD6}};
    auto b = encode(f);
    check(hex(b) == "54 43 50 54 00 01 00 02 01 02 03 04 00 00 00 04 FF FF FF D6 ");
    for (size_t split = 0; split <= b.size(); ++split) {
        Decoder d; int count = 0;
        auto receive = [&](const Frame& r) { check(r.type == f.type && r.sequence == f.sequence && r.payload == f.payload); ++count; };
        d.feed(b.data(), split, receive); d.feed(b.data()+split, b.size()-split, receive);
        check(count == 1 && d.pending() == 0);
    }
    Decoder d; int count = 0;
    auto receive = [&](const Frame&) { ++count; };
    Bytes many;
    for (int i = 0; i < 100; ++i) many.insert(many.end(), b.begin(), b.end());
    d.feed(many.data(), many.size(), receive); check(count == 100);
    d.clear(); count = 0;
    for (auto c : many) d.feed(&c, 1, receive);
    check(count == 100 && d.pending() == 0);
    auto bad = [&](Bytes data) { Decoder parser; bool failed = false; try { parser.feed(data.data(), data.size(), receive); } catch (const std::runtime_error&) { failed = true; } check(failed); };
    auto corrupt = b; corrupt[0] = 0; bad(corrupt);
    corrupt = b; corrupt[5] = 2; bad(corrupt);
    corrupt = b; corrupt[7] = 99; bad(corrupt);
    corrupt = b; corrupt[15] = 3; bad(corrupt);
    corrupt = b; corrupt[12] = 1; bad(corrupt);
    check(utf8(Bytes{0xED,0x95,0x9C,0xEA,0xB8,0x80}));
    check(!utf8(Bytes{0xC0,0x80})); check(!utf8(Bytes{0xED,0xA0,0x80}));
    auto text = encode({Text, 1, Bytes(MaxPayload, 'a')});
    d.clear(); d.feed(text.data(), text.size(), receive); check(d.pending() == 0);
    auto ack = encode({Ack, 42, {}}); d.feed(ack.data(), ack.size(), receive);
    auto pixels=imagePayload({2,1,3,8,6,6},{0,1,2,3,4,5});
    check(hex(Bytes(pixels.begin(),pixels.begin()+24)) == "00 00 00 02 00 00 00 01 00 00 00 03 00 00 00 08 00 00 00 06 00 00 00 06 ");
    auto image=encode({Image,9,pixels});
    for(size_t split=0;split<=image.size();++split) {
        Decoder parser; int images=0;
        auto read=[&](const Frame& f) { check(f.type==Image && f.payload==pixels); ++images; };
        parser.feed(image.data(),split,read); parser.feed(image.data()+split,image.size()-split,read);
        check(images==1 && parser.pending()==0);
    }
    for(size_t offset : {size_t(19),size_t(27),size_t(31),size_t(35),size_t(39)}) {
        auto broken=image; broken[offset]=0; bad(broken);
    }
    static_assert(sizeof(double) == 8 && std::numeric_limits<double>::is_iec559);
    std::cout << "PASS: golden bytes, all split points, bytewise, coalesced, malformed headers, UTF-8, max payload, ACK\n";
}
