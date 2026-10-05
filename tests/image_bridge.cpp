// Headless integration peer. Uses the same codec + wire code as the GUI.
#include <winsock2.h>
#include <ws2tcpip.h>
#include "image_io.h"
#include <iostream>
void sendAll(SOCKET s,const wire::Bytes& b) {
    size_t done=0;
    while (done<b.size()) {
        int n=send(s,reinterpret_cast<const char*>(b.data()+done),int((std::min)(size_t(65536),b.size()-done)),0);
        if(n<=0) throw std::runtime_error("send failed"); done+=n;
    }
}
int wmain(int argc,wchar_t** argv) {
    if(argc<3) return 2;
    WSADATA data; if(WSAStartup(MAKEWORD(2,2),&data)) return 2;
    if(FAILED(CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED))) return 2;
    SOCKET server=INVALID_SOCKET,client=INVALID_SOCKET;
    try {
        server=socket(AF_INET,SOCK_STREAM,IPPROTO_TCP);
        sockaddr_in a{}; a.sin_family=AF_INET; a.sin_addr.s_addr=htonl(INADDR_LOOPBACK);
        if(bind(server,reinterpret_cast<sockaddr*>(&a),sizeof(a)) || listen(server,1)) throw std::runtime_error("listen failed");
        int len=sizeof(a); getsockname(server,reinterpret_cast<sockaddr*>(&a),&len);
        std::cout<<ntohs(a.sin_port)<<std::endl;
        client=accept(server,nullptr,nullptr); if(client==INVALID_SOCKET) throw std::runtime_error("accept failed");
        DWORD ms=15000; setsockopt(client,SOL_SOCKET,SO_RCVTIMEO,reinterpret_cast<char*>(&ms),sizeof(ms));
        setsockopt(client,SOL_SOCKET,SO_SNDTIMEO,reinterpret_cast<char*>(&ms),sizeof(ms));
        for(int i=2;i<argc;++i) {
            auto p=imaging::load(argv[i]); sendAll(client,wire::encode({wire::Image,uint32_t(i-1),std::move(p)}));
        }
        wire::Decoder parser; uint8_t chunk[32768]; int n;
        while((n=recv(client,reinterpret_cast<char*>(chunk),sizeof(chunk),0))>0) {
            parser.feed(chunk,n,[&](const wire::Frame& f) {
                if(f.type==wire::Ack) return;
                if(f.type!=wire::Image) throw std::runtime_error("expected image");
                imaging::save(std::filesystem::path(argv[1])/(std::to_wstring(f.sequence)+L".png"),f.payload);
                sendAll(client,wire::encode(f)); sendAll(client,wire::encode({wire::Ack,f.sequence,{}}));
            });
        }
        if(n<0 || parser.pending()) throw std::runtime_error("receive failed or truncated image");
        closesocket(client); closesocket(server); CoUninitialize(); WSACleanup(); return 0;
    } catch(const std::exception& e) {
        std::cerr<<e.what()<<std::endl;
        if(client!=INVALID_SOCKET) closesocket(client); if(server!=INVALID_SOCKET) closesocket(server);
        CoUninitialize(); WSACleanup(); return 1;
    }
}
