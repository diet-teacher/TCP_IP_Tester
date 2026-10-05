#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include "protocol.h"
#include <iostream>
#include <set>
void require(bool ok) { if (!ok) throw std::runtime_error("TCP integration assertion failed"); }
void sendAll(SOCKET s, const wire::Bytes& bytes) {
    size_t offset = 0;
    while (offset < bytes.size()) {
        int n = send(s, reinterpret_cast<const char*>(bytes.data()+offset), int(bytes.size()-offset), 0);
        require(n > 0); offset += n;
    }
}
void timeout(SOCKET s) {
    DWORD ms = 5000;
    setsockopt(s,SOL_SOCKET,SO_RCVTIMEO,reinterpret_cast<char*>(&ms),sizeof(ms));
    setsockopt(s,SOL_SOCKET,SO_SNDTIMEO,reinterpret_cast<char*>(&ms),sizeof(ms));
}
std::vector<wire::Frame> frames() {
    return {{wire::Text,1,{0xED,0x95,0x9C,0xEA,0xB8,0x80}},
            {wire::Integer,2,{0xFF,0xFF,0xFF,0xD6}},
            {wire::Real,3,{0x3F,0xF0,0,0,0,0,0,0}}, {wire::Text,4,{}}};
}
int main(int argc, char** argv) {
    WSADATA startup; require(WSAStartup(MAKEWORD(2,2),&startup) == 0);
    try {
        SOCKET s = socket(AF_INET,SOCK_STREAM,IPPROTO_TCP); require(s != INVALID_SOCKET); timeout(s);
        sockaddr_in address{}; address.sin_family = AF_INET; address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        if (argc == 2) {
            address.sin_port = htons(u_short(std::stoi(argv[1])));
            require(connect(s,reinterpret_cast<sockaddr*>(&address),sizeof(address)) == 0);
            wire::Bytes batch;
            for (const auto& f : frames()) { auto b = wire::encode(f); batch.insert(batch.end(),b.begin(),b.end()); }
            for (auto byte : batch) sendAll(s,{byte}); // Arbitrary fragmented writes.
            sendAll(s,batch); // Coalesced frames.
            shutdown(s,SD_SEND);
            wire::Decoder parser; int acks = 0, echoes = 0; uint8_t chunk[7];
            int n;
            while ((n = recv(s,reinterpret_cast<char*>(chunk),sizeof(chunk),0)) > 0) {
                parser.feed(chunk,n,[&](const wire::Frame& f) {
                    require(f.sequence >= 1 && f.sequence <= 4);
                    if (f.type == wire::Ack) ++acks;
                    else { auto original = frames()[f.sequence-1]; require(f.type == original.type && f.payload == original.payload); ++echoes; }
                });
            }
            require(n == 0 && acks == 8 && echoes == 8 && parser.pending() == 0);
            closesocket(s); WSACleanup(); return 0;
        }
        require(bind(s,reinterpret_cast<sockaddr*>(&address),sizeof(address)) == 0); require(listen(s,1) == 0);
        int size = sizeof(address); require(getsockname(s,reinterpret_cast<sockaddr*>(&address),&size) == 0);
        wchar_t executable[32768]; GetModuleFileNameW(nullptr,executable,32768);
        std::wstring command = L"\"" + std::wstring(executable) + L"\" " + std::to_wstring(ntohs(address.sin_port));
        STARTUPINFOW si{}; si.cb = sizeof(si); PROCESS_INFORMATION pi{};
        require(CreateProcessW(executable,command.data(),nullptr,nullptr,FALSE,CREATE_NO_WINDOW,nullptr,nullptr,&si,&pi));
        CloseHandle(pi.hThread);
        fd_set ready; FD_ZERO(&ready); FD_SET(s,&ready); timeval wait{5,0}; require(select(0,&ready,nullptr,nullptr,&wait) == 1);
        SOCKET client = accept(s,nullptr,nullptr); require(client != INVALID_SOCKET); timeout(client);
        wire::Decoder parser; int count = 0; uint8_t chunk[11]; int n;
        while ((n = recv(client,reinterpret_cast<char*>(chunk),sizeof(chunk),0)) > 0) {
            parser.feed(chunk,n,[&](const wire::Frame& f) {
                require(f.sequence == uint32_t(count % 4 + 1)); auto original = frames()[count % 4];
                require(f.type == original.type && f.payload == original.payload); ++count;
                sendAll(client,wire::encode(f)); sendAll(client,wire::encode({wire::Ack,f.sequence,{}}));
            });
        }
        require(n == 0 && count == 8 && parser.pending() == 0);
        shutdown(client,SD_SEND); closesocket(client); closesocket(s);
        require(WaitForSingleObject(pi.hProcess,7000) == WAIT_OBJECT_0);
        DWORD code; require(GetExitCodeProcess(pi.hProcess,&code) && code == 0); CloseHandle(pi.hProcess);
        WSACleanup(); std::cout << "PASS: two processes, loopback TCP, UTF-8/Int32/Float64/empty, fragmented+batch, echo+ACK, graceful EOF\n"; return 0;
    } catch (const std::exception& e) { std::cerr << e.what() << " WSA=" << WSAGetLastError() << '\n'; WSACleanup(); return 1; }
}
