#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <commdlg.h>
#include "protocol.h"
#include "image_io.h"
#include <algorithm>
#include <cmath>
#include <deque>
#include <fstream>
#include <map>
#include <filesystem>

constexpr UINT NetMessage = WM_APP + 1;
enum { Listen = 101, Connect, Stop, Send, Split, Burst, Clear, Save, Peer, PickImage, SendImage, SaveImage };
HWND window, addressBox, portBox, typeBox, valueBox, logBox, statusBox;
HWND imagePathBox, imageInfoBox, previewBox;
wire::Bytes receivedImage;
HBITMAP previewBitmap = nullptr;
constexpr size_t MaxQueue = wire::MaxImageBytes + wire::ImageHeaderSize + wire::HeaderSize + 1024 * 1024;
HFONT font;
SOCKET listener = INVALID_SOCKET, peer = INVALID_SOCKET;
bool connected = false;
uint32_t sequence = 1;
wire::Decoder decoder;
struct Pending {
    wire::Bytes data;
    size_t offset = 0;
    bool slow = false;
};
std::deque<Pending> outgoing;
std::map<uint32_t, ULONGLONG> acknowledgements;
size_t queued = 0;
std::wstring content(HWND h) {
    int n = GetWindowTextLengthW(h);
    std::wstring s(n + 1, L'\0');
    GetWindowTextW(h, s.data(), n + 1);
    s.resize(n);
    return s;
}
std::wstring wide(const std::string &s) {
    if (s.empty())
        return {};
    int n = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, s.data(), int(s.size()), nullptr, 0);
    if (!n)
        throw std::runtime_error("invalid UTF-8");
    std::wstring w(n, 0);
    MultiByteToWideChar(CP_UTF8, 0, s.data(), int(s.size()), w.data(), n);
    return w;
}
std::string utf(const std::wstring &w) {
    if (w.empty())
        return {};
    int n = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, w.data(), int(w.size()), nullptr, 0, nullptr,
                                nullptr);
    if (!n)
        throw std::runtime_error("invalid Unicode input");
    std::string s(n, 0);
    WideCharToMultiByte(CP_UTF8, 0, w.data(), int(w.size()), s.data(), n, nullptr, nullptr);
    return s;
}
void log(const std::wstring &s) {
    if (GetWindowTextLengthW(logBox) > 400000)
        SetWindowTextW(logBox, L"[이전 로그 자동 정리]\r\n");
    SYSTEMTIME t;
    GetLocalTime(&t);
    wchar_t stamp[32];
    swprintf_s(stamp, L"[%02u:%02u:%02u.%03u] ", t.wHour, t.wMinute, t.wSecond, t.wMilliseconds);
    auto line = std::wstring(stamp) + s + L"\r\n";
    SendMessageW(logBox, EM_SETSEL, WPARAM(-1), -1);
    SendMessageW(logBox, EM_REPLACESEL, FALSE, LPARAM(line.c_str()));
}
void status() {
    std::wstring s = connected                    ? L"연결됨 · 양방향 송수신 가능"
                     : peer != INVALID_SOCKET     ? L"연결 중..."
                     : listener != INVALID_SOCKET ? L"서버 대기 중"
                                                  : L"연결 안 됨";
    s += L"  |  수신 대기 " + std::to_wstring(decoder.pending()) + L" B  |  송신 큐 " +
         std::to_wstring(queued) + L" B";
    SetWindowTextW(statusBox, s.c_str());
    for (int id : {Send, Split, Burst, SendImage})
        EnableWindow(GetDlgItem(window, id), connected);
    for (int id : {Listen, Connect})
        EnableWindow(GetDlgItem(window, id), listener == INVALID_SOCKET && peer == INVALID_SOCKET);
}
void closeSocket(SOCKET &s) {
    if (s != INVALID_SOCKET) {
        WSAAsyncSelect(s, window, 0, 0);
        closesocket(s);
        s = INVALID_SOCKET;
    }
}
void disconnect() {
    if (decoder.pending())
        log(L"주의: 불완전한 프레임 " + std::to_wstring(decoder.pending()) + L" B 폐기");
    if (!acknowledgements.empty())
        log(L"주의: ACK 미확인 " + std::to_wstring(acknowledgements.size()) + L"건");
    closeSocket(peer);
    connected = false;
    outgoing.clear();
    queued = 0;
    decoder.clear();
    acknowledgements.clear();
    status();
}
void fail(const std::wstring &operation, int error) {
    log(operation + L" 실패 · Winsock 오류 " + std::to_wstring(error));
}
void attach(SOCKET s, bool ready) {
    peer = s;
    if (WSAAsyncSelect(peer, window, NetMessage, FD_CONNECT | FD_READ | FD_WRITE | FD_CLOSE)) {
        int error = WSAGetLastError();
        disconnect();
        fail(L"이벤트 등록", error);
        return;
    }
    connected = ready;
    status();
}
void start(bool server) {
    size_t used = 0;
    auto portText = content(portBox);
    int port = std::stoi(portText, &used);
    if (used != portText.size() || port < 1 || port > 65535)
        throw std::runtime_error("port must be 1..65535");
    sockaddr_in endpoint{};
    endpoint.sin_family = AF_INET;
    endpoint.sin_port = htons(u_short(port));
    if (InetPtonW(AF_INET, content(addressBox).c_str(), &endpoint.sin_addr) != 1)
        throw std::runtime_error("enter an IPv4 address, e.g. 127.0.0.1");
    SOCKET s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (s == INVALID_SOCKET) {
        fail(L"socket", WSAGetLastError());
        return;
    }
    if (server) {
        BOOL exclusive = TRUE;
        setsockopt(s, SOL_SOCKET, SO_EXCLUSIVEADDRUSE, reinterpret_cast<char *>(&exclusive),
                   sizeof(exclusive));
        if (bind(s, reinterpret_cast<sockaddr *>(&endpoint), sizeof(endpoint)) || listen(s, 1) ||
            WSAAsyncSelect(s, window, NetMessage, FD_ACCEPT)) {
            int error = WSAGetLastError();
            closesocket(s);
            fail(L"서버 시작", error);
            return;
        }
        listener = s;
        log(L"LISTEN " + content(addressBox) + L":" + portText);
    } else {
        attach(s, false);
        if (peer == INVALID_SOCKET)
            return;
        int result = connect(peer, reinterpret_cast<sockaddr *>(&endpoint), sizeof(endpoint));
        if (result == SOCKET_ERROR && WSAGetLastError() != WSAEWOULDBLOCK) {
            int error = WSAGetLastError();
            disconnect();
            fail(L"연결", error);
        } else {
            log(L"CONNECT " + content(addressBox) + L":" + portText);
            SetTimer(window, 2, 10000, nullptr);
        }
    }
    status();
}
std::wstring describe(const wire::Frame &f) {
    std::wstring value;
    if (f.type == wire::Text) {
        auto decoded = wide(std::string(f.payload.begin(), f.payload.end()));
        for (auto c : decoded) {
            if (c == L'\r')
                value += L"\\r";
            else if (c == L'\n')
                value += L"\\n";
            else if (c == 0)
                value += L"\\0";
            else
                value += c;
        }
    } else if (f.type == wire::Integer) {
        uint32_t bits = uint32_t(wire::get(f.payload.data(), 4));
        int64_t number = bits >= 0x80000000U ? int64_t(bits) - 0x100000000LL : bits;
        value = std::to_wstring(number);
    } else if (f.type == wire::Real) {
        uint64_t bits = wire::get(f.payload.data(), 8);
        double v;
        std::memcpy(&v, &bits, 8);
        std::wostringstream stream;
        stream << std::setprecision(17) << v;
        value = stream.str();
    } else if (f.type == wire::Image) {
        auto m = wire::imageInfo(f.payload);
        value = L"IMAGE " + std::to_wstring(m.width) + L" x " + std::to_wstring(m.height) + L" channels=" +
                std::to_wstring(m.channels) + L" bits=8 stride=" + std::to_wstring(m.stride) + L" pixels=" +
                std::to_wstring(m.size) + L" B " +
                (m.channels == 1   ? L"GRAY"
                 : m.channels == 3 ? L"RGB"
                                   : L"RGBA");
    } else
        value = L"상대측 프레임 해석 완료";
    return L"seq=" + std::to_wstring(f.sequence) + L" type=" + std::to_wstring(f.type) + L" length=" +
           std::to_wstring(f.payload.size()) + L" value=[" + value + L"]";
}
void flush(bool timer = false) {
    while (connected && !outgoing.empty()) {
        auto &q = outgoing.front();
        if (q.slow && !timer)
            break;
        size_t n = q.slow ? std::min<size_t>(3, q.data.size() - q.offset) : q.data.size() - q.offset;
        int sent = send(peer, reinterpret_cast<const char *>(q.data.data() + q.offset), int(n), 0);
        if (sent == SOCKET_ERROR) {
            int e = WSAGetLastError();
            if (e == WSAEWOULDBLOCK)
                break;
            fail(L"send", e);
            disconnect();
            return;
        }
        if (!sent) {
            log(L"send returned zero");
            disconnect();
            return;
        }
        q.offset += sent;
        queued -= sent;
        bool slow = q.slow;
        if (q.offset == q.data.size())
            outgoing.pop_front();
        if (slow)
            break;
    }
    status();
}
void enqueue(wire::Bytes data, bool slow = false) {
    if (queued + data.size() > MaxQueue)
        throw std::runtime_error("send queue full; wait until current image is sent");
    queued += data.size();
    outgoing.push_back({std::move(data), 0, slow});
}
void sendValue(int mode) {
    if (!connected)
        return;
    wire::Frame f{uint16_t(SendMessageW(typeBox, CB_GETCURSEL, 0, 0) + 1), 0, {}};
    auto input = content(valueBox);
    size_t used = 0;
    if (f.type == wire::Text) {
        auto s = utf(input);
        f.payload.assign(s.begin(), s.end());
    } else if (f.type == wire::Integer) {
        auto n = std::stoll(input, &used);
        if (used != input.size() || n < INT32_MIN || n > INT32_MAX)
            throw std::runtime_error("enter an Int32 (-2147483648..2147483647)");
        wire::put(f.payload, uint32_t(n), 4);
    } else {
        double n = std::stod(input, &used);
        if (used != input.size() || !std::isfinite(n))
            throw std::runtime_error("enter a finite Float64 value");
        uint64_t bits;
        std::memcpy(&bits, &n, 8);
        wire::put(f.payload, bits, 8);
    }
    wire::validate(f.type, f.payload.size());
    int count = mode == Burst ? 3 : 1;
    if (queued + (wire::HeaderSize + f.payload.size()) * count > 1024 * 1024 ||
        acknowledgements.size() + count > 4096)
        throw std::runtime_error("too many pending messages; wait for ACKs");
    wire::Bytes bytes;
    for (int i = 0; i < count; ++i) {
        f.sequence = sequence++;
        if (!sequence)
            sequence = 1;
        auto encoded = wire::encode(f);
        bytes.insert(bytes.end(), encoded.begin(), encoded.end());
        acknowledgements[f.sequence] = GetTickCount64();
        log(L"TX QUEUED " + describe(f));
        log(L"  HEX " + wide(wire::hex(encoded)));
    }
    enqueue(std::move(bytes), mode == Split);
    flush();
}
void showImage(const wire::Bytes &p, const wchar_t *direction) {
    auto bitmap = imaging::thumbnail(p);
    SendMessageW(previewBox, STM_SETIMAGE, IMAGE_BITMAP, LPARAM(bitmap));
    if (previewBitmap)
        DeleteObject(previewBitmap);
    previewBitmap = bitmap;
    auto description = std::wstring(direction) + L"\r\n" + describe({wire::Image, 0, p});
    SetWindowTextW(imageInfoBox, description.c_str());
}
void chooseImage() {
    wchar_t path[32768]{};
    OPENFILENAMEW ofn{};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = window;
    ofn.lpstrFile = path;
    ofn.nMaxFile = 32768;
    ofn.lpstrFilter = L"Images\0*.jpg;*.jpeg;*.png;*.bmp;*.tif;*.tiff\0All files\0*.*\0";
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_NOCHANGEDIR;
    if (GetOpenFileNameW(&ofn)) {
        auto p = imaging::load(path);
        showImage(p, L"선택 이미지 (전송 전)");
        SetWindowTextW(imagePathBox, path);
        log(L"IMAGE SELECT " + content(imagePathBox));
    }
}
void sendImage() {
    if (!connected)
        return;
    // Reload each time: replacing the file may change size or channel count.
    auto p = imaging::load(content(imagePathBox));
    if (queued + p.size() + wire::HeaderSize > MaxQueue || acknowledgements.size() >= 4096)
        throw std::runtime_error("send queue full; wait for previous transmission");
    wire::Frame f{wire::Image, sequence++, std::move(p)};
    if (!sequence)
        sequence = 1;
    auto bytes = wire::encode(f);
    enqueue(std::move(bytes));
    acknowledgements[f.sequence] = GetTickCount64();
    showImage(f.payload, L"송신 이미지");
    log(L"TX QUEUED " + describe(f));
    log(L"IMAGE metadata HEX " +
        wide(wire::hex(wire::Bytes(f.payload.begin(), f.payload.begin() + wire::ImageHeaderSize))));
    flush();
}
void saveImage() {
    if (receivedImage.empty())
        throw std::runtime_error("no received image yet");
    wchar_t path[32768] = L"received.png";
    OPENFILENAMEW ofn{};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = window;
    ofn.lpstrFile = path;
    ofn.nMaxFile = 32768;
    ofn.lpstrFilter = L"PNG\0*.png\0";
    ofn.lpstrDefExt = L"png";
    ofn.Flags = OFN_OVERWRITEPROMPT | OFN_NOCHANGEDIR;
    if (GetSaveFileNameW(&ofn)) {
        imaging::save(path, receivedImage);
        log(L"IMAGE SAVED " + std::wstring(path));
    }
}
void readPeer() {
    uint8_t chunk[65536];
    // Bound each dispatch so a busy sender does not monopolize the UI thread.
    for (int batch = 0; batch < 16 && connected; ++batch) {
        int n = recv(peer, reinterpret_cast<char *>(chunk), sizeof(chunk), 0);
        if (n == 0) {
            log(L"상대가 연결 종료");
            disconnect();
            return;
        }
        if (n < 0) {
            int e = WSAGetLastError();
            if (e != WSAEWOULDBLOCK) {
                fail(L"recv", e);
                disconnect();
            }
            return;
        }
        log(L"RX CHUNK " + std::to_wstring(n) + L" B (TCP 읽기 단위 ≠ 메시지 단위)");
        try {
            decoder.feed(chunk, n, [](const wire::Frame &f) {
                log(L"RX FRAME " + describe(f));
                if (f.type == wire::Image) {
                    showImage(f.payload, L"수신 이미지 (검증 완료)");
                    receivedImage = f.payload;
                    log(L"IMAGE metadata HEX " +
                        wide(wire::hex(
                            wire::Bytes(f.payload.begin(), f.payload.begin() + wire::ImageHeaderSize))));
                } else
                    log(L"  HEX " + wide(wire::hex(wire::encode(f))));
                if (f.type == wire::Ack) {
                    auto it = acknowledgements.find(f.sequence);
                    if (it != acknowledgements.end()) {
                        log(L"ACK 확인 seq=" + std::to_wstring(f.sequence) + L" · 큐 등록부터 " +
                            std::to_wstring(GetTickCount64() - it->second) + L" ms");
                        acknowledgements.erase(it);
                    } else
                        log(L"ACK: 대기 항목 없음 (지연 또는 중복)");
                } else {
                    wire::Frame ack{wire::Ack, f.sequence, {}};
                    auto bytes = wire::encode(ack);
                    log(L"TX ACK " + describe(ack));
                    log(L"  HEX " + wide(wire::hex(bytes)));
                    enqueue(std::move(bytes));
                }
            });
        } catch (const std::exception &e) {
            log(L"PROTOCOL ERROR: " + wide(e.what()) + L" · 연결 종료");
            disconnect();
            return;
        }
        flush();
    }
    status();
}
HWND control(const wchar_t *cls, const wchar_t *text, DWORD style, int x, int y, int w, int h, int id = 0) {
    HWND c = CreateWindowExW(cls == std::wstring(L"EDIT") ? WS_EX_CLIENTEDGE : 0, cls, text,
                             WS_CHILD | WS_VISIBLE | style, x, y, w, h, window,
                             reinterpret_cast<HMENU>(INT_PTR(id)), nullptr, nullptr);
    SendMessageW(c, WM_SETFONT, WPARAM(font), TRUE);
    return c;
}
void saveLog() {
    wchar_t path[MAX_PATH] = L"tcp-test.log";
    OPENFILENAMEW ofn{};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = window;
    ofn.lpstrFile = path;
    ofn.nMaxFile = MAX_PATH;
    ofn.lpstrFilter = L"UTF-8 Log\0*.log\0";
    ofn.lpstrDefExt = L"log";
    ofn.Flags = OFN_OVERWRITEPROMPT;
    if (GetSaveFileNameW(&ofn)) {
        std::ofstream file(std::filesystem::path(path), std::ios::binary);
        file << utf(content(logBox));
        if (!file)
            throw std::runtime_error("could not save log");
    }
}
LRESULT CALLBACK procedure(HWND h, UINT msg, WPARAM w, LPARAM l) {
    try {
        switch (msg) {
            case WM_CREATE: {
                window = h;
                font = CreateFontW(-16, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, 0, 0,
                                   CLEARTYPE_QUALITY, 0, L"Malgun Gothic");
                control(L"STATIC", L"TCP / IP TESTER   ·   두 프로세스 간 프로토콜 실험", 0, 16, 12, 850, 26);
                control(L"STATIC", L"IPv4", 0, 16, 53, 40, 24);
                addressBox = control(L"EDIT", L"127.0.0.1", ES_AUTOHSCROLL | WS_TABSTOP, 60, 48, 160, 28);
                control(L"STATIC", L"Port", 0, 232, 53, 38, 24);
                portBox = control(L"EDIT", L"9001", ES_NUMBER | WS_TABSTOP, 272, 48, 72, 28);
                control(L"BUTTON", L"서버 대기", WS_TABSTOP, 356, 47, 112, 30, Listen);
                control(L"BUTTON", L"클라이언트 연결", WS_TABSTOP, 476, 47, 146, 30, Connect);
                control(L"BUTTON", L"중지", WS_TABSTOP, 630, 47, 70, 30, Stop);
                control(L"BUTTON", L"상대 창 열기", WS_TABSTOP, 708, 47, 130, 30, Peer);
                statusBox = control(L"STATIC", L"", 0, 16, 88, 860, 26);
                typeBox = control(L"COMBOBOX", L"", CBS_DROPDOWNLIST | WS_TABSTOP, 16, 126, 170, 140);
                for (auto name : {L"1 · UTF-8 문자열", L"2 · Int32 정수", L"3 · Float64 실수"})
                    SendMessageW(typeBox, CB_ADDSTRING, 0, LPARAM(name));
                SendMessageW(typeBox, CB_SETCURSEL, 0, 0);
                valueBox = control(L"EDIT", L"안녕하세요! TCP 테스트", ES_AUTOHSCROLL | WS_TABSTOP, 196, 126,
                                   642, 30);
                SendMessageW(valueBox, EM_SETLIMITTEXT, 16000, 0);
                control(L"BUTTON", L"전송", WS_TABSTOP, 16, 168, 100, 32, Send);
                control(L"BUTTON", L"3바이트씩 분할 전송", WS_TABSTOP, 124, 168, 204, 32, Split);
                control(L"BUTTON", L"3개 프레임 묶음 전송", WS_TABSTOP, 336, 168, 208, 32, Burst);
                control(L"BUTTON", L"로그 지우기", WS_TABSTOP, 552, 168, 134, 32, Clear);
                control(L"BUTTON", L"로그 저장", WS_TABSTOP, 694, 168, 144, 32, Save);
                control(L"STATIC", L"헤더 16 B: TCPT(4) | 버전(2) | 종류(2) | 순번(4) | 본문 길이(4) → 본문",
                        0, 16, 214, 860, 24);
                control(L"STATIC",
                        L"숫자: Big-endian  ·  문자열: UTF-8  ·  ACK: 상대가 해석 완료  ·  분할: 100ms마다 "
                        L"최대 3 B",
                        0, 16, 240, 880, 24);
                imagePathBox = control(L"EDIT", L"", ES_AUTOHSCROLL | ES_READONLY, 16, 274, 520, 28);
                control(L"BUTTON", L"사진 선택", WS_TABSTOP, 544, 274, 94, 30, PickImage);
                control(L"BUTTON", L"사진 전송", WS_TABSTOP, 646, 274, 94, 30, SendImage);
                control(L"BUTTON", L"수신 PNG 저장", WS_TABSTOP, 748, 274, 140, 30, SaveImage);
                previewBox = control(L"STATIC", L"", SS_BITMAP | SS_CENTERIMAGE, 16, 312, 250, 140);
                imageInfoBox = control(
                    L"STATIC",
                    L"사진 선택 후 전송하세요.\r\nGRAY(1), RGB(3), RGBA(4) / 8 bit / 최대 픽셀 64 MiB", 0,
                    282, 316, 600, 130);
                wchar_t exe[32768];
                GetModuleFileNameW(nullptr, exe, 32768);
                auto folder = std::filesystem::path(exe).parent_path();
                for (int i = 0; i < 6; ++i) {
                    auto sample = folder / L"000028.JPG";
                    if (std::filesystem::exists(sample)) {
                        SetWindowTextW(imagePathBox, sample.c_str());
                        break;
                    }
                    folder = folder.parent_path();
                }
                logBox = control(L"EDIT", L"",
                                 ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL | ES_AUTOHSCROLL | WS_VSCROLL |
                                     WS_HSCROLL | WS_TABSTOP,
                                 16, 466, 870, 230);
                SendMessageW(logBox, EM_SETLIMITTEXT, 1000000, 0);
                log(L"① 이 창에서 서버 대기 → ② 상대 창 열기 → ③ 상대 창에서 클라이언트 연결 → 양쪽에서 "
                    L"전송");
                SetTimer(h, 1, 100, nullptr);
                status();
                return 0;
            }
            case WM_GETMINMAXINFO:
                reinterpret_cast<MINMAXINFO *>(l)->ptMinTrackSize = {930, 700};
                return 0;
            case WM_SIZE:
                if (logBox)
                    MoveWindow(logBox, 16, 466, std::max(100, int(LOWORD(l)) - 32),
                               std::max(80, int(HIWORD(l)) - 482), TRUE);
                return 0;
            case WM_COMMAND:
                if (HIWORD(w) != BN_CLICKED)
                    break;
                switch (LOWORD(w)) {
                    case Listen:
                        start(true);
                        break;
                    case Connect:
                        start(false);
                        break;
                    case Stop:
                        KillTimer(h, 2);
                        closeSocket(listener);
                        disconnect();
                        log(L"중지 완료");
                        break;
                    case Send:
                    case Split:
                    case Burst:
                        sendValue(LOWORD(w));
                        break;
                    case Clear:
                        SetWindowTextW(logBox, L"");
                        break;
                    case Save:
                        saveLog();
                        break;
                    case PickImage:
                        chooseImage();
                        break;
                    case SendImage:
                        sendImage();
                        break;
                    case SaveImage:
                        saveImage();
                        break;
                    case Peer: {
                        wchar_t path[32768];
                        GetModuleFileNameW(nullptr, path, 32768);
                        STARTUPINFOW si{};
                        si.cb = sizeof(si);
                        PROCESS_INFORMATION pi{};
                        if (CreateProcessW(path, nullptr, nullptr, nullptr, FALSE, 0, nullptr, nullptr, &si,
                                           &pi)) {
                            CloseHandle(pi.hThread);
                            CloseHandle(pi.hProcess);
                        } else
                            throw std::runtime_error("could not launch peer process");
                        break;
                    }
                }
                return 0;
            case WM_TIMER:
                if (w == 2) {
                    KillTimer(h, 2);
                    if (!connected && peer != INVALID_SOCKET) {
                        log(L"연결 시간 초과 (10초)");
                        disconnect();
                    }
                }
                if (w == 1) {
                    flush(true);
                    for (auto it = acknowledgements.begin(); it != acknowledgements.end();) {
                        if (GetTickCount64() - it->second > 120000) {
                            log(L"ACK 시간 초과 seq=" + std::to_wstring(it->first) +
                                L" (120초, 전송 큐 대기 포함)");
                            it = acknowledgements.erase(it);
                        } else
                            ++it;
                    }
                }
                return 0;
            case NetMessage: {
                SOCKET s = SOCKET(w);
                int event = WSAGETSELECTEVENT(l), error = WSAGETSELECTERROR(l);
                if (s != peer && s != listener)
                    return 0;
                if (error) {
                    fail(L"네트워크 이벤트", error);
                    if (s == peer)
                        disconnect();
                    return 0;
                }
                if (s == listener && event == FD_ACCEPT) {
                    SOCKET accepted = accept(listener, nullptr, nullptr);
                    if (accepted == INVALID_SOCKET)
                        return 0;
                    if (peer != INVALID_SOCKET) {
                        closesocket(accepted);
                        log(L"추가 연결 거절: 1:1 시험");
                    } else {
                        attach(accepted, true);
                        log(L"클라이언트 접속");
                    }
                    return 0;
                }
                if (s != peer)
                    return 0;
                if (event == FD_CONNECT) {
                    KillTimer(h, 2);
                    connected = true;
                    log(L"서버 연결 완료");
                    status();
                }
                if (event == FD_READ || event == FD_CLOSE)
                    readPeer();
                if (event == FD_WRITE)
                    flush();
                return 0;
            }
            case WM_DESTROY:
                closeSocket(listener);
                disconnect();
                if (previewBitmap)
                    DeleteObject(previewBitmap);
                DeleteObject(font);
                PostQuitMessage(0);
                return 0;
        }
    } catch (const std::exception &e) {
        if (logBox)
            log(L"ERROR: " + wide(e.what()));
    }
    return DefWindowProcW(h, msg, w, l);
}
int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int show) {
    static_assert(sizeof(double) == 8 && std::numeric_limits<double>::is_iec559);
    WSADATA data;
    if (WSAStartup(MAKEWORD(2, 2), &data))
        return 1;
    if (FAILED(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED))) {
        WSACleanup();
        return 1;
    }
    SetProcessDPIAware();
    WNDCLASSW cls{};
    cls.hInstance = instance;
    cls.lpfnWndProc = procedure;
    cls.lpszClassName = L"TcpIpTesterWindow";
    cls.hCursor = LoadCursor(nullptr, IDC_ARROW);
    cls.hbrBackground = HBRUSH(COLOR_BTNFACE + 1);
    RegisterClassW(&cls);
    HWND h = CreateWindowW(cls.lpszClassName, L"TCP/IP Tester · C++ / Winsock2", WS_OVERLAPPEDWINDOW,
                           CW_USEDEFAULT, CW_USEDEFAULT, 950, 740, nullptr, nullptr, instance, nullptr);
    if (!h) {
        WSACleanup();
        return 1;
    }
    ShowWindow(h, show);
    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        if (!IsDialogMessageW(h, &msg)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }
    CoUninitialize();
    WSACleanup();
    return int(msg.wParam);
}
