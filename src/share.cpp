// まなびと講習準備 ― LAN 内の授業ファイル配布（share.h を参照）

#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#define _CRT_RAND_S  // rand_s を使う（Visual Studio ではこれが要る）

#include <winsock2.h>
#include <ws2tcpip.h>
#include <stdlib.h>

#include "share.h"

#include <iphlpapi.h>
#include <commctrl.h>
#include <shellapi.h>
#include <shlobj.h>
#include <shobjidl.h>
#include <netlistmgr.h>

#include <algorithm>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace share {

void StartDownload(HWND owner, const Offer& offer, const std::wstring& pass, const std::wstring& dest);

namespace {

const wchar_t kAppName[] = L"まなびと講習準備";
const u_short kUdpPort = 47811;
const u_short kTcpPort = 47812;
const char kQuery[] = "MNBSHARE?1";
const char kAnswerHead[] = "MNBSHARE!1|";

// ---------------------------------------------------------------- 小さな道具

struct Lock {
    CRITICAL_SECTION* cs;
    explicit Lock(CRITICAL_SECTION* c) : cs(c) { EnterCriticalSection(cs); }
    ~Lock() { LeaveCriticalSection(cs); }
};

std::string ToUtf8(const std::wstring& w) {
    if (w.empty()) return std::string();
    int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), static_cast<int>(w.size()), nullptr, 0, nullptr, nullptr);
    std::string s(static_cast<size_t>(n), '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.c_str(), static_cast<int>(w.size()), &s[0], n, nullptr, nullptr);
    return s;
}

std::wstring FromUtf8(const std::string& s) {
    if (s.empty()) return std::wstring();
    int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), static_cast<int>(s.size()), nullptr, 0);
    std::wstring w(static_cast<size_t>(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), static_cast<int>(s.size()), &w[0], n);
    return w;
}

std::vector<std::string> SplitBar(const std::string& s) {
    std::vector<std::string> f;
    size_t a = 0;
    for (;;) {
        size_t b = s.find('|', a);
        f.push_back(s.substr(a, b == std::string::npos ? std::string::npos : b - a));
        if (b == std::string::npos) break;
        a = b + 1;
    }
    return f;
}

std::wstring ComputerName() {
    wchar_t buf[MAX_COMPUTERNAME_LENGTH + 2];
    DWORD n = MAX_COMPUTERNAME_LENGTH + 1;
    if (GetComputerNameW(buf, &n)) return std::wstring(buf, n);
    return L"PC";
}

std::wstring FormatBytes(unsigned long long b) {
    wchar_t buf[64];
    if (b >= (1ull << 30)) swprintf(buf, 64, L"%.2f GB", static_cast<double>(b) / (1ull << 30));
    else if (b >= (1ull << 20)) swprintf(buf, 64, L"%.1f MB", static_cast<double>(b) / (1ull << 20));
    else swprintf(buf, 64, L"%llu KB", (b + 1023) / 1024);
    return buf;
}

std::wstring FormatSeconds(double s) {
    if (s < 0) s = 0;
    unsigned long n = static_cast<unsigned long>(s + 0.5);
    wchar_t buf[64];
    if (n < 60) swprintf(buf, 64, L"%lu 秒", n);
    else if (n < 3600) swprintf(buf, 64, L"%lu 分 %lu 秒", n / 60, n % 60);
    else swprintf(buf, 64, L"%lu 時間 %lu 分", n / 3600, (n % 3600) / 60);
    return buf;
}

HFONT MakeFont(HWND h, int tenthPt, bool bold) {
    UINT dpi = 96;
    HDC dc = GetDC(h);
    if (dc) {
        dpi = static_cast<UINT>(GetDeviceCaps(dc, LOGPIXELSY));
        ReleaseDC(h, dc);
    }
    typedef UINT(WINAPI * PFN)(HWND);
    PFN f = reinterpret_cast<PFN>(reinterpret_cast<void*>(GetProcAddress(GetModuleHandleW(L"user32.dll"), "GetDpiForWindow")));
    if (f && h) {
        UINT d = f(h);
        if (d) dpi = d;
    }
    NONCLIENTMETRICSW ncm{};
    ncm.cbSize = sizeof(ncm);
    SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof(ncm), &ncm, 0);
    LOGFONTW lf = ncm.lfMessageFont;
    lf.lfHeight = -MulDiv(tenthPt, static_cast<int>(dpi), 720);
    lf.lfWeight = bold ? FW_BOLD : FW_NORMAL;
    lf.lfQuality = CLEARTYPE_QUALITY;
    return CreateFontIndirectW(&lf);
}

int Dpi(HWND h) {
    typedef UINT(WINAPI * PFN)(HWND);
    PFN f = reinterpret_cast<PFN>(reinterpret_cast<void*>(GetProcAddress(GetModuleHandleW(L"user32.dll"), "GetDpiForWindow")));
    if (f && h) {
        UINT d = f(h);
        if (d) return static_cast<int>(d);
    }
    return 96;
}

void SetFontAll(HWND parent, HFONT f) {
    EnumChildWindows(
        parent, [](HWND c, LPARAM lp) -> BOOL {
            SendMessageW(c, WM_SETFONT, static_cast<WPARAM>(lp), TRUE);
            return TRUE;
        },
        reinterpret_cast<LPARAM>(f));
}

// ---- ソケット：1 行ずつ読む／決まった長さを読む
struct Conn {
    SOCKET s = INVALID_SOCKET;
    std::string buf;  // 読みすぎた分

    bool SendAll(const char* p, size_t n) {
        while (n > 0) {
            int k = send(s, p, static_cast<int>(std::min<size_t>(n, 1 << 20)), 0);
            if (k <= 0) return false;
            p += k;
            n -= static_cast<size_t>(k);
        }
        return true;
    }
    bool SendLine(const std::string& line) {
        std::string t = line + "\n";
        return SendAll(t.data(), t.size());
    }
    bool ReadLine(std::string& out, size_t limit = 64 * 1024) {
        for (;;) {
            size_t nl = buf.find('\n');
            if (nl != std::string::npos) {
                out = buf.substr(0, nl);
                if (!out.empty() && out.back() == '\r') out.pop_back();
                buf.erase(0, nl + 1);
                return true;
            }
            if (buf.size() > limit) return false;
            char tmp[8192];
            int k = recv(s, tmp, sizeof(tmp), 0);
            if (k <= 0) return false;
            buf.append(tmp, static_cast<size_t>(k));
        }
    }
    // 中身を受け取って h に書く（進み具合は onChunk で知らせる。false を返すと打ち切り）
    template <typename F>
    bool ReadBody(unsigned long long size, HANDLE h, F onChunk) {
        unsigned long long left = size;
        if (!buf.empty()) {
            size_t take = static_cast<size_t>(std::min<unsigned long long>(left, buf.size()));
            DWORD w = 0;
            if (take && !WriteFile(h, buf.data(), static_cast<DWORD>(take), &w, nullptr)) return false;
            buf.erase(0, take);
            left -= take;
            if (!onChunk(take)) return false;
        }
        std::vector<char> chunk(256 * 1024);
        while (left > 0) {
            int want = static_cast<int>(std::min<unsigned long long>(left, chunk.size()));
            int k = recv(s, chunk.data(), want, 0);
            if (k <= 0) return false;
            DWORD w = 0;
            if (!WriteFile(h, chunk.data(), static_cast<DWORD>(k), &w, nullptr)) return false;
            left -= static_cast<unsigned long long>(k);
            if (!onChunk(static_cast<size_t>(k))) return false;
        }
        return true;
    }
    void Close() {
        if (s != INVALID_SOCKET) {
            shutdown(s, SD_BOTH);
            closesocket(s);
            s = INVALID_SOCKET;
        }
    }
};

// 受け取る側の守り：サーバーから届いた相対パスが、受け取るフォルダーの外を指していないか
bool SafeRelPath(const std::wstring& p) {
    if (p.empty() || p.size() > 400) return false;
    if (p[0] == L'\\' || p[0] == L'/') return false;
    for (wchar_t c : p)
        if (c < 0x20 || c == L':' || c == L'*' || c == L'?' || c == L'"' || c == L'<' || c == L'>' || c == L'|' || c == L'/')
            return false;
    size_t a = 0;
    for (;;) {
        size_t b = p.find(L'\\', a);
        std::wstring seg = p.substr(a, b == std::wstring::npos ? std::wstring::npos : b - a);
        if (seg.empty() || seg == L"." || seg == L"..") return false;
        if (seg.back() == L'.' || seg.back() == L' ') return false;
        if (b == std::wstring::npos) break;
        a = b + 1;
    }
    return true;
}

bool MakeDirs(const std::wstring& dir) {
    int r = SHCreateDirectoryExW(nullptr, dir.c_str(), nullptr);
    return r == ERROR_SUCCESS || r == ERROR_ALREADY_EXISTS || r == ERROR_FILE_EXISTS;
}

std::wstring AppDataDir() {
    wchar_t local[MAX_PATH];
    DWORD n = GetEnvironmentVariableW(L"LOCALAPPDATA", local, MAX_PATH);
    std::wstring d = (n > 0 && n < MAX_PATH) ? std::wstring(local) : std::wstring(L".");
    return d + L"\\ManabitoClassSetup";
}

// ---------------------------------------------------------------- 状態

HINSTANCE g_inst = nullptr;
HWND g_owner = nullptr;
UINT g_foundMsg = 0;
bool g_wsa = false;
volatile LONG g_stop = 0;
HANDLE g_discoverThread = nullptr;
CRITICAL_SECTION g_cs;  // 下のものを守る
std::set<unsigned> g_handled;
unsigned g_pending = 0;

// 配布サーバー
struct Entry {
    std::wstring rel, full;
    unsigned long long size = 0;
    bool dir = false;
};
struct Source {
    std::wstring path, name;  // ドロップされた場所、配るときの名前
    unsigned long long size = 0;
    int files = 0;
};
struct ClientInfo {
    std::wstring name, ip, state;
    unsigned long long sent = 0, total = 0;
    DWORD lastTick = 0;
};
HWND g_server = nullptr;
volatile LONG g_serverStop = 0;
SOCKET g_udpServer = INVALID_SOCKET, g_tcpServer = INVALID_SOCKET;
HANDLE g_udpThread = nullptr, g_tcpThread = nullptr;
std::vector<Source> g_sources;
std::vector<Entry> g_entries;
unsigned long long g_totalBytes = 0;
int g_totalFiles = 0;
unsigned g_session = 0;
std::wstring g_passcode;
std::map<unsigned long, ClientInfo> g_clients;  // 接続の番号ごと
unsigned long g_nextClient = 1;
std::map<std::wstring, std::pair<int, DWORD>> g_fails;  // IP ごとの合言葉の間違い（回数、最後の時刻）
// インストール元（合言葉なしで配る大きなもの）：0 = Office、1 = Adobe Creative Cloud（ランチャー）
struct Package {
    const wchar_t* label;   // 画面の名前
    const wchar_t* folder;  // %LOCALAPPDATA%\ManabitoClassSetup\ の下の置き場所
    const wchar_t* top;     // 置き場所の中の、配るフォルダー（相対パスの先頭）
    const wchar_t* script;  // 用意する部品
    const char* tag;        // UDP の答えの名前
    const char* listCmd;    // 一覧を求める命令
    const char* getCmd;     // 中身を求める命令
    std::vector<Entry> entries;
    std::wstring version;
    unsigned long long bytes = 0;
    HANDLE prep = nullptr;  // 用意しているときの PowerShell
    std::wstring statusFile, text;
};
Package g_pkgs[2] = {
    {L"Office", L"OfficeSource", L"Office", L"office_source.ps1", "office", "OFFICE", "OGET", {}, L"", 0, nullptr, L"", L""},
    {L"Creative Cloud（ランチャー）", L"AdobeSource", L"Adobe", L"adobe_source.ps1", "adobe", "ADOBE", "AGET", {}, L"", 0, nullptr, L"", L""},
};
const int kPkgCount = 2;
std::wstring g_profilePath;

unsigned RandomU32() {
    unsigned v = 0;
    if (rand_s(&v) != 0) v = GetTickCount() ^ (GetCurrentProcessId() << 16);
    return v;
}

// ---------------------------------------------------------------- 受け取る側：配布サーバーを探す

std::vector<sockaddr_in> BroadcastTargets() {
    std::vector<sockaddr_in> out;
    sockaddr_in all{};
    all.sin_family = AF_INET;
    all.sin_port = htons(kUdpPort);
    all.sin_addr.s_addr = INADDR_BROADCAST;
    out.push_back(all);
    // 各ネットワークの区切りごとのブロードキャスト（255.255.255.255 だけでは届かない環境があるため）
    ULONG size = 16 * 1024;
    std::vector<BYTE> buf(size);
    ULONG r = GetAdaptersAddresses(AF_INET, GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST | GAA_FLAG_SKIP_DNS_SERVER, nullptr,
                                   reinterpret_cast<IP_ADAPTER_ADDRESSES*>(buf.data()), &size);
    if (r == ERROR_BUFFER_OVERFLOW) {
        buf.resize(size);
        r = GetAdaptersAddresses(AF_INET, GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST | GAA_FLAG_SKIP_DNS_SERVER, nullptr,
                                 reinterpret_cast<IP_ADAPTER_ADDRESSES*>(buf.data()), &size);
    }
    if (r != NO_ERROR) return out;
    for (IP_ADAPTER_ADDRESSES* a = reinterpret_cast<IP_ADAPTER_ADDRESSES*>(buf.data()); a; a = a->Next) {
        if (a->OperStatus != IfOperStatusUp || a->IfType == IF_TYPE_SOFTWARE_LOOPBACK) continue;
        for (IP_ADAPTER_UNICAST_ADDRESS* u = a->FirstUnicastAddress; u; u = u->Next) {
            if (u->Address.lpSockaddr->sa_family != AF_INET) continue;
            ULONG ip = ntohl(reinterpret_cast<sockaddr_in*>(u->Address.lpSockaddr)->sin_addr.s_addr);
            UINT8 len = u->OnLinkPrefixLength;
            if (len == 0 || len >= 31) continue;
            ULONG mask = len >= 32 ? 0xFFFFFFFFu : (0xFFFFFFFFu << (32 - len));
            sockaddr_in t{};
            t.sin_family = AF_INET;
            t.sin_port = htons(kUdpPort);
            t.sin_addr.s_addr = htonl((ip & mask) | ~mask);
            out.push_back(t);
        }
    }
    return out;
}

DWORD WINAPI DiscoverThread(LPVOID) {
    SOCKET s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (s == INVALID_SOCKET) return 0;
    BOOL yes = TRUE;
    setsockopt(s, SOL_SOCKET, SO_BROADCAST, reinterpret_cast<const char*>(&yes), sizeof(yes));
    sockaddr_in me{};
    me.sin_family = AF_INET;
    bind(s, reinterpret_cast<sockaddr*>(&me), sizeof(me));
    const std::string self = ToUtf8(ComputerName());
    while (!g_stop) {
        if (!IsServerRunning()) {
            for (const auto& t : BroadcastTargets())
                sendto(s, kQuery, static_cast<int>(sizeof(kQuery) - 1), 0, reinterpret_cast<const sockaddr*>(&t), sizeof(t));
        }
        // 5 秒のあいだ返事を待つ
        DWORD until = GetTickCount() + 5000;
        while (!g_stop && static_cast<LONG>(until - GetTickCount()) > 0) {
            fd_set fs;
            FD_ZERO(&fs);
            FD_SET(s, &fs);
            timeval tv{0, 500000};
            if (select(0, &fs, nullptr, nullptr, &tv) <= 0) continue;
            char buf[1024];
            sockaddr_in from{};
            int fl = sizeof(from);
            int k = recvfrom(s, buf, sizeof(buf) - 1, 0, reinterpret_cast<sockaddr*>(&from), &fl);
            if (k <= 0) continue;
            std::string msg(buf, static_cast<size_t>(k));
            if (msg.rfind(kAnswerHead, 0) != 0) continue;
            auto f = SplitBar(msg);
            if (f.size() < 6) continue;
            if (f[1] == self) continue;  // 自分自身
            if (atoi(f[4].c_str()) <= 0) continue;  // 授業ファイルを配っていない（Office のインストール元だけ）
            unsigned session = static_cast<unsigned>(strtoul(f[3].c_str(), nullptr, 10));
            {
                Lock l(&g_cs);
                if (g_handled.count(session) || g_pending) continue;
                g_pending = session;
            }
            Offer* o = new Offer();
            o->server = FromUtf8(f[1]);
            char ip[64] = "";
            inet_ntop(AF_INET, &from.sin_addr, ip, sizeof(ip));
            o->ip = FromUtf8(ip);
            o->port = atoi(f[2].c_str());
            o->session = session;
            o->files = atoi(f[4].c_str());
            o->bytes = _strtoui64(f[5].c_str(), nullptr, 10);
            if (!PostMessageW(g_owner, g_foundMsg, 0, reinterpret_cast<LPARAM>(o))) {
                delete o;
                Lock l(&g_cs);
                g_pending = 0;
            }
        }
    }
    closesocket(s);
    return 0;
}

void MarkHandled(unsigned session) {
    Lock l(&g_cs);
    g_handled.insert(session);
    if (g_pending == session) g_pending = 0;
}

// ---------------------------------------------------------------- 配布する側：一覧を作る

std::wstring CleanName(std::wstring n) {
    for (wchar_t& c : n)
        if (c < 0x20 || c == L'\\' || c == L'/' || c == L':' || c == L'*' || c == L'?' || c == L'"' || c == L'<' || c == L'>' || c == L'|')
            c = L'_';
    while (!n.empty() && (n.back() == L'.' || n.back() == L' ')) n.pop_back();
    if (n.empty()) n = L"配布";
    return n;
}

void Walk(const std::wstring& dir, const std::wstring& rel, std::vector<Entry>& out, unsigned long long& bytes, int& files) {
    WIN32_FIND_DATAW fd{};
    HANDLE h = FindFirstFileExW((dir + L"\\*").c_str(), FindExInfoBasic, &fd, FindExSearchNameMatch, nullptr, FIND_FIRST_EX_LARGE_FETCH);
    if (h == INVALID_HANDLE_VALUE) return;
    do {
        std::wstring n = fd.cFileName;
        if (n == L"." || n == L"..") continue;
        // Windows 用の隠しファイル・リンクは配らない（System Volume Information、$RECYCLE.BIN など）
        if ((fd.dwFileAttributes & FILE_ATTRIBUTE_SYSTEM) && (fd.dwFileAttributes & FILE_ATTRIBUTE_HIDDEN)) continue;
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) continue;
        if (!n.empty() && n[0] == L'$') continue;
        Entry e;
        e.rel = rel + L"\\" + n;
        e.full = dir + L"\\" + n;
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            e.dir = true;
            out.push_back(e);
            Walk(e.full, e.rel, out, bytes, files);
        } else {
            e.size = (static_cast<unsigned long long>(fd.nFileSizeHigh) << 32) | fd.nFileSizeLow;
            bytes += e.size;
            ++files;
            out.push_back(e);
        }
    } while (FindNextFileW(h, &fd));
    FindClose(h);
}

// g_sources から g_entries を作り直す（呼ぶ側が g_cs を持つ）
void RebuildEntries() {
    g_entries.clear();
    g_totalBytes = 0;
    g_totalFiles = 0;
    for (auto& src : g_sources) {
        unsigned long long b = 0;
        int f = 0;
        DWORD a = GetFileAttributesW(src.path.c_str());
        if (a == INVALID_FILE_ATTRIBUTES) continue;
        if (a & FILE_ATTRIBUTE_DIRECTORY) {
            Entry top;
            top.rel = src.name;
            top.full = src.path;
            top.dir = true;
            g_entries.push_back(top);
            Walk(src.path, src.name, g_entries, b, f);
        } else {
            WIN32_FILE_ATTRIBUTE_DATA d{};
            GetFileAttributesExW(src.path.c_str(), GetFileExInfoStandard, &d);
            Entry e;
            e.rel = src.name;
            e.full = src.path;
            e.size = (static_cast<unsigned long long>(d.nFileSizeHigh) << 32) | d.nFileSizeLow;
            b = e.size;
            f = 1;
            g_entries.push_back(e);
        }
        src.size = b;
        src.files = f;
        g_totalBytes += b;
        g_totalFiles += f;
    }
    g_session = RandomU32() | 1u;  // 中身が変わったら、受け取る側にもう一度知らせる
}

std::wstring NameForDrop(const std::wstring& path) {
    std::wstring p = path;
    if (p.size() == 3 && p[1] == L':' && p[2] == L'\\') {
        wchar_t label[MAX_PATH] = L"";
        GetVolumeInformationW(p.c_str(), label, MAX_PATH, nullptr, nullptr, nullptr, nullptr, 0);
        std::wstring n = label[0] ? std::wstring(label) : std::wstring(1, p[0]) + L" ドライブ";
        return CleanName(n);
    }
    while (!p.empty() && p.back() == L'\\') p.pop_back();
    size_t s = p.find_last_of(L'\\');
    return CleanName(s == std::wstring::npos ? p : p.substr(s + 1));
}

std::wstring PkgDir(int k) { return AppDataDir() + L"\\" + g_pkgs[k].folder; }

// 版の名前：Office は Office\Data\<版>\ のフォルダー名、Adobe は ZIP の名前（ACCCx6_11_0_478_6.zip → 6.11.0.478.6）
std::wstring FindPkgVersion(int k, const std::wstring& top) {
    std::wstring ver;
    WIN32_FIND_DATAW fd{};
    std::wstring pattern = (k == 0) ? top + L"\\Data\\*" : top + L"\\ACCCx*.zip";
    HANDLE h = FindFirstFileW(pattern.c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return ver;
    do {
        std::wstring n = fd.cFileName;
        if (k == 0) {
            if ((fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) && n.size() > 6 && iswdigit(n[0]) && std::count(n.begin(), n.end(), L'.') == 3 && n > ver)
                ver = n;
        } else if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) {
            std::wstring v = n.substr(5, n.size() - 5 - 4);  // "ACCCx" と ".zip" を除く
            std::replace(v.begin(), v.end(), L'_', L'.');
            if (!v.empty() && iswdigit(v[0]) && v > ver) ver = v;
        }
    } while (FindNextFileW(h, &fd));
    FindClose(h);
    return ver;
}

// インストール元を調べ直す（呼ぶ側が g_cs を持たない）
void ScanPackage(int k) {
    std::vector<Entry> list;
    unsigned long long bytes = 0;
    int files = 0;
    std::wstring top = PkgDir(k) + L"\\" + g_pkgs[k].top;
    std::wstring ver;
    if (GetFileAttributesW(top.c_str()) != INVALID_FILE_ATTRIBUTES) {
        Entry t;
        t.rel = g_pkgs[k].top;
        t.full = top;
        t.dir = true;
        list.push_back(t);
        Walk(top, g_pkgs[k].top, list, bytes, files);
        ver = FindPkgVersion(k, top);
    }
    Lock l(&g_cs);
    if (ver.empty() || files == 0) {
        g_pkgs[k].entries.clear();
        g_pkgs[k].version.clear();
        g_pkgs[k].bytes = 0;
    } else {
        g_pkgs[k].entries = list;
        g_pkgs[k].version = ver;
        g_pkgs[k].bytes = bytes;
    }
}

// ---------------------------------------------------------------- 配布する側：通信

DWORD WINAPI UdpServerThread(LPVOID) {
    const std::string self = ToUtf8(ComputerName());
    while (!g_serverStop) {
        fd_set fs;
        FD_ZERO(&fs);
        FD_SET(g_udpServer, &fs);
        timeval tv{0, 500000};
        if (select(0, &fs, nullptr, nullptr, &tv) <= 0) continue;
        char buf[256];
        sockaddr_in from{};
        int fl = sizeof(from);
        int k = recvfrom(g_udpServer, buf, sizeof(buf), 0, reinterpret_cast<sockaddr*>(&from), &fl);
        if (k <= 0) continue;
        if (std::string(buf, static_cast<size_t>(k)) != kQuery) continue;
        std::string answer;
        {
            Lock l(&g_cs);
            bool anyPkg = false;
            for (int pk = 0; pk < kPkgCount; ++pk) anyPkg = anyPkg || !g_pkgs[pk].version.empty();
            if (g_totalFiles == 0 && !anyPkg) continue;  // 配るものが無いときは返事をしない
            answer = std::string(kAnswerHead) + self + "|" + std::to_string(kTcpPort) + "|" + std::to_string(g_session) + "|" +
                     std::to_string(g_totalFiles) + "|" + std::to_string(g_totalBytes);
            for (int pk = 0; pk < kPkgCount; ++pk)  // |office=版|バイト|adobe=版|バイト
                answer += std::string("|") + g_pkgs[pk].tag + "=" + ToUtf8(g_pkgs[pk].version) + "|" + std::to_string(g_pkgs[pk].bytes);
        }
        sendto(g_udpServer, answer.data(), static_cast<int>(answer.size()), 0, reinterpret_cast<sockaddr*>(&from), fl);
    }
    return 0;
}

struct ClientJob {
    SOCKET s;
    std::wstring ip;
};

// 1 つのファイルを送る（DATA|大きさ のあと中身）。つながりが切れたら false
bool SendFileTo(Conn& c, const std::wstring& full, unsigned long id) {
    HANDLE h = CreateFileW(full.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
    if (h == INVALID_HANDLE_VALUE) return c.SendLine("ERR|配布する側でファイルを開けませんでした（USB メモリを抜いた、など）。");
    LARGE_INTEGER sz{};
    GetFileSizeEx(h, &sz);
    if (!c.SendLine("DATA|" + std::to_string(sz.QuadPart))) {
        CloseHandle(h);
        return false;
    }
    std::vector<char> buf(256 * 1024);
    unsigned long long left = static_cast<unsigned long long>(sz.QuadPart);
    bool good = true;
    while (left > 0) {
        DWORD want = static_cast<DWORD>(std::min<unsigned long long>(left, buf.size())), got = 0;
        if (!ReadFile(h, buf.data(), want, &got, nullptr) || got == 0) { good = false; break; }
        if (!c.SendAll(buf.data(), got)) { good = false; break; }
        left -= got;
        Lock l(&g_cs);
        g_clients[id].sent += got;
        g_clients[id].lastTick = GetTickCount();
    }
    CloseHandle(h);
    return good;
}

// インストール元の受け渡し（合言葉なし。first は OFFICE|PC名 ／ ADOBE|PC名 の行）
void ServePackage(Conn& c, const std::wstring& ip, const std::vector<std::string>& first, int k) {
    std::vector<Entry> entries;
    unsigned long id;
    const std::wstring tagName = std::wstring(L"（") + g_pkgs[k].label + L"）";
    const std::wstring doneText = std::wstring(g_pkgs[k].label) + L" を受け取りました";
    {
        Lock l(&g_cs);
        entries = g_pkgs[k].entries;
        id = g_nextClient++;
        ClientInfo ci;
        ci.name = (first.size() >= 2 ? FromUtf8(first[1]) : std::wstring(L"（不明）")) + tagName;
        ci.ip = ip;
        ci.state = entries.empty() ? L"インストール元なし" : L"受け取り中";
        ci.total = g_pkgs[k].bytes;
        ci.lastTick = GetTickCount();
        g_clients[id] = ci;
    }
    if (entries.empty()) {
        c.SendLine("ERR|この配布サーバーには、そのインストール元がありません。");
        return;
    }
    unsigned long long total = 0;
    for (const auto& e : entries) total += e.size;
    std::string out = "COUNT|" + std::to_string(entries.size()) + "|" + std::to_string(total) + "\n";
    for (const auto& e : entries) out += (e.dir ? "D|" : "F|") + ToUtf8(e.rel) + "|" + std::to_string(e.size) + "\n";
    if (!c.SendAll(out.data(), out.size())) return;
    std::string line;
    while (!g_serverStop && c.ReadLine(line)) {
        auto f = SplitBar(line);
        if (f[0] == g_pkgs[k].getCmd && f.size() >= 2) {
            size_t i = static_cast<size_t>(strtoul(f[1].c_str(), nullptr, 10));
            if (i >= entries.size() || entries[i].dir) { c.SendLine("ERR|番号が違います。"); continue; }
            if (!SendFileTo(c, entries[i].full, id)) break;
        } else if (f[0] == "DONE") {
            Lock l(&g_cs);
            g_clients[id].state = doneText;
            g_clients[id].sent = g_clients[id].total;
            return;
        }
    }
    Lock l(&g_cs);
    if (g_clients[id].state != doneText) g_clients[id].state = L"途中で切れました";
}

DWORD WINAPI ClientServeThread(LPVOID param) {
    ClientJob* job = static_cast<ClientJob*>(param);
    Conn c;
    c.s = job->s;
    std::wstring ip = job->ip;
    delete job;
    DWORD tmo = 120000;
    setsockopt(c.s, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&tmo), sizeof(tmo));
    unsigned long id = 0;
    std::string line;
    // 合言葉（Office のインストール元は合言葉なし）
    if (!c.ReadLine(line)) { c.Close(); return 0; }
    auto f = SplitBar(line);
    for (int k = 0; k < kPkgCount; ++k) {
        if (f[0] == g_pkgs[k].listCmd) {
            ServePackage(c, ip, f, k);
            c.Close();
            return 0;
        }
    }
    bool ok = false;
    std::wstring name = L"（不明）";
    {
        Lock l(&g_cs);
        auto& fail = g_fails[ip];
        if (fail.first >= 5 && GetTickCount() - fail.second < 60000) {
            c.SendLine("NG|合言葉を続けて間違えたため、1 分待ってからもう一度試してください。");
            c.Close();
            return 0;
        }
        if (f.size() >= 3 && f[0] == "HELLO") {
            name = FromUtf8(f[2]);
            ok = (FromUtf8(f[1]) == g_passcode);
        }
        if (!ok) {
            fail.first++;
            fail.second = GetTickCount();
        } else {
            fail.first = 0;
            id = g_nextClient++;
            ClientInfo ci;
            ci.name = name;
            ci.ip = ip;
            ci.state = L"つながりました";
            ci.total = g_totalBytes;
            ci.lastTick = GetTickCount();
            g_clients[id] = ci;
        }
    }
    if (!ok) {
        c.SendLine("NG|合言葉が違います。");
        c.Close();
        return 0;
    }
    std::vector<Entry> entries;
    unsigned session;
    {
        Lock l(&g_cs);
        entries = g_entries;  // 受け渡しの途中で一覧が変わっても困らないよう、写して使う
        session = g_session;
    }
    c.SendLine("OK|" + std::to_string(session));
    while (!g_serverStop && c.ReadLine(line)) {
        f = SplitBar(line);
        if (f[0] == "LIST") {
            unsigned long long total = 0;
            for (const auto& e : entries) total += e.size;
            std::string out = "COUNT|" + std::to_string(entries.size()) + "|" + std::to_string(total) + "\n";
            for (const auto& e : entries) out += (e.dir ? "D|" : "F|") + ToUtf8(e.rel) + "|" + std::to_string(e.size) + "\n";
            if (!c.SendAll(out.data(), out.size())) break;
            Lock l(&g_cs);
            g_clients[id].state = L"受け取り中";
            g_clients[id].total = total;
        } else if (f[0] == "GET" && f.size() >= 2) {
            size_t i = static_cast<size_t>(strtoul(f[1].c_str(), nullptr, 10));
            if (i >= entries.size() || entries[i].dir) { c.SendLine("ERR|番号が違います。"); continue; }
            if (!SendFileTo(c, entries[i].full, id)) break;  // 途中で切れたら、受け取る側もつながりをやめる
        } else if (f[0] == "DONE") {
            Lock l(&g_cs);
            int failed = f.size() >= 3 ? atoi(f[2].c_str()) : 0;
            g_clients[id].state = failed ? L"完了（" + std::to_wstring(failed) + L" 個できず）" : L"完了";
            g_clients[id].sent = g_clients[id].total;
            break;
        }
    }
    {
        Lock l(&g_cs);
        if (g_clients[id].state != L"完了" && g_clients[id].state.rfind(L"完了", 0) != 0) g_clients[id].state = L"途中で切れました";
    }
    c.Close();
    return 0;
}

DWORD WINAPI TcpServerThread(LPVOID) {
    while (!g_serverStop) {
        fd_set fs;
        FD_ZERO(&fs);
        FD_SET(g_tcpServer, &fs);
        timeval tv{0, 500000};
        if (select(0, &fs, nullptr, nullptr, &tv) <= 0) continue;
        sockaddr_in from{};
        int fl = sizeof(from);
        SOCKET s = accept(g_tcpServer, reinterpret_cast<sockaddr*>(&from), &fl);
        if (s == INVALID_SOCKET) continue;
        char ip[64] = "";
        inet_ntop(AF_INET, &from.sin_addr, ip, sizeof(ip));
        ClientJob* job = new ClientJob{s, FromUtf8(ip)};
        HANDLE th = CreateThread(nullptr, 0, ClientServeThread, job, 0, nullptr);
        if (th) CloseHandle(th);
        else {
            closesocket(s);
            delete job;
        }
    }
    return 0;
}

bool StartServerSockets(std::wstring& err) {
    g_serverStop = 0;
    g_udpServer = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    g_tcpServer = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    sockaddr_in a{};
    a.sin_family = AF_INET;
    a.sin_addr.s_addr = INADDR_ANY;
    a.sin_port = htons(kUdpPort);
    if (g_udpServer == INVALID_SOCKET || bind(g_udpServer, reinterpret_cast<sockaddr*>(&a), sizeof(a)) != 0) {
        err = L"見つけてもらうための受け口（UDP " + std::to_wstring(kUdpPort) + L"）を開けませんでした。";
        return false;
    }
    a.sin_port = htons(kTcpPort);
    if (g_tcpServer == INVALID_SOCKET || bind(g_tcpServer, reinterpret_cast<sockaddr*>(&a), sizeof(a)) != 0 || listen(g_tcpServer, 32) != 0) {
        err = L"配布の受け口（TCP " + std::to_wstring(kTcpPort) + L"）を開けませんでした。ほかのアプリが使っている可能性があります。";
        return false;
    }
    g_udpThread = CreateThread(nullptr, 0, UdpServerThread, nullptr, 0, nullptr);
    g_tcpThread = CreateThread(nullptr, 0, TcpServerThread, nullptr, 0, nullptr);
    return true;
}

void StopServerSockets() {
    InterlockedExchange(&g_serverStop, 1);
    for (HANDLE* t : {&g_udpThread, &g_tcpThread}) {
        if (*t) {
            WaitForSingleObject(*t, 3000);
            CloseHandle(*t);
            *t = nullptr;
        }
    }
    for (SOCKET* s : {&g_udpServer, &g_tcpServer}) {
        if (*s != INVALID_SOCKET) {
            closesocket(*s);
            *s = INVALID_SOCKET;
        }
    }
}

// 今つながっているネットワークのうち「パブリック」のものの名前（無ければ空）
std::vector<std::wstring> PublicNetworkNames() {
    static const CLSID kClsidNlm = {0xDCB00C01, 0x570F, 0x4A9B, {0x8D, 0x69, 0x19, 0x9F, 0xDB, 0xA5, 0x72, 0x3B}};
    static const IID kIidNlm = {0xDCB00000, 0x570F, 0x4A9B, {0x8D, 0x69, 0x19, 0x9F, 0xDB, 0xA5, 0x72, 0x3B}};
    std::vector<std::wstring> names;
    INetworkListManager* nlm = nullptr;
    if (FAILED(CoCreateInstance(kClsidNlm, nullptr, CLSCTX_ALL, kIidNlm, reinterpret_cast<void**>(&nlm))) || !nlm) return names;
    IEnumNetworks* en = nullptr;
    if (SUCCEEDED(nlm->GetNetworks(NLM_ENUM_NETWORK_CONNECTED, &en)) && en) {
        INetwork* n = nullptr;
        ULONG got = 0;
        while (en->Next(1, &n, &got) == S_OK && got) {
            NLM_NETWORK_CATEGORY cat = NLM_NETWORK_CATEGORY_PRIVATE;
            if (SUCCEEDED(n->GetCategory(&cat)) && cat == NLM_NETWORK_CATEGORY_PUBLIC) {
                BSTR name = nullptr;
                if (SUCCEEDED(n->GetName(&name)) && name) {
                    names.push_back(name);
                    SysFreeString(name);
                } else {
                    names.push_back(L"（名前なし）");
                }
            }
            n->Release();
        }
        en->Release();
    }
    nlm->Release();
    return names;
}

bool OnPublicNetwork() { return !PublicNetworkNames().empty(); }

std::wstring JoinNames(const std::vector<std::wstring>& v) {
    std::wstring r;
    for (const auto& n : v) r += (r.empty() ? L"" : L"、") + n;
    return r;
}

// 「パブリック」を「プライベート」に切り替える（管理者として scripts\network_private.ps1）。結果の説明を返す
bool SwitchToPrivate(HWND owner, std::wstring& message) {
    wchar_t exe[MAX_PATH * 2];
    DWORD n = GetModuleFileNameW(nullptr, exe, MAX_PATH * 2);
    std::wstring dir(exe, n);
    dir = dir.substr(0, dir.find_last_of(L'\\'));
    std::wstring script = dir + L"\\scripts\\network_private.ps1";
    if (GetFileAttributesW(script.c_str()) == INVALID_FILE_ATTRIBUTES) {
        message = L"切り替えの部品が見つかりません：" + script;
        return false;
    }
    wchar_t tmp[MAX_PATH];
    GetTempPathW(MAX_PATH, tmp);
    std::wstring out = std::wstring(tmp) + L"ManabitoClassSetup_network_" + std::to_wstring(GetTickCount()) + L".txt";
    wchar_t sys[MAX_PATH];
    UINT sn = GetSystemDirectoryW(sys, MAX_PATH);
    std::wstring ps = std::wstring(sn ? sys : L"C:\\Windows\\System32") + L"\\WindowsPowerShell\\v1.0\\powershell.exe";
    std::wstring params = L"-NoProfile -NonInteractive -ExecutionPolicy Bypass -WindowStyle Hidden -File \"" + script + L"\" -OutFile \"" + out + L"\"";
    SHELLEXECUTEINFOW sei{};
    sei.cbSize = sizeof(sei);
    sei.fMask = SEE_MASK_NOCLOSEPROCESS | SEE_MASK_NOASYNC;
    sei.hwnd = owner;
    sei.lpVerb = L"runas";
    sei.lpFile = ps.c_str();
    sei.lpParameters = params.c_str();
    sei.nShow = SW_HIDE;
    if (!ShellExecuteExW(&sei) || !sei.hProcess) {
        message = L"切り替えを取りやめました（管理者の確認で「いいえ」が押されました）。";
        return false;
    }
    HCURSOR old = SetCursor(LoadCursorW(nullptr, IDC_WAIT));
    WaitForSingleObject(sei.hProcess, 30000);
    CloseHandle(sei.hProcess);
    SetCursor(old);
    std::string bytes;
    HANDLE h = CreateFileW(out.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h != INVALID_HANDLE_VALUE) {
        char buf[8192];
        DWORD r = 0;
        ReadFile(h, buf, sizeof(buf), &r, nullptr);
        CloseHandle(h);
        bytes.assign(buf, r);
        DeleteFileW(out.c_str());
    }
    if (bytes.rfind("\xEF\xBB\xBF", 0) == 0) bytes.erase(0, 3);
    std::wstring text = FromUtf8(bytes);
    bool ok = text.rfind(L"OK", 0) == 0 && !OnPublicNetwork();
    size_t nl = text.find(L'\n');
    message = nl == std::wstring::npos ? text : text.substr(nl + 1);
    return ok;
}

// ---------------------------------------------------------------- 配布する側：窓

enum : int {
    IDS_SOURCES = 301,
    IDS_REMOVE,
    IDS_CLEAR,
    IDS_CLIENTS,
    IDS_STOP,
    IDS_NETWORK,
    IDS_OFFICE,  // ［Office のインストール元を用意する］
    IDS_ADOBE,   // ［Creative Cloud を用意する］（IDS_OFFICE + 1）
};
const UINT_PTR kServerTimer = 11;
HFONT g_sfont = nullptr, g_sbig = nullptr, g_sbold = nullptr;
std::wstring g_serverWarn;

void RefreshSourceList(HWND hwnd) {
    HWND lb = GetDlgItem(hwnd, IDS_SOURCES);
    SendMessageW(lb, LB_RESETCONTENT, 0, 0);
    Lock l(&g_cs);
    for (const auto& s : g_sources) {
        std::wstring t = s.name + L"　（" + std::to_wstring(s.files) + L" 個・" + FormatBytes(s.size) + L"）　" + s.path;
        SendMessageW(lb, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(t.c_str()));
    }
    InvalidateRect(hwnd, nullptr, TRUE);
}

void AddDrops(HWND hwnd, HDROP drop) {
    UINT n = DragQueryFileW(drop, 0xFFFFFFFF, nullptr, 0);
    std::vector<std::wstring> paths;
    for (UINT i = 0; i < n; ++i) {
        wchar_t p[MAX_PATH * 4];
        if (DragQueryFileW(drop, i, p, MAX_PATH * 4)) paths.push_back(p);
    }
    DragFinish(drop);
    if (paths.empty()) return;
    HCURSOR old = SetCursor(LoadCursorW(nullptr, IDC_WAIT));
    unsigned long long before = 0;
    int beforeFiles = 0;
    std::vector<Source> saved;
    {
        Lock l(&g_cs);
        saved = g_sources;
        before = g_totalBytes;
        beforeFiles = g_totalFiles;
        for (const auto& p : paths) {
            Source s;
            s.path = p;
            s.name = NameForDrop(p);
            // 名前が重なったら（2）（3）…を付ける
            std::wstring base = s.name;
            for (int k = 2;; ++k) {
                bool dup = false;
                for (const auto& o : g_sources)
                    if (_wcsicmp(o.name.c_str(), s.name.c_str()) == 0) dup = true;
                if (!dup) break;
                s.name = base + L" (" + std::to_wstring(k) + L")";
            }
            g_sources.push_back(s);
        }
        RebuildEntries();
    }
    SetCursor(old);
    unsigned long long added;
    int addedFiles;
    {
        Lock l(&g_cs);
        added = g_totalBytes - before;
        addedFiles = g_totalFiles - beforeFiles;
    }
    if (added > (2ull << 30) || addedFiles > 5000) {
        std::wstring q = L"ドロップしたものは、ファイル " + std::to_wstring(addedFiles) + L" 個・" + FormatBytes(added) +
                         L" あります。\n大きいため、配り終えるまで時間がかかります。このまま配布しますか？";
        if (MessageBoxW(hwnd, q.c_str(), kAppName, MB_YESNO | MB_ICONQUESTION) != IDYES) {
            Lock l(&g_cs);
            g_sources = saved;
            RebuildEntries();
        }
    }
    RefreshSourceList(hwnd);
}

// ---- インストール元を用意する（scripts\office_source.ps1 ／ adobe_source.ps1 を裏で動かす）

const wchar_t* kPrepAsk[2] = {
    L"Office のインストール元（約 3.4 GB）を、この PC にインターネットから一度だけダウンロードします。\n"
    L"用意できると、この窓を開いている間、ほかの PC の「時間のかかる準備」「まとめて実行」が、Office をここから受け取ります。\n\n"
    L"有線 LAN でつないでおくと速く配れます。始めますか？",
    L"Adobe Creative Cloud デスクトップアプリ（ランチャー）のインストーラーを、Adobe の公式ページにある直接ダウンロードの ZIP から、"
    L"この PC に一度だけダウンロードします。\n"
    L"用意できると、この窓を開いている間、ほかの PC の「時間のかかる準備」「まとめて実行」が、ランチャーをここから受け取ります。\n\n"
    L"Photoshop などのアプリ本体は、受講生がサインインしたあとに各 PC が Adobe から直接ダウンロードします（配れません）。始めますか？",
};

void StartPrep(HWND hwnd, int k) {
    Package& p = g_pkgs[k];
    if (p.prep) return;
    wchar_t exe[MAX_PATH * 2];
    DWORD n = GetModuleFileNameW(nullptr, exe, MAX_PATH * 2);
    std::wstring dir(exe, n);
    dir = dir.substr(0, dir.find_last_of(L'\\'));
    std::wstring script = dir + L"\\scripts\\" + p.script;
    if (GetFileAttributesW(script.c_str()) == INVALID_FILE_ATTRIBUTES) {
        MessageBoxW(hwnd, (L"用意する部品が見つかりません：" + script).c_str(), kAppName, MB_OK | MB_ICONWARNING);
        return;
    }
    if (MessageBoxW(hwnd, kPrepAsk[k], kAppName, MB_YESNO | MB_ICONQUESTION) != IDYES) return;
    MakeDirs(AppDataDir());
    p.statusFile = AppDataDir() + L"\\" + p.folder + L"_status.txt";
    DeleteFileW(p.statusFile.c_str());
    wchar_t sys[MAX_PATH];
    UINT sn = GetSystemDirectoryW(sys, MAX_PATH);
    std::wstring ps = std::wstring(sn ? sys : L"C:\\Windows\\System32") + L"\\WindowsPowerShell\\v1.0\\powershell.exe";
    std::wstring cmd = L"\"" + ps + L"\" -NoProfile -NonInteractive -ExecutionPolicy Bypass -WindowStyle Hidden -File \"" + script + L"\" -OutDir \"" +
                       PkgDir(k) + L"\" -StatusFile \"" + p.statusFile + L"\" -LogFile \"" + AppDataDir() + L"\\" + p.folder + L"_log.txt\"";
    if (!g_profilePath.empty()) cmd += L" -ProfilePath \"" + g_profilePath + L"\"";
    std::vector<wchar_t> buf(cmd.begin(), cmd.end());
    buf.push_back(L'\0');
    STARTUPINFOW si{};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;
    PROCESS_INFORMATION pi{};
    if (!CreateProcessW(nullptr, buf.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi)) {
        MessageBoxW(hwnd, L"用意を始められませんでした。", kAppName, MB_OK | MB_ICONWARNING);
        return;
    }
    CloseHandle(pi.hThread);
    p.prep = pi.hProcess;
    p.text = std::wstring(p.label) + L" のインストール元：入手の準備をしています…";
    EnableWindow(GetDlgItem(hwnd, IDS_OFFICE + k), FALSE);
    InvalidateRect(hwnd, nullptr, TRUE);
}

std::wstring PkgReadyText(int k) {
    Lock l(&g_cs);
    const Package& p = g_pkgs[k];
    if (p.version.empty()) return std::wstring(p.label) + L" のインストール元：まだ用意していません（用意すると、ほかの PC はここから受け取れます）";
    return std::wstring(p.label) + L" のインストール元：" + p.version + L"（" + FormatBytes(p.bytes) + L"）を配っています（合言葉なし）";
}

// 用意の進み具合を読む（タイマーから）
void PollPrep(HWND hwnd, int k) {
    Package& p = g_pkgs[k];
    if (!p.prep) return;
    std::string bytes;
    HANDLE h = CreateFileW(p.statusFile.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
    if (h != INVALID_HANDLE_VALUE) {
        char buf[1024];
        DWORD n = 0;
        ReadFile(h, buf, sizeof(buf), &n, nullptr);
        CloseHandle(h);
        bytes.assign(buf, n);
    }
    auto f = SplitBar(bytes);
    bool ended = WaitForSingleObject(p.prep, 0) == WAIT_OBJECT_0;
    std::wstring text = p.text;
    if (f[0] == "download" && f.size() >= 3) {
        unsigned long long got = _strtoui64(f[1].c_str(), nullptr, 10), exp = _strtoui64(f[2].c_str(), nullptr, 10);
        text = std::wstring(p.label) + L" のインストール元を用意しています… " + FormatBytes(got) + (exp ? L" / 約 " + FormatBytes(exp) : std::wstring());
    } else if (f[0] == "check") {
        text = std::wstring(p.label) + L" のインストール元：電子署名を確かめています…";
    }
    if (ended) {
        CloseHandle(p.prep);
        p.prep = nullptr;
        EnableWindow(GetDlgItem(hwnd, IDS_OFFICE + k), TRUE);
        ScanPackage(k);
        if (f[0] == "done") {
            text = PkgReadyText(k);
        } else {
            std::wstring why = (f[0] == "fail" && f.size() >= 2) ? FromUtf8(f[1]) : std::wstring(L"途中で終わりました。");
            text = std::wstring(p.label) + L" のインストール元を用意できませんでした：" + why;
            MessageBoxW(hwnd, text.c_str(), kAppName, MB_OK | MB_ICONWARNING);
        }
    }
    if (text != p.text) {
        p.text = text;
        InvalidateRect(hwnd, nullptr, TRUE);
    }
}

void LayoutServer(HWND hwnd) {
    RECT rc;
    GetClientRect(hwnd, &rc);
    int d = Dpi(hwnd);
    auto S = [d](int v) { return MulDiv(v, d, 96); };
    int m = S(16), W = rc.right, H = rc.bottom;
    int y = S(110) + (g_serverWarn.empty() ? 0 : S(34));
    MoveWindow(GetDlgItem(hwnd, IDS_NETWORK), W - m - S(200), S(70), S(200), S(30), TRUE);
    ShowWindow(GetDlgItem(hwnd, IDS_NETWORK), g_serverWarn.empty() ? SW_HIDE : SW_SHOW);
    int listH = (H - y - S(326));
    if (listH < S(80)) listH = S(80);
    MoveWindow(GetDlgItem(hwnd, IDS_SOURCES), m, y + S(22), W - 2 * m, listH, TRUE);
    int by = y + S(22) + listH + S(6);
    MoveWindow(GetDlgItem(hwnd, IDS_REMOVE), m, by, S(160), S(30), TRUE);
    MoveWindow(GetDlgItem(hwnd, IDS_CLEAR), m + S(170), by, S(120), S(30), TRUE);
    MoveWindow(GetDlgItem(hwnd, IDS_OFFICE), m, by + S(40), S(250), S(30), TRUE);
    MoveWindow(GetDlgItem(hwnd, IDS_ADOBE), m, by + S(76), S(250), S(30), TRUE);
    int cy = by + S(140);
    MoveWindow(GetDlgItem(hwnd, IDS_CLIENTS), m, cy, W - 2 * m, H - cy - S(56), TRUE);
    MoveWindow(GetDlgItem(hwnd, IDS_STOP), W - m - S(200), H - S(44), S(200), S(32), TRUE);
    HWND lv = GetDlgItem(hwnd, IDS_CLIENTS);
    RECT lr;
    GetClientRect(lv, &lr);
    ListView_SetColumnWidth(lv, 0, lr.right * 30 / 100);
    ListView_SetColumnWidth(lv, 1, lr.right * 20 / 100);
    ListView_SetColumnWidth(lv, 2, lr.right * 25 / 100);
    ListView_SetColumnWidth(lv, 3, lr.right - lr.right * 75 / 100 - 4);
    InvalidateRect(hwnd, nullptr, TRUE);
}

void RefreshClients(HWND hwnd) {
    HWND lv = GetDlgItem(hwnd, IDS_CLIENTS);
    std::vector<ClientInfo> list;
    {
        Lock l(&g_cs);
        for (const auto& kv : g_clients) list.push_back(kv.second);
    }
    int count = ListView_GetItemCount(lv);
    for (size_t i = 0; i < list.size(); ++i) {
        const auto& c = list[i];
        std::wstring pct = c.total ? std::to_wstring(static_cast<int>(c.sent * 100 / c.total)) + L"%" : L"―";
        std::wstring amount = FormatBytes(c.sent) + L" / " + FormatBytes(c.total);
        if (static_cast<int>(i) >= count) {
            LVITEMW it{};
            it.mask = LVIF_TEXT;
            it.iItem = static_cast<int>(i);
            it.pszText = const_cast<LPWSTR>(c.name.c_str());
            ListView_InsertItem(lv, &it);
        } else {
            ListView_SetItemText(lv, static_cast<int>(i), 0, const_cast<LPWSTR>(c.name.c_str()));
        }
        ListView_SetItemText(lv, static_cast<int>(i), 1, const_cast<LPWSTR>(c.state.c_str()));
        ListView_SetItemText(lv, static_cast<int>(i), 2, const_cast<LPWSTR>(amount.c_str()));
        ListView_SetItemText(lv, static_cast<int>(i), 3, const_cast<LPWSTR>(pct.c_str()));
    }
}

void PaintServer(HWND hwnd, HDC dc) {
    RECT rc;
    GetClientRect(hwnd, &rc);
    int d = Dpi(hwnd);
    auto S = [d](int v) { return MulDiv(v, d, 96); };
    HBRUSH paper = CreateSolidBrush(RGB(0xF7, 0xF3, 0xEA));
    FillRect(dc, &rc, paper);
    DeleteObject(paper);
    RECT bar{0, 0, rc.right, S(4)};
    HBRUSH accent = CreateSolidBrush(RGB(0x2D, 0x5B, 0x8A));
    FillRect(dc, &bar, accent);
    DeleteObject(accent);
    SetBkMode(dc, TRANSPARENT);
    int m = S(16);
    std::wstring pass, summary;
    {
        Lock l(&g_cs);
        pass = g_passcode;
        summary = L"配るもの：ファイル " + std::to_wstring(g_totalFiles) + L" 個・" + FormatBytes(g_totalBytes) + L"　／　この PC：" +
                  ComputerName();
    }
    HGDIOBJ old = SelectObject(dc, g_sbold);
    SetTextColor(dc, RGB(0x33, 0x2E, 0x29));
    RECT r1{m, S(14), rc.right - m, S(40)};
    DrawTextW(dc, L"授業ファイルの配布サーバー", -1, &r1, DT_SINGLELINE | DT_LEFT);
    SelectObject(dc, g_sbig);
    SetTextColor(dc, RGB(0x2D, 0x5B, 0x8A));
    std::wstring pc = L"合言葉　" + pass;
    RECT r2{m, S(40), rc.right - m, S(100)};
    DrawTextW(dc, pc.c_str(), -1, &r2, DT_SINGLELINE | DT_LEFT | DT_VCENTER);
    SelectObject(dc, g_sfont);
    SetTextColor(dc, RGB(0x7A, 0x72, 0x68));
    RECT r3{m + S(260), S(44), rc.right - m - S(210), S(100)};
    DrawTextW(dc, L"受講生の PC で「取得する」を押し、この合言葉を入れてもらってください。", -1, &r3, DT_WORDBREAK | DT_LEFT);
    int y = S(110);
    if (!g_serverWarn.empty()) {
        SetTextColor(dc, RGB(0xC4, 0x52, 0x1E));
        RECT rw{m, y, rc.right - m, y + S(30)};
        DrawTextW(dc, g_serverWarn.c_str(), -1, &rw, DT_WORDBREAK | DT_LEFT);
        y += S(34);
    }
    SetTextColor(dc, RGB(0x33, 0x2E, 0x29));
    RECT r4{m, y, rc.right - m, y + S(20)};
    DrawTextW(dc, L"配るもの（ファイル・フォルダー・ドライブを、ここにドロップしてください）", -1, &r4, DT_SINGLELINE | DT_LEFT);
    HWND lb = GetDlgItem(hwnd, IDS_SOURCES);
    RECT lr;
    GetWindowRect(lb, &lr);
    MapWindowPoints(nullptr, hwnd, reinterpret_cast<POINT*>(&lr), 2);
    RECT r5{m + S(310), lr.bottom + S(12), rc.right - m, lr.bottom + S(32)};
    DrawTextW(dc, summary.c_str(), -1, &r5, DT_SINGLELINE | DT_LEFT | DT_END_ELLIPSIS);
    for (int k = 0; k < kPkgCount; ++k) {
        SetTextColor(dc, g_pkgs[k].prep ? RGB(0x2D, 0x5B, 0x8A) : RGB(0x7A, 0x72, 0x68));
        RECT ro{m + S(262), lr.bottom + S(46 + 36 * k), rc.right - m, lr.bottom + S(76 + 36 * k)};
        DrawTextW(dc, g_pkgs[k].text.c_str(), -1, &ro, DT_WORDBREAK | DT_LEFT | DT_END_ELLIPSIS);
    }
    SetTextColor(dc, RGB(0x33, 0x2E, 0x29));
    RECT r6{m, lr.bottom + S(122), rc.right - m, lr.bottom + S(142)};
    DrawTextW(dc, L"受け取った PC", -1, &r6, DT_SINGLELINE | DT_LEFT);
    SelectObject(dc, old);
}

LRESULT CALLBACK ServerProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_CREATE: {
            g_sfont = MakeFont(hwnd, 105, false);
            g_sbold = MakeFont(hwnd, 120, true);
            g_sbig = MakeFont(hwnd, 280, true);
            CreateWindowExW(0, WC_LISTBOXW, L"", WS_CHILD | WS_VISIBLE | WS_BORDER | WS_VSCROLL | LBS_NOINTEGRALHEIGHT | LBS_EXTENDEDSEL, 0,
                            0, 10, 10, hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDS_SOURCES)), g_inst, nullptr);
            CreateWindowExW(0, WC_BUTTONW, L"選んだものを外す", WS_CHILD | WS_VISIBLE | WS_TABSTOP, 0, 0, 10, 10, hwnd,
                            reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDS_REMOVE)), g_inst, nullptr);
            CreateWindowExW(0, WC_BUTTONW, L"すべて外す", WS_CHILD | WS_VISIBLE | WS_TABSTOP, 0, 0, 10, 10, hwnd,
                            reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDS_CLEAR)), g_inst, nullptr);
            HWND lv = CreateWindowExW(0, WC_LISTVIEWW, L"", WS_CHILD | WS_VISIBLE | WS_BORDER | LVS_REPORT | LVS_NOSORTHEADER, 0, 0, 10, 10,
                                      hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDS_CLIENTS)), g_inst, nullptr);
            ListView_SetExtendedListViewStyle(lv, LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER);
            const wchar_t* heads[] = {L"PC 名", L"状態", L"送った量", L"進み具合"};
            for (int i = 0; i < 4; ++i) {
                LVCOLUMNW col{};
                col.mask = LVCF_TEXT | LVCF_WIDTH;
                col.pszText = const_cast<LPWSTR>(heads[i]);
                col.cx = 100;
                ListView_InsertColumn(lv, i, &col);
            }
            CreateWindowExW(0, WC_BUTTONW, L"配布をやめて閉じる", WS_CHILD | WS_VISIBLE | WS_TABSTOP, 0, 0, 10, 10, hwnd,
                            reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDS_STOP)), g_inst, nullptr);
            CreateWindowExW(0, WC_BUTTONW, L"Office のインストール元を用意する", WS_CHILD | WS_VISIBLE | WS_TABSTOP, 0, 0, 10, 10, hwnd,
                            reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDS_OFFICE)), g_inst, nullptr);
            CreateWindowExW(0, WC_BUTTONW, L"Creative Cloud（ランチャー）を用意する", WS_CHILD | WS_VISIBLE | WS_TABSTOP, 0, 0, 10, 10, hwnd,
                            reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDS_ADOBE)), g_inst, nullptr);
            CreateWindowExW(0, WC_BUTTONW, L"プライベートに切り替える", WS_CHILD | WS_TABSTOP, 0, 0, 10, 10, hwnd,
                            reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDS_NETWORK)), g_inst, nullptr);
            SetFontAll(hwnd, g_sfont);
            DragAcceptFiles(hwnd, TRUE);
            SetTimer(hwnd, kServerTimer, 500, nullptr);
            return 0;
        }
        case WM_SIZE:
            LayoutServer(hwnd);
            return 0;
        case WM_ERASEBKGND:
            return 1;
        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC dc = BeginPaint(hwnd, &ps);
            PaintServer(hwnd, dc);
            EndPaint(hwnd, &ps);
            return 0;
        }
        case WM_DROPFILES:
            AddDrops(hwnd, reinterpret_cast<HDROP>(wp));
            return 0;
        case WM_TIMER:
            if (wp == kServerTimer) {
                RefreshClients(hwnd);
                for (int k = 0; k < kPkgCount; ++k) PollPrep(hwnd, k);
            }
            return 0;
        case WM_COMMAND:
            switch (LOWORD(wp)) {
                case IDS_REMOVE: {
                    HWND lb = GetDlgItem(hwnd, IDS_SOURCES);
                    int n = static_cast<int>(SendMessageW(lb, LB_GETSELCOUNT, 0, 0));
                    if (n <= 0) return 0;
                    std::vector<int> sel(static_cast<size_t>(n));
                    SendMessageW(lb, LB_GETSELITEMS, static_cast<WPARAM>(n), reinterpret_cast<LPARAM>(sel.data()));
                    std::sort(sel.rbegin(), sel.rend());
                    {
                        Lock l(&g_cs);
                        for (int i : sel)
                            if (i >= 0 && i < static_cast<int>(g_sources.size())) g_sources.erase(g_sources.begin() + i);
                        RebuildEntries();
                    }
                    RefreshSourceList(hwnd);
                    return 0;
                }
                case IDS_CLEAR: {
                    Lock l(&g_cs);
                    g_sources.clear();
                    RebuildEntries();
                }
                    RefreshSourceList(hwnd);
                    return 0;
                case IDS_NETWORK: {
                    std::wstring result;
                    bool ok = SwitchToPrivate(hwnd, result);
                    if (ok) g_serverWarn.clear();
                    MessageBoxW(hwnd, ((ok ? L"プライベートに切り替えました。\n\n" : L"プライベートに切り替えられませんでした。\n\n") + result).c_str(),
                                kAppName, MB_OK | (ok ? MB_ICONINFORMATION : MB_ICONWARNING));
                    LayoutServer(hwnd);
                    return 0;
                }
                case IDS_STOP:
                    SendMessageW(hwnd, WM_CLOSE, 0, 0);
                    return 0;
                case IDS_OFFICE:
                case IDS_ADOBE:
                    StartPrep(hwnd, LOWORD(wp) - IDS_OFFICE);
                    return 0;
            }
            break;
        case WM_CLOSE: {
            bool busy = false;
            {
                Lock l(&g_cs);
                for (const auto& kv : g_clients)
                    if (kv.second.state == L"受け取り中" &&
                        GetTickCount() - kv.second.lastTick < 30000)
                        busy = true;
            }
            if ((g_pkgs[0].prep || g_pkgs[1].prep) &&
                MessageBoxW(hwnd, L"インストール元を用意しています。閉じると、用意は止まります。閉じますか？", kAppName,
                            MB_YESNO | MB_ICONQUESTION | MB_DEFBUTTON2) != IDYES)
                return 0;
            {
            }
            if (busy && MessageBoxW(hwnd, L"受け取っている途中の PC があります。配布をやめますか？", kAppName,
                                    MB_YESNO | MB_ICONQUESTION | MB_DEFBUTTON2) != IDYES)
                return 0;
            DestroyWindow(hwnd);
            return 0;
        }
        case WM_DESTROY:
            KillTimer(hwnd, kServerTimer);
            for (int k = 0; k < kPkgCount; ++k) {
                if (g_pkgs[k].prep) {
                    TerminateProcess(g_pkgs[k].prep, 1);
                    CloseHandle(g_pkgs[k].prep);
                    g_pkgs[k].prep = nullptr;
                }
            }
            StopServerSockets();
            {
                Lock l(&g_cs);
                g_sources.clear();
                g_entries.clear();
                g_clients.clear();
                g_totalBytes = 0;
                g_totalFiles = 0;
            }
            for (HFONT* f : {&g_sfont, &g_sbig, &g_sbold})
                if (*f) {
                    DeleteObject(*f);
                    *f = nullptr;
                }
            g_server = nullptr;
            return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

// ---------------------------------------------------------------- 受け取る側：合言葉を聞く小さな窓

struct AskState {
    std::wstring result;
    bool done = false, ok = false;
    HFONT font = nullptr;
};

LRESULT CALLBACK AskProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    AskState* st = reinterpret_cast<AskState*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    switch (msg) {
        case WM_COMMAND:
            if (LOWORD(wp) == IDOK || LOWORD(wp) == IDCANCEL) {
                if (st) {
                    wchar_t buf[16] = L"";
                    GetDlgItemTextW(hwnd, 100, buf, 16);
                    st->result = buf;
                    st->ok = (LOWORD(wp) == IDOK);
                    st->done = true;
                }
                return 0;
            }
            break;
        case WM_CLOSE:
            if (st) {
                st->ok = false;
                st->done = true;
            }
            return 0;
        case WM_CTLCOLORSTATIC: {
            HDC dc = reinterpret_cast<HDC>(wp);
            SetBkMode(dc, TRANSPARENT);
            return reinterpret_cast<LRESULT>(GetStockObject(WHITE_BRUSH));
        }
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

// 合言葉を聞く。キャンセルなら空
std::wstring AskPasscode(HWND owner, const std::wstring& server, const std::wstring& note) {
    static bool registered = false;
    if (!registered) {
        WNDCLASSEXW wc{};
        wc.cbSize = sizeof(wc);
        wc.lpfnWndProc = AskProc;
        wc.hInstance = g_inst;
        wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        wc.hbrBackground = static_cast<HBRUSH>(GetStockObject(WHITE_BRUSH));
        wc.lpszClassName = L"ManabitoShareAsk";
        RegisterClassExW(&wc);
        registered = true;
    }
    int d = Dpi(owner);
    auto S = [d](int v) { return MulDiv(v, d, 96); };
    RECT orc;
    GetWindowRect(owner, &orc);
    int w = S(440), h = S(230);
    int x = orc.left + ((orc.right - orc.left) - w) / 2, y = orc.top + ((orc.bottom - orc.top) - h) / 2;
    AskState st;
    HWND hwnd = CreateWindowExW(WS_EX_DLGMODALFRAME, L"ManabitoShareAsk", kAppName, WS_POPUP | WS_CAPTION | WS_SYSMENU, x, y, w, h, owner,
                                nullptr, g_inst, nullptr);
    SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(&st));
    std::wstring text = server + L" の配布を受け取ります。\n先生が見せている 4 桁の合言葉を入れてください。";
    if (!note.empty()) text = note + L"\n" + text;
    CreateWindowExW(0, WC_STATICW, text.c_str(), WS_CHILD | WS_VISIBLE, S(16), S(14), S(400), S(70), hwnd, nullptr, g_inst, nullptr);
    HWND edit = CreateWindowExW(WS_EX_CLIENTEDGE, WC_EDITW, L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_NUMBER | ES_CENTER, S(16), S(92),
                                S(160), S(40), hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(100)), g_inst, nullptr);
    SendMessageW(edit, EM_LIMITTEXT, 4, 0);
    CreateWindowExW(0, WC_BUTTONW, L"受け取る", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON, S(196), S(96), S(110), S(32), hwnd,
                    reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDOK)), g_inst, nullptr);
    CreateWindowExW(0, WC_BUTTONW, L"キャンセル", WS_CHILD | WS_VISIBLE | WS_TABSTOP, S(314), S(96), S(100), S(32), hwnd,
                    reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDCANCEL)), g_inst, nullptr);
    st.font = MakeFont(hwnd, 105, false);
    HFONT big = MakeFont(hwnd, 200, true);
    SetFontAll(hwnd, st.font);
    SendMessageW(edit, WM_SETFONT, reinterpret_cast<WPARAM>(big), TRUE);
    EnableWindow(owner, FALSE);
    ShowWindow(hwnd, SW_SHOW);
    SetFocus(edit);
    MSG m;
    while (!st.done && GetMessageW(&m, nullptr, 0, 0) > 0) {
        if (IsDialogMessageW(hwnd, &m)) continue;
        TranslateMessage(&m);
        DispatchMessageW(&m);
    }
    EnableWindow(owner, TRUE);
    DestroyWindow(hwnd);
    DeleteObject(st.font);
    DeleteObject(big);
    SetForegroundWindow(owner);
    if (!st.ok) return std::wstring();
    return st.result;
}

// 受け取るフォルダーを選ぶ（初期値は前回の場所）。キャンセルなら空
std::wstring PickFolder(HWND owner, const std::wstring& initial) {
    static const CLSID kClsidOpen = {0xDC1C5A9C, 0xE88A, 0x4DDE, {0xA5, 0xA1, 0x60, 0xF8, 0x2A, 0x20, 0xAE, 0xF7}};
    static const IID kIidOpen = {0xD57C7288, 0xD4AD, 0x4768, {0xBE, 0x02, 0x9D, 0x96, 0x95, 0x32, 0xD9, 0x60}};
    static const IID kIidItem = {0x43826D1E, 0xE718, 0x42EE, {0xBC, 0x55, 0xA1, 0xE2, 0x61, 0xC3, 0x7B, 0xFE}};
    IFileOpenDialog* dlg = nullptr;
    if (FAILED(CoCreateInstance(kClsidOpen, nullptr, CLSCTX_INPROC_SERVER, kIidOpen, reinterpret_cast<void**>(&dlg))) || !dlg)
        return initial;
    DWORD opt = 0;
    dlg->GetOptions(&opt);
    dlg->SetOptions(opt | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM);
    dlg->SetTitle(L"授業ファイルを受け取るフォルダーを選んでください");
    IShellItem* start = nullptr;
    if (SUCCEEDED(SHCreateItemFromParsingName(initial.c_str(), nullptr, kIidItem, reinterpret_cast<void**>(&start))) && start) {
        dlg->SetFolder(start);
        start->Release();
    }
    std::wstring out;
    if (SUCCEEDED(dlg->Show(owner))) {
        IShellItem* item = nullptr;
        if (SUCCEEDED(dlg->GetResult(&item)) && item) {
            PWSTR p = nullptr;
            if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &p)) && p) {
                out = p;
                CoTaskMemFree(p);
            }
            item->Release();
        }
    }
    dlg->Release();
    return out;
}

void SaveDest(const std::wstring& dir) {
    std::wstring d = AppDataDir();
    MakeDirs(d);
    std::string u = "\xEF\xBB\xBF" + ToUtf8(dir);
    HANDLE h = CreateFileW((d + L"\\share_dest.txt").c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return;
    DWORD w = 0;
    WriteFile(h, u.data(), static_cast<DWORD>(u.size()), &w, nullptr);
    CloseHandle(h);
}

// ---------------------------------------------------------------- 受け取る側：受け取り（裏で）と進み具合の窓

struct Download {
    Offer offer;
    std::wstring pass, dest;
    HWND wnd = nullptr;
    volatile LONG cancel = 0;
    CRITICAL_SECTION cs;
    // 進み具合（cs で守る）
    std::wstring current, error;
    int fileNo = 0, fileCount = 0, failed = 0;
    unsigned long long got = 0, total = 0;
    bool finished = false, wrongPass = false;
    DWORD startTick = 0;
};

const UINT kDownloadDone = WM_APP + 50;

bool ConnectTo(Conn& c, const std::wstring& ip, int port) {
    c.s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (c.s == INVALID_SOCKET) return false;
    sockaddr_in a{};
    a.sin_family = AF_INET;
    a.sin_port = htons(static_cast<u_short>(port));
    InetPtonW(AF_INET, ip.c_str(), &a.sin_addr);
    u_long nb = 1;
    ioctlsocket(c.s, FIONBIO, &nb);
    connect(c.s, reinterpret_cast<sockaddr*>(&a), sizeof(a));
    fd_set ws, es;
    FD_ZERO(&ws);
    FD_ZERO(&es);
    FD_SET(c.s, &ws);
    FD_SET(c.s, &es);
    timeval tv{8, 0};
    int r = select(0, nullptr, &ws, &es, &tv);
    nb = 0;
    ioctlsocket(c.s, FIONBIO, &nb);
    if (r <= 0 || FD_ISSET(c.s, &es)) {
        c.Close();
        return false;
    }
    DWORD tmo = 60000;
    setsockopt(c.s, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&tmo), sizeof(tmo));
    return true;
}

DWORD WINAPI DownloadThread(LPVOID param) {
    Download* dl = static_cast<Download*>(param);
    auto fail = [dl](const std::wstring& why) {
        Lock l(&dl->cs);
        dl->error = why;
        dl->finished = true;
    };
    Conn c;
    if (!ConnectTo(c, dl->offer.ip, dl->offer.port)) {
        fail(L"配布サーバー（" + dl->offer.server + L"）につながりませんでした。配布サーバーの PC のファイアウォールか、ネットワークの設定（プライベート）を確かめてください。");
        PostMessageW(dl->wnd, kDownloadDone, 0, 0);
        return 0;
    }
    std::string line;
    c.SendLine("HELLO|" + ToUtf8(dl->pass) + "|" + ToUtf8(ComputerName()));
    if (!c.ReadLine(line)) { fail(L"配布サーバーから返事がありませんでした。"); c.Close(); PostMessageW(dl->wnd, kDownloadDone, 0, 0); return 0; }
    auto f = SplitBar(line);
    if (f[0] != "OK") {
        {
            Lock l(&dl->cs);
            dl->wrongPass = line.find("合言葉が違") != std::string::npos;
        }
        fail(f.size() > 1 ? FromUtf8(f[1]) : L"受け取れませんでした。");
        c.Close();
        PostMessageW(dl->wnd, kDownloadDone, 0, 0);
        return 0;
    }
    c.SendLine("LIST");
    if (!c.ReadLine(line) || line.rfind("COUNT|", 0) != 0) { fail(L"一覧を受け取れませんでした。"); c.Close(); PostMessageW(dl->wnd, kDownloadDone, 0, 0); return 0; }
    f = SplitBar(line);
    size_t n = f.size() >= 3 ? static_cast<size_t>(strtoul(f[1].c_str(), nullptr, 10)) : 0;
    struct Item { bool dir; std::wstring rel; unsigned long long size; };
    std::vector<Item> items;
    int files = 0;
    unsigned long long total = 0;
    for (size_t i = 0; i < n; ++i) {
        if (!c.ReadLine(line)) break;
        auto g = SplitBar(line);
        if (g.size() < 3) continue;
        Item it{g[0] == "D", FromUtf8(g[1]), _strtoui64(g[2].c_str(), nullptr, 10)};
        items.push_back(it);
        if (!it.dir) {
            ++files;
            total += it.size;
        }
    }
    {
        Lock l(&dl->cs);
        dl->fileCount = files;
        dl->total = total;
    }
    int failed = 0, no = 0;
    MakeDirs(dl->dest);
    for (size_t i = 0; i < items.size() && !dl->cancel; ++i) {
        const Item& it = items[i];
        if (!SafeRelPath(it.rel)) { if (!it.dir) ++failed; continue; }  // 受け取るフォルダーの外を指すものは受け取らない
        std::wstring path = dl->dest + L"\\" + it.rel;
        if (it.dir) { MakeDirs(path); continue; }
        ++no;
        {
            Lock l(&dl->cs);
            dl->fileNo = no;
            dl->current = it.rel;
        }
        size_t slash = path.find_last_of(L'\\');
        MakeDirs(path.substr(0, slash));
        c.SendLine("GET|" + std::to_string(i));
        if (!c.ReadLine(line)) { ++failed; break; }
        if (line.rfind("DATA|", 0) != 0) { ++failed; continue; }
        unsigned long long size = _strtoui64(line.c_str() + 5, nullptr, 10);
        std::wstring part = path + L".part";
        HANDLE h = CreateFileW(part.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (h == INVALID_HANDLE_VALUE) {
            // 書けなくても中身は読み捨てる（つながりを保つ）
            HANDLE nul = CreateFileW(L"NUL", GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, 0, nullptr);
            c.ReadBody(size, nul, [](size_t) { return true; });
            CloseHandle(nul);
            ++failed;
            continue;
        }
        bool ok = c.ReadBody(size, h, [dl](size_t k) {
            Lock l(&dl->cs);
            dl->got += k;
            return dl->cancel == 0;
        });
        CloseHandle(h);
        if (!ok) {
            DeleteFileW(part.c_str());
            ++failed;
            break;
        }
        if (!MoveFileExW(part.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING)) {  // 同じ名前は上書き
            DeleteFileW(part.c_str());
            ++failed;
        }
    }
    c.SendLine("DONE|" + std::to_string(no - failed) + "|" + std::to_string(failed));
    c.Close();
    {
        Lock l(&dl->cs);
        dl->failed = failed;
        dl->finished = true;
        if (dl->cancel) dl->error = L"取り消しました。";
    }
    PostMessageW(dl->wnd, kDownloadDone, 0, 0);
    return 0;
}

LRESULT CALLBACK DownloadProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    Download* dl = reinterpret_cast<Download*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    switch (msg) {
        case WM_TIMER: {
            if (!dl) return 0;
            std::wstring cur, text;
            int pos = 0;
            {
                Lock l(&dl->cs);
                double sec = (GetTickCount() - dl->startTick) / 1000.0;
                double speed = sec > 0.5 ? dl->got / sec : 0;
                pos = dl->total ? static_cast<int>(dl->got * 1000 / dl->total) : 0;
                text = std::to_wstring(dl->fileNo) + L" / " + std::to_wstring(dl->fileCount) + L" 個・" + FormatBytes(dl->got) + L" / " +
                       FormatBytes(dl->total);
                if (speed > 0) {
                    wchar_t sp[64];
                    swprintf(sp, 64, L"・%.1f MB/秒", speed / (1 << 20));
                    text += sp;
                    if (dl->total > dl->got) text += L"・あと約 " + FormatSeconds((dl->total - dl->got) / speed);
                }
                cur = dl->current;
            }
            SetDlgItemTextW(hwnd, 201, cur.c_str());
            SetDlgItemTextW(hwnd, 202, text.c_str());
            SendDlgItemMessageW(hwnd, 203, PBM_SETPOS, static_cast<WPARAM>(pos), 0);
            return 0;
        }
        case WM_COMMAND:
            if (LOWORD(wp) == IDCANCEL && dl) InterlockedExchange(&dl->cancel, 1);
            return 0;
        case WM_CLOSE:
            if (dl) InterlockedExchange(&dl->cancel, 1);
            return 0;
        case kDownloadDone: {
            if (!dl) return 0;
            KillTimer(hwnd, 1);
            std::wstring err, dest = dl->dest;
            bool wrong;
            int failed, count;
            {
                Lock l(&dl->cs);
                err = dl->error;
                wrong = dl->wrongPass;
                failed = dl->failed;
                count = dl->fileCount;
            }
            HWND owner = GetWindow(hwnd, GW_OWNER);
            Offer offer = dl->offer;
            std::wstring passTried = dl->pass;
            ShowWindow(hwnd, SW_HIDE);
            if (wrong) {
                // 合言葉の間違い：もう一度聞く
                std::wstring again = AskPasscode(owner, offer.server, L"合言葉が違います。");
                if (!again.empty()) {
                    DestroyWindow(hwnd);
                    StartDownload(owner, offer, again, dest);  // 合言葉だけ変えて、もう一度
                    return 0;
                }
                MarkHandled(offer.session);
            } else if (!err.empty()) {
                MessageBoxW(owner, (L"受け取れませんでした。\n\n" + err).c_str(), kAppName, MB_OK | MB_ICONWARNING);
                if (err != L"取り消しました。") {
                    Lock l(&g_cs);  // あとでもう一度知らせてよい
                    if (g_pending == offer.session) g_pending = 0;
                } else {
                    MarkHandled(offer.session);
                }
            } else {
                MarkHandled(offer.session);
                std::wstring m = L"授業ファイルを受け取りました（" + std::to_wstring(count - failed) + L" 個）。\n\n受け取った場所：" + dest;
                if (failed) m += L"\n\n" + std::to_wstring(failed) + L" 個は受け取れませんでした。";
                MessageBoxW(owner, m.c_str(), kAppName, MB_OK | (failed ? MB_ICONWARNING : MB_ICONINFORMATION));
                ShellExecuteW(owner, L"open", dest.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
            }
            DestroyWindow(hwnd);
            return 0;
        }
        case WM_DESTROY:
            if (dl) {
                SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
                bool finished;
                {
                    Lock l(&dl->cs);
                    finished = dl->finished;
                }
                if (finished) {
                    DeleteCriticalSection(&dl->cs);
                    delete dl;
                } else {
                    // アプリが閉じられたなど、受け取りの途中：裏の作業がまだ使うので、止める合図だけ出して解放しない
                    InterlockedExchange(&dl->cancel, 1);
                    dl->wnd = nullptr;
                }
            }
            return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

}  // namespace

void StartDownload(HWND owner, const Offer& offer, const std::wstring& pass, const std::wstring& dest) {
    static bool registered = false;
    if (!registered) {
        WNDCLASSEXW wc{};
        wc.cbSize = sizeof(wc);
        wc.lpfnWndProc = DownloadProc;
        wc.hInstance = g_inst;
        wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        wc.hbrBackground = static_cast<HBRUSH>(GetStockObject(WHITE_BRUSH));
        wc.lpszClassName = L"ManabitoShareDownload";
        RegisterClassExW(&wc);
        registered = true;
    }
    Download* dl = new Download();
    dl->offer = offer;
    dl->pass = pass;
    dl->dest = dest;
    dl->startTick = GetTickCount();
    InitializeCriticalSection(&dl->cs);
    int d = Dpi(owner);
    auto S = [d](int v) { return MulDiv(v, d, 96); };
    RECT orc;
    GetWindowRect(owner, &orc);
    int w = S(560), h = S(210);
    HWND hwnd = CreateWindowExW(WS_EX_DLGMODALFRAME, L"ManabitoShareDownload", L"授業ファイルを受け取っています", WS_POPUP | WS_CAPTION | WS_SYSMENU,
                                orc.left + ((orc.right - orc.left) - w) / 2, orc.top + ((orc.bottom - orc.top) - h) / 2, w, h, owner, nullptr,
                                g_inst, nullptr);
    dl->wnd = hwnd;
    SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(dl));
    std::wstring head = offer.server + L" から → " + dest;
    CreateWindowExW(0, WC_STATICW, head.c_str(), WS_CHILD | WS_VISIBLE | SS_ENDELLIPSIS, S(16), S(12), S(520), S(22), hwnd, nullptr, g_inst, nullptr);
    CreateWindowExW(0, WC_STATICW, L"", WS_CHILD | WS_VISIBLE | SS_PATHELLIPSIS, S(16), S(40), S(520), S(22), hwnd,
                    reinterpret_cast<HMENU>(static_cast<INT_PTR>(201)), g_inst, nullptr);
    HWND pb = CreateWindowExW(0, PROGRESS_CLASSW, L"", WS_CHILD | WS_VISIBLE, S(16), S(68), S(520), S(20), hwnd,
                              reinterpret_cast<HMENU>(static_cast<INT_PTR>(203)), g_inst, nullptr);
    SendMessageW(pb, PBM_SETRANGE32, 0, 1000);
    CreateWindowExW(0, WC_STATICW, L"つないでいます…", WS_CHILD | WS_VISIBLE, S(16), S(96), S(520), S(22), hwnd,
                    reinterpret_cast<HMENU>(static_cast<INT_PTR>(202)), g_inst, nullptr);
    CreateWindowExW(0, WC_BUTTONW, L"取り消す", WS_CHILD | WS_VISIBLE | WS_TABSTOP, S(436), S(126), S(100), S(30), hwnd,
                    reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDCANCEL)), g_inst, nullptr);
    HFONT font = MakeFont(hwnd, 105, false);
    SetFontAll(hwnd, font);  // 窓が閉じるまで使う（小さいので、解放は省く）
    ShowWindow(hwnd, SW_SHOW);
    SetTimer(hwnd, 1, 500, nullptr);
    HANDLE th = CreateThread(nullptr, 0, DownloadThread, dl, 0, nullptr);
    if (th) CloseHandle(th);
}

// ---------------------------------------------------------------- 公開する関数

void Init(HINSTANCE inst, HWND owner, UINT foundMsg) {
    g_inst = inst;
    g_owner = owner;
    g_foundMsg = foundMsg;
    InitializeCriticalSection(&g_cs);
    WSADATA wd{};
    g_wsa = WSAStartup(MAKEWORD(2, 2), &wd) == 0;
    if (!g_wsa) return;
    g_discoverThread = CreateThread(nullptr, 0, DiscoverThread, nullptr, 0, nullptr);
}

void Shutdown() {
    if (g_server) DestroyWindow(g_server);
    InterlockedExchange(&g_stop, 1);
    if (g_discoverThread) {
        WaitForSingleObject(g_discoverThread, 3000);
        CloseHandle(g_discoverThread);
        g_discoverThread = nullptr;
    }
    if (g_wsa) WSACleanup();
    g_wsa = false;
}

bool IsServerRunning() { return g_server != nullptr; }

void SetProfilePath(const std::wstring& profilePath) { g_profilePath = profilePath; }

void OpenServerWindow(HWND owner) {
    if (g_server) {
        SetForegroundWindow(g_server);
        return;
    }
    if (!g_wsa) {
        MessageBoxW(owner, L"ネットワークの準備ができませんでした。", kAppName, MB_OK | MB_ICONWARNING);
        return;
    }
    static bool registered = false;
    if (!registered) {
        WNDCLASSEXW wc{};
        wc.cbSize = sizeof(wc);
        wc.lpfnWndProc = ServerProc;
        wc.hInstance = g_inst;
        wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        wc.lpszClassName = L"ManabitoShareServer";
        RegisterClassExW(&wc);
        registered = true;
    }
    {
        Lock l(&g_cs);
        wchar_t p[8];
        swprintf(p, 8, L"%04u", RandomU32() % 10000);
        g_passcode = p;
        g_sources.clear();
        g_entries.clear();
        g_clients.clear();
        g_fails.clear();
        g_totalBytes = 0;
        g_totalFiles = 0;
        g_session = RandomU32() | 1u;
    }
    // パブリックのネットワークにつながっていたら、プライベートに切り替えるかを聞く（受け取る側は切り替えなくてよい）
    std::vector<std::wstring> pub = PublicNetworkNames();
    if (!pub.empty()) {
        std::wstring head = L"この PC のネットワーク（" + JoinNames(pub) + L"）は「パブリック」です";
        TASKDIALOG_BUTTON buttons[] = {
            {7001, L"プライベートに切り替えて始める（おすすめ）\n管理者の確認が出ます。教室や事務所のネットワークのときだけ選んでください。"},
            {7002, L"このまま始める\nほかの PC から、この配布サーバーが見えないことがあります。"},
        };
        TASKDIALOGCONFIG c{};
        c.cbSize = sizeof(c);
        c.hwndParent = owner;
        c.hInstance = g_inst;
        c.dwFlags = TDF_USE_COMMAND_LINKS | TDF_ALLOW_DIALOG_CANCELLATION | TDF_POSITION_RELATIVE_TO_WINDOW;
        c.pszWindowTitle = kAppName;
        c.pszMainIcon = TD_WARNING_ICON;
        c.pszMainInstruction = head.c_str();
        c.pszContent = L"このままでは、ほかの PC からこの配布サーバーが見えません。"
                       L"カフェなどの公共の Wi-Fi では、パブリックのままにしてください。";
        c.cButtons = 2;
        c.pButtons = buttons;
        c.nDefaultButton = 7001;
        int button = 0;
        if (FAILED(TaskDialogIndirect(&c, &button, nullptr, nullptr)))
            button = (MessageBoxW(owner, (head + L"\n\nプライベートに切り替えますか？").c_str(), kAppName, MB_YESNO | MB_ICONWARNING) == IDYES) ? 7001 : 7002;
        if (button != 7001 && button != 7002) {
            StopServerSockets();
            return;  // キャンセル
        }
        if (button == 7001) {
            std::wstring msg;
            if (!SwitchToPrivate(owner, msg))
                MessageBoxW(owner, (L"プライベートに切り替えられませんでした。このまま始めます。\n\n" + msg).c_str(), kAppName, MB_OK | MB_ICONWARNING);
        }
    }

    for (int k = 0; k < kPkgCount; ++k) {
        ScanPackage(k);
        g_pkgs[k].text = PkgReadyText(k);
    }
    std::wstring err;
    if (!StartServerSockets(err)) {
        StopServerSockets();
        MessageBoxW(owner, (L"配布サーバーになれませんでした。\n\n" + err).c_str(), kAppName, MB_OK | MB_ICONWARNING);
        return;
    }
    g_serverWarn = OnPublicNetwork()
                       ? L"この PC のネットワークが「パブリック」のままです。ほかの PC から見えないことがあります。右の［プライベートに切り替える］を押してください。"
                       : L"";
    int d = Dpi(owner);
    RECT orc;
    GetWindowRect(owner, &orc);
    int w = MulDiv(820, d, 96), h = MulDiv(640, d, 96);
    g_server = CreateWindowExW(WS_EX_ACCEPTFILES, L"ManabitoShareServer", L"まなびと講習準備 ― 授業ファイルの配布サーバー",
                               WS_OVERLAPPEDWINDOW, orc.left + MulDiv(40, d, 96), orc.top + MulDiv(40, d, 96), w, h, owner, nullptr, g_inst,
                               nullptr);
    ShowWindow(g_server, SW_SHOW);
    LayoutServer(g_server);
}

void Release(unsigned session) {
    Lock l(&g_cs);
    if (g_pending == session) g_pending = 0;
}

std::wstring DestFolder() {
    std::wstring file = AppDataDir() + L"\\share_dest.txt";
    HANDLE h = CreateFileW(file.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h != INVALID_HANDLE_VALUE) {
        char buf[4096];
        DWORD n = 0;
        ReadFile(h, buf, sizeof(buf), &n, nullptr);
        CloseHandle(h);
        std::string s(buf, n);
        if (s.rfind("\xEF\xBB\xBF", 0) == 0) s.erase(0, 3);
        while (!s.empty() && (s.back() == '\r' || s.back() == '\n')) s.pop_back();
        if (!s.empty()) return FromUtf8(s);
    }
    PWSTR docs = nullptr;
    static const GUID kDocuments = {0xFDD39AD0, 0x238F, 0x46AF, {0xAD, 0xB4, 0x6C, 0x85, 0x48, 0x03, 0x69, 0xC7}};
    std::wstring base = L"C:\\";
    if (SUCCEEDED(SHGetKnownFolderPath(kDocuments, 0, nullptr, &docs)) && docs) base = docs;
    if (docs) CoTaskMemFree(docs);
    return base + L"\\授業ファイル";
}

void HandleOffer(HWND owner, Offer* offer) {
    Offer o = *offer;
    delete offer;
    std::wstring content = L"配布サーバー：" + o.server + L"\n配るもの：ファイル " + std::to_wstring(o.files) + L" 個・" + FormatBytes(o.bytes);
    TASKDIALOG_BUTTON buttons[] = {
        {6001, L"取得する\n合言葉を入れて、受け取るフォルダーを選びます。"},
        {6002, L"今回は取得しない"},
    };
    TASKDIALOGCONFIG c{};
    c.cbSize = sizeof(c);
    c.hwndParent = owner;
    c.hInstance = g_inst;
    c.dwFlags = TDF_USE_COMMAND_LINKS | TDF_ALLOW_DIALOG_CANCELLATION | TDF_POSITION_RELATIVE_TO_WINDOW;
    c.pszWindowTitle = kAppName;
    c.pszMainIcon = TD_INFORMATION_ICON;
    c.pszMainInstruction = L"LAN 内に配布サーバーがあります。ファイルを取得しますか？";
    c.pszContent = content.c_str();
    c.cButtons = 2;
    c.pButtons = buttons;
    c.nDefaultButton = 6001;
    int button = 0;
    if (FAILED(TaskDialogIndirect(&c, &button, nullptr, nullptr)))
        button = (MessageBoxW(owner, (content + L"\n\n取得しますか？").c_str(), kAppName, MB_YESNO | MB_ICONINFORMATION) == IDYES) ? 6001 : 6002;
    if (button != 6001) {
        MarkHandled(o.session);
        return;
    }
    std::wstring pass = AskPasscode(owner, o.server, L"");
    if (pass.empty()) {
        MarkHandled(o.session);
        return;
    }
    std::wstring dest = DestFolder();
    MakeDirs(dest);
    std::wstring picked = PickFolder(owner, dest);
    if (picked.empty()) {
        MarkHandled(o.session);
        return;
    }
    SaveDest(picked);
    StartDownload(owner, o, pass, picked);
}

}  // namespace share
