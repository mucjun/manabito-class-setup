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
#include <bcrypt.h>
#include <wincrypt.h>

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

// ================================================================ 先生用：署名・報告・プリンター（v0.17.0）
//
// 署名：Windows の CNG（ECDSA P-256・SHA-256）。先生の PC の秘密の鍵は DPAPI（この利用者だけが戻せる）で暗号化して
//       %LOCALAPPDATA%\ManabitoClassSetup\teacher_key.bin に置く。受講生の PC は、合言葉で確かめた先生の PC の公開の鍵を
//       teacher_pin.txt に覚え（登録）、プリンターの設定はその鍵で署名を確かめてから使う。

std::string Hex(const std::vector<BYTE>& v) {
    static const char* d = "0123456789abcdef";
    std::string s;
    s.reserve(v.size() * 2);
    for (BYTE b : v) {
        s.push_back(d[b >> 4]);
        s.push_back(d[b & 15]);
    }
    return s;
}

bool Unhex(const std::string& s, std::vector<BYTE>& out) {
    out.clear();
    if (s.size() % 2 != 0 || s.size() > 4096) return false;
    auto val = [](char c) -> int {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        return -1;
    };
    for (size_t i = 0; i < s.size(); i += 2) {
        int a = val(s[i]), b = val(s[i + 1]);
        if (a < 0 || b < 0) return false;
        out.push_back(static_cast<BYTE>(a * 16 + b));
    }
    return true;
}

bool Sha256(const BYTE* data, size_t n, std::vector<BYTE>& out) {
    out.assign(32, 0);
    BCRYPT_ALG_HANDLE alg = nullptr;
    BCRYPT_HASH_HANDLE h = nullptr;
    bool ok = BCRYPT_SUCCESS(BCryptOpenAlgorithmProvider(&alg, BCRYPT_SHA256_ALGORITHM, nullptr, 0)) &&
              BCRYPT_SUCCESS(BCryptCreateHash(alg, &h, nullptr, 0, nullptr, 0, 0)) &&
              BCRYPT_SUCCESS(BCryptHashData(h, const_cast<PUCHAR>(data), static_cast<ULONG>(n), 0)) &&
              BCRYPT_SUCCESS(BCryptFinishHash(h, out.data(), 32, 0));
    if (h) BCryptDestroyHash(h);
    if (alg) BCryptCloseAlgorithmProvider(alg, 0);
    return ok;
}

std::string Fingerprint(const std::vector<BYTE>& pub) {
    std::vector<BYTE> hsh;
    if (!Sha256(pub.data(), pub.size(), hsh)) return std::string();
    hsh.resize(8);
    return Hex(hsh);
}

bool ReadWholeFile(const std::wstring& path, std::vector<BYTE>& out, size_t limit = 1 << 20) {
    out.clear();
    HANDLE f = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f == INVALID_HANDLE_VALUE) return false;
    LARGE_INTEGER sz{};
    bool ok = GetFileSizeEx(f, &sz) && sz.QuadPart >= 0 && static_cast<unsigned long long>(sz.QuadPart) <= limit;
    if (ok) {
        out.resize(static_cast<size_t>(sz.QuadPart));
        DWORD got = 0;
        ok = out.empty() || (ReadFile(f, out.data(), static_cast<DWORD>(out.size()), &got, nullptr) && got == out.size());
    }
    CloseHandle(f);
    return ok;
}

// 書きかけのファイルが残らないよう、別名に書いてから入れ替える
bool WriteFileAtomic(const std::wstring& path, const void* data, size_t n) {
    std::wstring tmp = path + L".tmp";
    HANDLE f = CreateFileW(tmp.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f == INVALID_HANDLE_VALUE) return false;
    DWORD w = 0;
    bool ok = (n == 0) || (WriteFile(f, data, static_cast<DWORD>(n), &w, nullptr) && w == n);
    ok = FlushFileBuffers(f) && ok;
    CloseHandle(f);
    if (!ok) {
        DeleteFileW(tmp.c_str());
        return false;
    }
    if (!MoveFileExW(tmp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        DeleteFileW(tmp.c_str());
        return false;
    }
    return true;
}

// 公開の鍵の形（BCRYPT_ECCPUBLIC_BLOB・P-256）か
bool ValidPublicBlob(const std::vector<BYTE>& pub) {
    if (pub.size() != sizeof(BCRYPT_ECCKEY_BLOB) + 64) return false;
    const BCRYPT_ECCKEY_BLOB* h = reinterpret_cast<const BCRYPT_ECCKEY_BLOB*>(pub.data());
    return h->dwMagic == BCRYPT_ECDSA_PUBLIC_P256_MAGIC && h->cbKey == 32;
}

bool VerifySignature(const std::vector<BYTE>& pub, const std::string& data, const std::vector<BYTE>& sig) {
    if (!ValidPublicBlob(pub) || sig.size() != 64) return false;
    std::vector<BYTE> hsh;
    if (!Sha256(reinterpret_cast<const BYTE*>(data.data()), data.size(), hsh)) return false;
    BCRYPT_ALG_HANDLE alg = nullptr;
    BCRYPT_KEY_HANDLE key = nullptr;
    bool ok = BCRYPT_SUCCESS(BCryptOpenAlgorithmProvider(&alg, BCRYPT_ECDSA_P256_ALGORITHM, nullptr, 0)) &&
              BCRYPT_SUCCESS(BCryptImportKeyPair(alg, nullptr, BCRYPT_ECCPUBLIC_BLOB, &key, const_cast<PUCHAR>(pub.data()),
                                                 static_cast<ULONG>(pub.size()), 0)) &&
              BCRYPT_SUCCESS(BCryptVerifySignature(key, nullptr, hsh.data(), 32, const_cast<PUCHAR>(sig.data()), 64, 0));
    if (key) BCryptDestroyKey(key);
    if (alg) BCryptCloseAlgorithmProvider(alg, 0);
    return ok;
}

// 先生の PC の鍵（窓を開いている間だけ持つ）
struct TeacherKey {
    BCRYPT_ALG_HANDLE alg = nullptr;
    BCRYPT_KEY_HANDLE key = nullptr;
    std::vector<BYTE> pub;
    std::string fp;
};
TeacherKey g_tkey;

void CloseTeacherKey() {
    if (g_tkey.key) BCryptDestroyKey(g_tkey.key);
    if (g_tkey.alg) BCryptCloseAlgorithmProvider(g_tkey.alg, 0);
    g_tkey = TeacherKey();
}

bool LoadOrCreateTeacherKey(std::wstring& err) {
    if (g_tkey.key) return true;
    if (!BCRYPT_SUCCESS(BCryptOpenAlgorithmProvider(&g_tkey.alg, BCRYPT_ECDSA_P256_ALGORITHM, nullptr, 0))) {
        err = L"署名の仕組みを使えませんでした。";
        return false;
    }
    MakeDirs(AppDataDir());
    const std::wstring file = AppDataDir() + L"\\teacher_key.bin";
    std::vector<BYTE> enc;
    bool loaded = false;
    if (ReadWholeFile(file, enc, 64 * 1024) && !enc.empty()) {
        DATA_BLOB in{static_cast<DWORD>(enc.size()), enc.data()}, out{};
        if (CryptUnprotectData(&in, nullptr, nullptr, nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN, &out)) {
            loaded = BCRYPT_SUCCESS(BCryptImportKeyPair(g_tkey.alg, nullptr, BCRYPT_ECCPRIVATE_BLOB, &g_tkey.key, out.pbData, out.cbData, 0));
            SecureZeroMemory(out.pbData, out.cbData);
            LocalFree(out.pbData);
        }
        if (!loaded) {
            // 別の利用者・別の PC の鍵などで戻せない：作り直さず止める（受講生の PC の登録が無効になるのを、黙って起こさない）
            err = L"先生の PC の鍵（teacher_key.bin）を読めませんでした。別の利用者で作った鍵かもしれません。作り直すときは、そのファイルを消してから開き直し、受講生の PC を登録し直してください。";
            CloseTeacherKey();
            return false;
        }
    } else {
        if (!BCRYPT_SUCCESS(BCryptGenerateKeyPair(g_tkey.alg, &g_tkey.key, 256, 0)) || !BCRYPT_SUCCESS(BCryptFinalizeKeyPair(g_tkey.key, 0))) {
            err = L"先生の PC の鍵を作れませんでした。";
            CloseTeacherKey();
            return false;
        }
        ULONG n = 0;
        BCryptExportKey(g_tkey.key, nullptr, BCRYPT_ECCPRIVATE_BLOB, nullptr, 0, &n, 0);
        std::vector<BYTE> priv(n);
        bool exported = n > 0 && BCRYPT_SUCCESS(BCryptExportKey(g_tkey.key, nullptr, BCRYPT_ECCPRIVATE_BLOB, priv.data(), n, &n, 0));
        DATA_BLOB in{n, priv.data()}, out{};
        bool saved = exported && CryptProtectData(&in, L"ManabitoTeacherKey", nullptr, nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN, &out);
        SecureZeroMemory(priv.data(), priv.size());
        if (saved) {
            saved = WriteFileAtomic(file, out.pbData, out.cbData);
            LocalFree(out.pbData);
        }
        if (!saved) {
            err = L"先生の PC の鍵を保存できませんでした。";
            CloseTeacherKey();
            return false;
        }
    }
    ULONG n = 0;
    BCryptExportKey(g_tkey.key, nullptr, BCRYPT_ECCPUBLIC_BLOB, nullptr, 0, &n, 0);
    g_tkey.pub.assign(n, 0);
    if (n == 0 || !BCRYPT_SUCCESS(BCryptExportKey(g_tkey.key, nullptr, BCRYPT_ECCPUBLIC_BLOB, g_tkey.pub.data(), n, &n, 0)) ||
        !ValidPublicBlob(g_tkey.pub)) {
        err = L"先生の PC の鍵を読めませんでした。";
        CloseTeacherKey();
        return false;
    }
    g_tkey.fp = Fingerprint(g_tkey.pub);
    return !g_tkey.fp.empty();
}

bool SignWithTeacherKey(const std::string& data, std::vector<BYTE>& sig) {
    if (!g_tkey.key) return false;
    std::vector<BYTE> hsh;
    if (!Sha256(reinterpret_cast<const BYTE*>(data.data()), data.size(), hsh)) return false;
    ULONG n = 0;
    if (!BCRYPT_SUCCESS(BCryptSignHash(g_tkey.key, nullptr, hsh.data(), 32, nullptr, 0, &n, 0)) || n != 64) return false;
    sig.assign(n, 0);
    return BCRYPT_SUCCESS(BCryptSignHash(g_tkey.key, nullptr, hsh.data(), 32, sig.data(), n, &n, 0)) && n == 64;
}

// ---- 先生の PC：受講生の PC からの報告
struct RepItem {
    std::wstring id, state, name, current;
};
struct PcReport {
    std::wstring name, ip, version;
    int ok = 0, ng = 0, na = 0;
    std::vector<RepItem> items;
    DWORD tick = 0;
    std::wstring at;
    bool inspectReq = false;
};
std::map<std::wstring, PcReport> g_reports;  // PC 名ごと（g_cs で守る）
unsigned g_reportsVersion = 0;               // 報告が届くたびに増やす（画面は、変わったときだけ書き直す。g_cs で守る）
const size_t kMaxPcs = 200;

// ---- 先生の PC：配っているプリンターの設定（g_cs で守る）
std::string g_prnConfig;    // UTF-8 の本文（printers.txt と同じ形）
std::vector<BYTE> g_prnSig;  // 本文の署名
std::string g_prnVersion;   // 版（数字）

// ---- 受講生の PC：見つけた先生の PC（g_cs で守る）
struct TeacherSeen {
    std::wstring name, ip;
    int port = 0;
    std::string printersVer, fp;
    DWORD tick = 0;
};
TeacherSeen g_teacher;

// ---- 先生の PC：自動で配っているもの（g_cs で守る。v0.18.0）
struct PushFile {
    std::wstring full, rel;
    unsigned long long size = 0;
};
struct PushState {
    std::string id, manifest;
    std::vector<BYTE> sig;
    std::vector<PushFile> files;
    unsigned long long bytes = 0;
};
PushState g_push;            // g_cs で守る

struct Pin {
    std::wstring name;
    std::string fp;
    std::vector<BYTE> pub;
};

bool LoadPin(Pin& p) {
    std::vector<BYTE> b;
    if (!ReadWholeFile(AppDataDir() + L"\\teacher_pin.txt", b, 16 * 1024)) return false;
    std::string s(b.begin(), b.end());
    if (s.rfind("\xEF\xBB\xBF", 0) == 0) s.erase(0, 3);
    while (!s.empty() && (s.back() == '\r' || s.back() == '\n')) s.pop_back();
    auto f = SplitBar(s);
    if (f.size() < 3) return false;
    p.name = FromUtf8(f[0]);
    p.fp = f[1];
    if (!Unhex(f[2], p.pub) || !ValidPublicBlob(p.pub) || Fingerprint(p.pub) != p.fp) return false;
    return true;
}

bool SavePin(const std::wstring& name, const std::vector<BYTE>& pub) {
    if (!ValidPublicBlob(pub)) return false;
    std::wstring clean = name;
    for (auto& c : clean)
        if (c == L'|' || c < 0x20) c = L'_';
    std::string s = ToUtf8(clean) + "|" + Fingerprint(pub) + "|" + Hex(pub) + "\r\n";
    MakeDirs(AppDataDir());
    return WriteFileAtomic(AppDataDir() + L"\\teacher_pin.txt", s.data(), s.size());
}

// 受講生の PC：答え（UDP）から先生の PC を覚える。登録した先生の PC を優先する
void UpdateTeacherFromAnswer(const std::vector<std::string>& f, const sockaddr_in& from) {
    std::string printersVer, fp;
    for (size_t j = 6; j + 1 < f.size(); j += 2) {
        if (f[j].rfind("printers=", 0) == 0) printersVer = f[j].substr(9);
        if (f[j].rfind("teacher=", 0) == 0) fp = f[j].substr(8);
    }
    if (fp.size() != 16 || fp.find_first_not_of("0123456789abcdef") != std::string::npos) return;  // 先生用の答えではない（古い版の配布サーバー）
    const int port = atoi(f[2].c_str());
    if (port < 1 || port > 65535) return;
    char ip[64] = "";
    inet_ntop(AF_INET, &from.sin_addr, ip, sizeof(ip));
    Pin pin;
    const bool hasPin = LoadPin(pin);
    Lock l(&g_cs);
    const bool fresh = g_teacher.tick && (GetTickCount() - g_teacher.tick) < 30000;
    const bool currentIsPinned = hasPin && g_teacher.fp == pin.fp;
    const bool thisIsPinned = hasPin && fp == pin.fp;
    if (fresh && currentIsPinned && !thisIsPinned) return;  // 登録した先生の PC がいる間は、ほかに替えない
    g_teacher.name = FromUtf8(f[1]);
    g_teacher.ip = FromUtf8(ip);
    g_teacher.port = port;
    g_teacher.printersVer = printersVer;
    g_teacher.fp = fp;
    g_teacher.tick = GetTickCount();
}

// 画面に出す「この PC の印」：指紋の先頭 8 文字を 4 文字ずつ（例：1A2B-3C4D）
std::wstring ShortFingerprint(const std::string& fp) {
    if (fp.size() < 8) return L"――――";
    std::wstring s;
    for (size_t i = 0; i < 8; ++i) {
        if (i == 4) s += L'-';
        s += static_cast<wchar_t>(toupper(static_cast<unsigned char>(fp[i])));
    }
    return s;
}

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
            UpdateTeacherFromAnswer(f, from);  // 先生用なら覚える（報告の宛先）
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
            // 先生用の窓を開いている間は、配るものが無くても答える（受講生の PC が報告の宛先として見つけられるように）
            answer = std::string(kAnswerHead) + self + "|" + std::to_string(kTcpPort) + "|" + std::to_string(g_session) + "|" +
                     std::to_string(g_totalFiles) + "|" + std::to_string(g_totalBytes);
            for (int pk = 0; pk < kPkgCount; ++pk)  // |office=版|バイト|adobe=版|バイト
                answer += std::string("|") + g_pkgs[pk].tag + "=" + ToUtf8(g_pkgs[pk].version) + "|" + std::to_string(g_pkgs[pk].bytes);
            answer += "|printers=" + g_prnVersion + "|0|teacher=" + g_tkey.fp + "|0";  // 先生用の印（鍵が無ければ空）
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

// 合言葉の確かめ（失敗回数の制限つき）。0＝合っている、1＝違う、2＝続けて間違えたので止めている
int CheckPasscode(const std::wstring& ip, const std::wstring& pass) {
    Lock l(&g_cs);
    auto& fail = g_fails[ip];
    if (fail.first >= 5 && GetTickCount() - fail.second < 60000) return 2;
    if (!g_passcode.empty() && pass == g_passcode) {
        fail.first = 0;
        return 0;
    }
    fail.first++;
    fail.second = GetTickCount();
    return 1;
}

// 受講生の PC からの報告（合言葉なし。表示するだけで、先生の PC は何も実行しない）
//   REPORT|PC名|版|OK数|NG数|対象外数|行数  のあと  I|Id|判定|項目|今の状態  を行数分
//   ← OK|点検を頼むか（1/0）|プリンターの設定の版|先生の PC の印
void ServeReport(Conn& c, const std::wstring& ip, const std::vector<std::string>& f) {
    if (f.size() < 7) return;
    PcReport r;
    r.name = FromUtf8(f[1]).substr(0, 64);
    r.ip = ip;
    r.version = FromUtf8(f[2]).substr(0, 20);
    r.ok = atoi(f[3].c_str());
    r.ng = atoi(f[4].c_str());
    r.na = atoi(f[5].c_str());
    const int n = atoi(f[6].c_str());
    if (r.name.empty() || n < 0 || n > 300) return;
    for (wchar_t& ch : r.name)
        if (ch < 0x20) ch = L'_';
    std::string line;
    for (int i = 0; i < n; ++i) {
        if (!c.ReadLine(line, 4096)) return;
        auto g = SplitBar(line);
        if (g.size() < 5 || g[0] != "I") continue;
        r.items.push_back({FromUtf8(g[1]).substr(0, 64), FromUtf8(g[2]).substr(0, 8), FromUtf8(g[3]).substr(0, 120), FromUtf8(g[4]).substr(0, 200)});
    }
    r.tick = GetTickCount();
    SYSTEMTIME st{};
    GetLocalTime(&st);
    wchar_t at[16];
    swprintf(at, 16, L"%02d:%02d:%02d", st.wHour, st.wMinute, st.wSecond);
    r.at = at;
    bool inspect = false, full = false;
    std::string ver, fp, pushId;
    {
        Lock l(&g_cs);  // この中では送受信しない（ほかの PC と画面を待たせないため）
        auto it = g_reports.find(r.name);
        if (it == g_reports.end() && g_reports.size() >= kMaxPcs) {
            full = true;
        } else if (it != g_reports.end() && r.ok + r.ng + r.na == 0 && !it->second.items.empty()) {
            // 点検の前の報告（起動直後など）：前の結果は残し、時刻だけ更新する
            inspect = it->second.inspectReq;
            it->second.tick = r.tick;
            it->second.at = r.at;
            it->second.ip = ip;
            it->second.inspectReq = false;
            ++g_reportsVersion;
        } else {
            if (it != g_reports.end()) inspect = it->second.inspectReq;
            r.inspectReq = false;
            g_reports[r.name] = r;
            ++g_reportsVersion;
        }
        ver = g_prnVersion;
        fp = g_tkey.fp;
        pushId = g_push.id;
    }
    if (full) {
        c.SendLine("OK|0||");
        return;
    }
    c.SendLine(std::string("OK|") + (inspect ? "1" : "0") + "|" + ver + "|" + fp + "|" + pushId);
}

// 先生の PC の登録（合言葉が要る）：PAIR|合言葉|PC名  ← KEY|公開の鍵|先生の PC 名
void ServePair(Conn& c, const std::wstring& ip, const std::vector<std::string>& f) {
    if (f.size() < 3) return;
    int r = CheckPasscode(ip, FromUtf8(f[1]));
    if (r == 2) { c.SendLine("NG|合言葉を続けて間違えたため、1 分待ってからもう一度試してください。"); return; }
    if (r == 1) { c.SendLine("NG|合言葉が違います。"); return; }
    if (g_tkey.pub.empty()) { c.SendLine("NG|先生の PC の鍵がありません。"); return; }
    c.SendLine("KEY|" + Hex(g_tkey.pub) + "|" + ToUtf8(ComputerName()));
}

// プリンターの設定（合言葉なし。署名つき）：PRINTERS  ← PRN|版|長さ|署名  のあと本文
void ServePrinters(Conn& c) {
    std::string body, ver;
    std::vector<BYTE> sig;
    {
        Lock l(&g_cs);
        body = g_prnConfig;
        sig = g_prnSig;
        ver = g_prnVersion;
    }
    if (body.empty() || sig.size() != 64) { c.SendLine("ERR|配っているプリンターの設定はありません。"); return; }
    if (c.SendLine("PRN|" + ver + "|" + std::to_string(body.size()) + "|" + Hex(sig))) c.SendAll(body.data(), body.size());
}

void ServePushManifest(Conn& c);
void ServePushFiles(Conn& c, const std::wstring& ip, const std::vector<std::string>& first);

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
    if (f[0] == "REPORT" || f[0] == "PAIR" || f[0] == "PRINTERS" || f[0] == "PUSHMAN" || f[0] == "PUSH") {
        if (f[0] == "REPORT") ServeReport(c, ip, f);
        else if (f[0] == "PAIR") ServePair(c, ip, f);
        else if (f[0] == "PRINTERS") ServePrinters(c);
        else if (f[0] == "PUSHMAN") ServePushManifest(c);
        else ServePushFiles(c, ip, f);
        c.Close();
        return 0;
    }
    // 授業ファイル：合言葉を確かめる（確かめと送信は、ロックの外で）
    if (f.size() < 3 || f[0] != "HELLO") {
        c.Close();
        return 0;
    }
    const int pr = CheckPasscode(ip, FromUtf8(f[1]));
    if (pr != 0) {
        c.SendLine(pr == 2 ? "NG|合言葉を続けて間違えたため、1 分待ってからもう一度試してください。" : "NG|合言葉が違います。");
        c.Close();
        return 0;
    }
    {
        Lock l(&g_cs);
        id = g_nextClient++;
        ClientInfo ci;
        ci.name = FromUtf8(f[2]).substr(0, 64);
        ci.ip = ip;
        ci.state = L"つながりました";
        ci.total = g_totalBytes;
        ci.lastTick = GetTickCount();
        g_clients[id] = ci;
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
    IDS_TAB0,    // タブ：教室の PC（IDS_TAB0 + 1 = 配る、+ 2 = プリンター）
    IDS_TAB1,
    IDS_TAB2,
    IDS_PCS,         // 教室の PC の一覧
    IDS_PCDETAIL,    // 選んだ PC の詳しい様子
    IDS_INSPECT,     // ［いますぐ点検してもらう］
    IDS_PRINTERS,    // この PC のプリンター（チェックつき）
    IDS_PRN_DEFAULT, // 既定にする
    IDS_PRN_SHARE,   // ［チェックしたプリンターを配る］
    IDS_PRN_RELOAD,  // ［一覧を読み直す］
    IDS_PRN_STATUS,  // 教室の PC のプリンターの状態
    IDS_PUSH,        // ［登録済みの PC に自動で配る］／［自動で配るのをやめる］
    IDS_MAN_NAME,    // IP アドレスで加える：名前
    IDS_MAN_HOST,    //   IP アドレス（ホスト名）
    IDS_MAN_PORT,    //   ポート
    IDS_MAN_PATH,    //   パス
    IDS_MAN_TEST,    //   ［つながるか確かめる］
    IDS_MAN_ADD,     //   ［一覧に加える］
    IDS_MAN_REMOVE,  //   ［選んだものを外す］
};
const UINT_PTR kServerTimer = 11;
HFONT g_sfont = nullptr, g_sbig = nullptr, g_sbold = nullptr;
std::wstring g_serverWarn;

void RefreshSourceList(HWND hwnd) {
    HWND lb = GetDlgItem(hwnd, IDS_SOURCES);
    SendMessageW(lb, LB_RESETCONTENT, 0, 0);
    std::vector<Source> list;
    {
        Lock l(&g_cs);  // 写してから離す（画面への書き込みのあいだ、受け渡しを待たせない）
        list = g_sources;
    }
    for (const auto& src : list) {
        std::wstring t = src.name + L"　（" + std::to_wstring(src.files) + L" 個・" + FormatBytes(src.size) + L"）　" + src.path;
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
        InvalidateRect(hwnd, nullptr, FALSE);  // 裏で描いてから写すので、背景は消さない（ちらつかない）
    }
}

// ---------------------------------------------------------------- 先生用の窓：タブ・教室の PC・プリンター

int g_stab = 0;  // 0＝教室の PC、1＝配る、2＝プリンター
const UINT kPrnListDone = WM_APP + 61;

struct PrnRow {
    std::wstring name, host, proto, driver, why;
    std::wstring path;     // IPP のパス（例 /ipp/print）
    int port = 0;
    bool def = false, ok = false;
    bool manual = false;   // IP アドレスで加えたもの（先生の PC には入っていない）
};
std::vector<PrnRow> g_prnRows;    // 画面のスレッドだけで使う
std::vector<PrnRow> g_prnManual;  // IP アドレスで加えたもの（printers_manual.txt に覚える）
bool g_prnManualLoaded = false;
std::wstring g_prnCheckNew;       // 次に一覧を作るとき、チェックを付けておく名前（加えたばかりのもの）
const wchar_t* kIppDriver = L"Microsoft IPP Class Driver";  // Windows に入っている IPP のドライバー
bool g_prnLoading = false;
std::wstring g_prnText;  // ［プリンター］タブの「配っている設定」の文

// レイアウトの位置（LayoutServer が決め、PaintServer が使う）
struct ServerLayout {
    int tabTop = 0, contentTop = 0, prnRowTop = 0, prnStatusLabelTop = 0, manRowTop = 0, prnTextTop = 0;
} g_sl;

const wchar_t* kTabNames[3] = {L"教室の PC", L"配る", L"プリンター"};

std::string NowVersion() {
    FILETIME ft{};
    GetSystemTimeAsFileTime(&ft);
    ULARGE_INTEGER u{};
    u.LowPart = ft.dwLowDateTime;
    u.HighPart = ft.dwHighDateTime;
    return std::to_string(u.QuadPart / 10000000ULL);  // 1601 年からの秒（増えていく数）
}

// 配っているプリンターの設定を、先生の PC に保存（開き直しても続けて配れるように）・読み込む
void SaveSharedPrinters() {
    std::string body, ver;
    std::vector<BYTE> sig;
    {
        Lock l(&g_cs);
        body = g_prnConfig;
        sig = g_prnSig;
        ver = g_prnVersion;
    }
    MakeDirs(AppDataDir());
    WriteFileAtomic(AppDataDir() + L"\\printers_shared.txt", body.data(), body.size());
    std::string s = Hex(sig) + "\r\n";
    WriteFileAtomic(AppDataDir() + L"\\printers_shared.sig", s.data(), s.size());
}

std::wstring DescribeSharedPrinters() {
    std::string body, ver;
    {
        Lock l(&g_cs);
        body = g_prnConfig;
        ver = g_prnVersion;
    }
    if (body.empty()) return L"まだ配っていません。チェックして［チェックしたプリンターを配る］を押してください。";
    std::wstring names, def;
    size_t pos = 0;
    while (pos < body.size()) {
        size_t nl = body.find('\n', pos);
        std::string line = body.substr(pos, nl == std::string::npos ? std::string::npos : nl - pos);
        pos = nl == std::string::npos ? body.size() : nl + 1;
        while (!line.empty() && line.back() == '\r') line.pop_back();
        auto f = SplitBar(line);
        if (f.size() >= 7 && f[0] == "p") {
            names += (names.empty() ? L"" : L"・") + FromUtf8(f[1]);
            if (f[6] == "1") def = FromUtf8(f[1]);
        }
    }
    std::wstring t = L"配っている設定：" + names + (def.empty() ? L"（既定は変えない）" : L"（既定：" + def + L"）") + L"　版 " + FromUtf8(ver);
    return t;
}

void LoadSharedPrinters() {
    std::vector<BYTE> body, sigText, sig;
    if (!ReadWholeFile(AppDataDir() + L"\\printers_shared.txt", body, 64 * 1024) || body.empty()) return;
    if (!ReadWholeFile(AppDataDir() + L"\\printers_shared.sig", sigText, 4096)) return;
    std::string st(sigText.begin(), sigText.end());
    while (!st.empty() && (st.back() == '\r' || st.back() == '\n')) st.pop_back();
    std::string b(body.begin(), body.end());
    // 自分の鍵の署名か確かめる（鍵を作り直したあとの古い設定は配らない）
    if (!Unhex(st, sig) || !VerifySignature(g_tkey.pub, b, sig)) return;
    auto first = SplitBar(b.substr(0, b.find('\n')));
    std::string ver = first.size() >= 2 && first[0] == "v" ? first[1] : "";
    while (!ver.empty() && ver.back() == '\r') ver.pop_back();
    Lock l(&g_cs);
    g_prnConfig = b;
    g_prnSig = sig;
    g_prnVersion = ver;
}

struct PrnListJob {
    HWND hwnd;
    std::wstring out;
};

DWORD WINAPI PrnListThread(LPVOID param) {
    PrnListJob* job = static_cast<PrnListJob*>(param);
    wchar_t exe[MAX_PATH * 2];
    DWORD n = GetModuleFileNameW(nullptr, exe, MAX_PATH * 2);
    std::wstring dir(exe, n);
    dir = dir.substr(0, dir.find_last_of(L'\\'));
    wchar_t sys[MAX_PATH];
    UINT sn = GetSystemDirectoryW(sys, MAX_PATH);
    std::wstring ps = std::wstring(sn ? sys : L"C:\\Windows\\System32") + L"\\WindowsPowerShell\\v1.0\\powershell.exe";
    std::wstring cmd = L"\"" + ps + L"\" -NoProfile -NonInteractive -ExecutionPolicy Bypass -File \"" + dir + L"\\scripts\\printers_list.ps1\" -OutFile \"" +
                       job->out + L"\"";
    std::vector<wchar_t> buf(cmd.begin(), cmd.end());
    buf.push_back(L'\0');
    STARTUPINFOW si{};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;
    PROCESS_INFORMATION pi{};
    if (CreateProcessW(nullptr, buf.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi)) {
        if (WaitForSingleObject(pi.hProcess, 90000) == WAIT_TIMEOUT) TerminateProcess(pi.hProcess, 1);
        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
    }
    if (!IsWindow(job->hwnd) || !PostMessageW(job->hwnd, kPrnListDone, 0, reinterpret_cast<LPARAM>(job))) delete job;
    return 0;
}

// ---- IP アドレスで加えるプリンター（v0.19.2）
void FillPrinterList(HWND hwnd);  // 後で定義

void LoadManualPrinters() {
    if (g_prnManualLoaded) return;
    g_prnManualLoaded = true;
    std::vector<BYTE> b;
    if (!ReadWholeFile(AppDataDir() + L"\\printers_manual.txt", b, 64 * 1024)) return;
    std::string t(b.begin(), b.end());
    size_t pos = 0;
    while (pos < t.size()) {
        size_t nl = t.find('\n', pos);
        std::string line = t.substr(pos, nl == std::string::npos ? std::string::npos : nl - pos);
        pos = nl == std::string::npos ? t.size() : nl + 1;
        while (!line.empty() && line.back() == '\r') line.pop_back();
        auto f = SplitBar(line);
        if (f.size() < 5 || f[0] != "m") continue;
        PrnRow r;
        r.name = FromUtf8(f[1]);
        r.host = FromUtf8(f[2]);
        r.port = atoi(f[3].c_str());
        r.path = FromUtf8(f[4]);
        r.proto = L"ipp";
        r.driver = kIppDriver;
        r.ok = r.manual = true;
        g_prnManual.push_back(r);
    }
}

void SaveManualPrinters() {
    std::string t;
    for (const auto& r : g_prnManual) t += "m|" + ToUtf8(r.name) + "|" + ToUtf8(r.host) + "|" + std::to_string(r.port) + "|" + ToUtf8(r.path) + "\r\n";
    MakeDirs(AppDataDir());
    WriteFileAtomic(AppDataDir() + L"\\printers_manual.txt", t.data(), t.size());
}

// 入力を確かめる。間違いがあれば、画面に出す文を返す（正しければ空）
std::wstring CheckManualPrinter(const std::wstring& name, const std::wstring& host, const std::wstring& portText, const std::wstring& path, int& port) {
    if (name.empty() || name.size() > 60) return L"名前を 1〜60 文字で入れてください。";
    for (wchar_t c : name)
        if (c < 0x20 || c == L'|' || c == L'\\' || c == L',' || c == L'!') return L"名前に「|」「\\」「,」「!」は使えません。";
    if (host.empty() || host.size() > 253) return L"IP アドレス（またはホスト名）を入れてください。";
    for (wchar_t c : host)
        if (!((c >= L'0' && c <= L'9') || (c >= L'a' && c <= L'z') || (c >= L'A' && c <= L'Z') || c == L'.' || c == L'-'))
            return L"IP アドレス（ホスト名）に使えない文字があります（使えるのは英数字と「.」「-」）。";
    if (host[0] == L'.' || host[0] == L'-') return L"IP アドレス（ホスト名）の形が違います。";
    port = _wtoi(portText.c_str());
    if (port < 1 || port > 65535 || portText.find_first_not_of(L"0123456789") != std::wstring::npos) return L"ポートは 1〜65535 の数字で入れてください（ふつうは 631）。";
    if (path.empty() || path[0] != L'/' || path.size() > 100) return L"パスは「/」で始めてください（ふつうは /ipp/print）。";
    for (wchar_t c : path)
        if (!((c >= L'0' && c <= L'9') || (c >= L'a' && c <= L'z') || (c >= L'A' && c <= L'Z') || c == L'/' || c == L'_' || c == L'-' || c == L'.'))
            return L"パスに使えない文字があります（使えるのは英数字と「/」「_」「-」「.」）。";
    for (const auto& r : g_prnRows)
        if (_wcsicmp(r.name.c_str(), name.c_str()) == 0) return L"同じ名前のプリンターが、もう一覧にあります。";
    return std::wstring();
}

// つながるか（TCP で 2 秒まで）
bool TestTcp(const std::wstring& host, int port) {
    ADDRINFOW hints{}, *res = nullptr;
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    if (GetAddrInfoW(host.c_str(), std::to_wstring(port).c_str(), &hints, &res) != 0 || !res) return false;
    bool ok = false;
    SOCKET sk = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (sk != INVALID_SOCKET) {
        u_long nb = 1;
        ioctlsocket(sk, FIONBIO, &nb);
        connect(sk, res->ai_addr, static_cast<int>(res->ai_addrlen));
        fd_set ws, es;
        FD_ZERO(&ws);
        FD_ZERO(&es);
        FD_SET(sk, &ws);
        FD_SET(sk, &es);
        timeval tv{2, 0};
        ok = select(0, nullptr, &ws, &es, &tv) > 0 && FD_ISSET(sk, &ws) && !FD_ISSET(sk, &es);
        closesocket(sk);
    }
    FreeAddrInfoW(res);
    return ok;
}

std::wstring DlgText(HWND hwnd, int id) {
    HWND h = GetDlgItem(hwnd, id);
    std::wstring t(static_cast<size_t>(GetWindowTextLengthW(h)) + 1, L'\0');
    GetWindowTextW(h, &t[0], static_cast<int>(t.size()));
    t.resize(wcslen(t.c_str()));
    while (!t.empty() && t.back() == L' ') t.pop_back();
    while (!t.empty() && t.front() == L' ') t.erase(0, 1);
    return t;
}

void ManualTest(HWND hwnd) {
    int port = 0;
    const std::wstring host = DlgText(hwnd, IDS_MAN_HOST), portText = DlgText(hwnd, IDS_MAN_PORT);
    std::wstring err = CheckManualPrinter(L"x", host, portText, L"/", port);
    if (!err.empty()) {
        MessageBoxW(hwnd, err.c_str(), kAppName, MB_OK | MB_ICONINFORMATION);
        return;
    }
    HCURSOR old = SetCursor(LoadCursorW(nullptr, IDC_WAIT));
    const bool ok = TestTcp(host, port);
    SetCursor(old);
    MessageBoxW(hwnd,
                ok ? (host + L"（ポート " + portText + L"）につながりました。IPP で印刷できる見込みが高いです。").c_str()
                   : (host + L"（ポート " + portText + L"）につながりませんでした。\n\nIP アドレス・ポートが正しいか、プリンターの電源と、プリンターの設定で IPP が有効になっているかを確かめてください。").c_str(),
                kAppName, MB_OK | (ok ? MB_ICONINFORMATION : MB_ICONWARNING));
}

void ManualAdd(HWND hwnd) {
    int port = 0;
    const std::wstring name = DlgText(hwnd, IDS_MAN_NAME), host = DlgText(hwnd, IDS_MAN_HOST), portText = DlgText(hwnd, IDS_MAN_PORT),
                       path = DlgText(hwnd, IDS_MAN_PATH);
    std::wstring err = CheckManualPrinter(name, host, portText, path, port);
    if (!err.empty()) {
        MessageBoxW(hwnd, err.c_str(), kAppName, MB_OK | MB_ICONINFORMATION);
        return;
    }
    PrnRow r;
    r.name = name;
    r.host = host;
    r.port = port;
    r.path = path;
    r.proto = L"ipp";
    r.driver = kIppDriver;
    r.ok = r.manual = true;
    g_prnManual.push_back(r);
    SaveManualPrinters();
    g_prnRows.push_back(r);
    g_prnCheckNew = name;  // 加えたものにはチェックを付けておく
    FillPrinterList(hwnd);
    SetDlgItemTextW(hwnd, IDS_MAN_NAME, L"");
    SetDlgItemTextW(hwnd, IDS_MAN_HOST, L"");
    MessageBoxW(hwnd, (L"「" + name + L"」を一覧に加えました（チェックを付けました）。［チェックしたプリンターを配る］で配ります。").c_str(), kAppName,
                MB_OK | MB_ICONINFORMATION);
}

void ManualRemove(HWND hwnd) {
    HWND lv = GetDlgItem(hwnd, IDS_PRINTERS);
    int sel = ListView_GetNextItem(lv, -1, LVNI_SELECTED);
    if (sel < 0) {
        MessageBoxW(hwnd, L"外すプリンター（IP アドレスで加えたもの）を、一覧で選んでください。", kAppName, MB_OK | MB_ICONINFORMATION);
        return;
    }
    LVITEMW it{};
    it.mask = LVIF_PARAM;
    it.iItem = sel;
    ListView_GetItem(lv, &it);
    size_t ri = static_cast<size_t>(it.lParam);
    if (ri >= g_prnRows.size() || !g_prnRows[ri].manual) {
        MessageBoxW(hwnd, L"外せるのは、IP アドレスで加えたものだけです（この PC に入っているプリンターは外せません）。", kAppName, MB_OK | MB_ICONINFORMATION);
        return;
    }
    const std::wstring name = g_prnRows[ri].name;
    g_prnManual.erase(std::remove_if(g_prnManual.begin(), g_prnManual.end(), [&](const PrnRow& r) { return r.name == name; }), g_prnManual.end());
    SaveManualPrinters();
    g_prnRows.erase(g_prnRows.begin() + static_cast<std::ptrdiff_t>(ri));
    FillPrinterList(hwnd);
    MessageBoxW(hwnd, (L"「" + name + L"」を一覧から外しました。配るのをやめるときは、［チェックしたプリンターを配る］で配り直してください（受講生の PC に入ったプリンターは消しません）。").c_str(),
                kAppName, MB_OK | MB_ICONINFORMATION);
}

void StartPrinterList(HWND hwnd) {
    if (g_prnLoading) return;
    MakeDirs(AppDataDir());
    PrnListJob* job = new PrnListJob{hwnd, AppDataDir() + L"\\printers_list.txt"};
    DeleteFileW(job->out.c_str());
    HANDLE th = CreateThread(nullptr, 0, PrnListThread, job, 0, nullptr);
    if (!th) {
        delete job;
        return;
    }
    CloseHandle(th);
    g_prnLoading = true;
    g_prnText = L"この PC のプリンターを調べています…";
    InvalidateRect(hwnd, nullptr, TRUE);
}

void FillPrinterList(HWND hwnd) {
    HWND lv = GetDlgItem(hwnd, IDS_PRINTERS);
    HWND cb = GetDlgItem(hwnd, IDS_PRN_DEFAULT);
    ListView_DeleteAllItems(lv);
    SendMessageW(cb, CB_RESETCONTENT, 0, 0);
    SendMessageW(cb, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"変えない（受講生の PC の既定のまま）"));
    // 今配っている設定のプリンター名（チェックの初期値）
    std::set<std::wstring> shared;
    std::wstring sharedDef;
    {
        Lock l(&g_cs);
        size_t pos = 0;
        while (pos < g_prnConfig.size()) {
            size_t nl = g_prnConfig.find('\n', pos);
            std::string line = g_prnConfig.substr(pos, nl == std::string::npos ? std::string::npos : nl - pos);
            pos = nl == std::string::npos ? g_prnConfig.size() : nl + 1;
            while (!line.empty() && line.back() == '\r') line.pop_back();
            auto f = SplitBar(line);
            if (f.size() >= 7 && f[0] == "p") {
                shared.insert(FromUtf8(f[1]));
                if (f[6] == "1") sharedDef = FromUtf8(f[1]);
            }
        }
    }
    int defSel = 0;
    for (size_t i = 0; i < g_prnRows.size(); ++i) {
        const auto& p = g_prnRows[i];
        std::wstring how = p.ok ? (p.proto == L"ipp" ? L"IPP（" : L"ネットワーク（") + p.host + L":" + std::to_wstring(p.port) + (p.proto == L"ipp" ? p.path : L"") + L"）" : L"―";
        if (p.manual) how += L"・IP アドレスで加えたもの";
        LVITEMW it{};
        it.mask = LVIF_TEXT | LVIF_PARAM;
        it.iItem = static_cast<int>(i);
        it.pszText = const_cast<LPWSTR>(p.name.c_str());
        it.lParam = static_cast<LPARAM>(i);
        int idx = ListView_InsertItem(lv, &it);
        ListView_SetItemText(lv, idx, 1, const_cast<LPWSTR>(how.c_str()));
        ListView_SetItemText(lv, idx, 2, const_cast<LPWSTR>(p.driver.c_str()));
        std::wstring can = p.ok ? std::wstring(L"配れます") + (p.def ? L"（この PC の既定）" : L"") : p.why;
        ListView_SetItemText(lv, idx, 3, const_cast<LPWSTR>(can.c_str()));
        if (p.ok) {
            if (shared.count(p.name) || p.name == g_prnCheckNew) ListView_SetCheckState(lv, idx, TRUE);
            int ci = static_cast<int>(SendMessageW(cb, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(p.name.c_str())));
            SendMessageW(cb, CB_SETITEMDATA, ci, static_cast<LPARAM>(i));
            if ((!sharedDef.empty() && p.name == sharedDef) || (sharedDef.empty() && shared.empty() && p.def)) defSel = ci;
        }
    }
    SendMessageW(cb, CB_SETCURSEL, defSel, 0);
    g_prnCheckNew.clear();
}

void FinishPrinterList(HWND hwnd, PrnListJob* job) {
    g_prnLoading = false;
    g_prnRows.clear();
    std::vector<BYTE> b;
    std::wstring err;
    if (!ReadWholeFile(job->out, b, 1 << 20)) {
        err = L"この PC のプリンターを調べられませんでした。";
    } else {
        std::string s(b.begin(), b.end());
        if (s.rfind("\xEF\xBB\xBF", 0) == 0) s.erase(0, 3);
        size_t pos = 0;
        while (pos < s.size()) {
            size_t nl = s.find('\n', pos);
            std::string line = s.substr(pos, nl == std::string::npos ? std::string::npos : nl - pos);
            pos = nl == std::string::npos ? s.size() : nl + 1;
            while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) line.pop_back();
            auto f = SplitBar(line);
            if (f.size() >= 2 && f[0] == "error") err = L"この PC のプリンターを調べられませんでした：" + FromUtf8(f[1]);
            if (f.size() < 9 || f[0] != "printer") continue;
            PrnRow r;
            r.name = FromUtf8(f[1]);
            r.host = FromUtf8(f[2]);
            r.proto = FromUtf8(f[3]);
            r.port = atoi(f[4].c_str());
            r.driver = FromUtf8(f[5]);
            r.def = (f[6] == "1");
            r.ok = (f[7] == "1") && !r.host.empty() && r.port > 0 && r.port < 65536 && (r.proto == L"raw" || r.proto == L"ipp");
            r.why = FromUtf8(f[8]);
            r.path = (f.size() >= 10 && !f[9].empty()) ? FromUtf8(f[9]) : L"/ipp/print";
            g_prnRows.push_back(r);
        }
    }
    DeleteFileW(job->out.c_str());
    delete job;
    LoadManualPrinters();
    for (const auto& m : g_prnManual) {  // IP アドレスで加えたもの（この PC にある同じ名前のものを優先）
        bool dup = false;
        for (const auto& r : g_prnRows) dup = dup || _wcsicmp(r.name.c_str(), m.name.c_str()) == 0;
        if (!dup) g_prnRows.push_back(m);
    }
    FillPrinterList(hwnd);
    g_prnText = err.empty() ? DescribeSharedPrinters() : err;
    InvalidateRect(hwnd, nullptr, TRUE);
}

// ［チェックしたプリンターを配る］：本文を作って署名し、配る設定を入れ替える
void SharePrinters(HWND hwnd) {
    if (!g_tkey.key) {
        MessageBoxW(hwnd, L"先生の PC の鍵が無いため、プリンターの設定を配れません。", kAppName, MB_OK | MB_ICONWARNING);
        return;
    }
    HWND lv = GetDlgItem(hwnd, IDS_PRINTERS);
    HWND cb = GetDlgItem(hwnd, IDS_PRN_DEFAULT);
    int defSel = static_cast<int>(SendMessageW(cb, CB_GETCURSEL, 0, 0));
    std::wstring defName;
    if (defSel > 0) {
        size_t di = static_cast<size_t>(SendMessageW(cb, CB_GETITEMDATA, defSel, 0));
        if (di < g_prnRows.size()) defName = g_prnRows[di].name;
    }
    std::vector<const PrnRow*> pick;
    std::wstring skipped;
    int count = ListView_GetItemCount(lv);
    for (int i = 0; i < count; ++i) {
        if (!ListView_GetCheckState(lv, i)) continue;
        LVITEMW it{};
        it.mask = LVIF_PARAM;
        it.iItem = i;
        ListView_GetItem(lv, &it);
        size_t ri = static_cast<size_t>(it.lParam);
        if (ri >= g_prnRows.size()) continue;
        if (!g_prnRows[ri].ok) {
            skipped += (skipped.empty() ? L"" : L"・") + g_prnRows[ri].name;
            continue;
        }
        pick.push_back(&g_prnRows[ri]);
    }
    if (!defName.empty()) {
        bool inPick = false;
        for (auto* p : pick) inPick = inPick || p->name == defName;
        if (!inPick) {
            MessageBoxW(hwnd, (L"既定にする「" + defName + L"」に、チェックが付いていません。").c_str(), kAppName, MB_OK | MB_ICONINFORMATION);
            return;
        }
    }
    std::wstring ask = pick.empty() ? L"チェックが無いため、プリンターの設定を配るのをやめます（受講生の PC に入っているプリンターは消しません）。よろしいですか？"
                                    : L"次のプリンターの設定を、教室の PC に配ります。\n\n";
    for (auto* p : pick) ask += L"・" + p->name + (p->name == defName ? L"（既定にする）" : L"") + L"\n";
    if (!skipped.empty()) ask += L"\n配れないため外したもの：" + skipped + L"\n";
    if (!pick.empty()) ask += L"\n受講生の PC は、［まとめて実行］か［設定をそろえる］のときに入れます。配りますか？";
    if (MessageBoxW(hwnd, ask.c_str(), kAppName, MB_YESNO | MB_ICONQUESTION) != IDYES) return;
    std::string body;
    std::vector<BYTE> sig;
    std::string ver = NowVersion();
    if (!pick.empty()) {
        body = "v|" + ver + "\r\n";
        for (auto* p : pick) {
            auto clean = [](std::wstring t) {
                for (auto& ch : t)
                    if (ch == L'|' || ch < 0x20) ch = L' ';
                return ToUtf8(t);
            };
            body += "p|" + clean(p->name) + "|" + clean(p->host) + "|" + clean(p->proto) + "|" + std::to_string(p->port) + "|" + clean(p->driver) + "|" +
                    (p->name == defName ? "1" : "0") + "|" + (p->proto == L"ipp" ? clean(p->path) : std::string()) + "\r\n";
        }
        if (!SignWithTeacherKey(body, sig)) {
            MessageBoxW(hwnd, L"署名できませんでした。配るのをやめます。", kAppName, MB_OK | MB_ICONWARNING);
            return;
        }
    }
    {
        Lock l(&g_cs);
        g_prnConfig = body;
        g_prnSig = sig;
        g_prnVersion = pick.empty() ? std::string() : ver;
    }
    SaveSharedPrinters();
    g_prnText = DescribeSharedPrinters();
    InvalidateRect(hwnd, nullptr, TRUE);
}

// ---- ［教室の PC］の一覧
std::wstring ItemCurrent(const PcReport& r, const wchar_t* id) {
    for (const auto& it : r.items)
        if (it.id == id) return it.current;
    return L"―";
}

// 0＝準備できた、1＝要対応、2＝応答なし、3＝点検前
int PcStatus(const PcReport& r, DWORD now) {
    if (now - r.tick > 180000) return 2;
    if (r.ok + r.ng + r.na == 0) return 3;
    return r.ng > 0 ? 1 : 0;
}

// 一覧のセルの文字を、前と違うときだけ書き換える（同じ文字を書くだけでも一覧は描き直されて、ちらつくため）
void SetCellText(HWND lv, int row, int col, const std::wstring& text) {
    wchar_t cur[512] = L"";
    ListView_GetItemText(lv, row, col, cur, 512);
    if (text.size() < 511 && text == cur) return;
    ListView_SetItemText(lv, row, col, const_cast<LPWSTR>(text.c_str()));
}

// 行の数を合わせる（足りなければ空の行を足し、多ければ消す）
void EnsureRowCount(HWND lv, int n) {
    while (ListView_GetItemCount(lv) > n) ListView_DeleteItem(lv, ListView_GetItemCount(lv) - 1);
    while (ListView_GetItemCount(lv) < n) {
        LVITEMW it{};
        it.mask = LVIF_TEXT | LVIF_PARAM;
        it.iItem = ListView_GetItemCount(lv);
        it.pszText = const_cast<LPWSTR>(L"");
        it.lParam = -1;
        ListView_InsertItem(lv, &it);
    }
}

bool g_refreshingPcs = false;  // 一覧を作り直している最中（選び直しの知らせで、作り直しを繰り返さないように）

// 前回の書き直しのときの印（変わっていなければ書き直さない）
unsigned g_pcsShownVersion = ~0u;
DWORD g_pcsShownTick = 0;
std::wstring g_pcsSummaryShown;

// 集計の文（画面の上）
std::wstring PcsSummaryText() {
    int total = 0, ok = 0, warn = 0, off = 0, before = 0;
    {
        Lock l(&g_cs);
        const DWORD now = GetTickCount();
        for (const auto& kv : g_reports) {
            ++total;
            switch (PcStatus(kv.second, now)) {
                case 0: ++ok; break;
                case 1: ++warn; break;
                case 2: ++off; break;
                default: ++before; break;
            }
        }
    }
    if (total == 0) return L"まだ報告がありません。受講生の PC で、まなびと講習準備を開いてもらってください（開いていれば 1 分以内に知らせます）。";
    return std::to_wstring(total) + L" 台　　準備できた " + std::to_wstring(ok) + L"　・　要対応 " + std::to_wstring(warn) + L"　・　応答なし " +
           std::to_wstring(off) + (before ? L"　・　点検前 " + std::to_wstring(before) : L"");
}

// force：選んだ行が変わったときなど、印に関係なく書き直す
void RefreshPcs(HWND hwnd, bool force = false) {
    if (g_refreshingPcs) return;
    unsigned ver;
    {
        Lock l(&g_cs);
        ver = g_reportsVersion;
    }
    const DWORD nowTick = GetTickCount();
    // 報告が届いていなければ、10 秒に 1 回だけ（「応答なし」への変わり目のため）
    if (!force && ver == g_pcsShownVersion && nowTick - g_pcsShownTick < 10000) return;
    g_pcsShownVersion = ver;
    g_pcsShownTick = nowTick;
    g_refreshingPcs = true;
    HWND lv = GetDlgItem(hwnd, IDS_PCS);
    std::vector<PcReport> list;
    {
        Lock l(&g_cs);
        for (const auto& kv : g_reports) list.push_back(kv.second);
    }
    const DWORD now = GetTickCount();
    std::wstring selName;
    int sel = ListView_GetNextItem(lv, -1, LVNI_SELECTED);
    if (sel >= 0) {
        wchar_t b0[128] = L"";
        ListView_GetItemText(lv, sel, 0, b0, 128);
        selName = b0;
    }
    EnsureRowCount(lv, static_cast<int>(list.size()));
    static const wchar_t* kStatus[4] = {L"準備できた", L"要対応", L"応答なし", L"点検前"};
    for (size_t i = 0; i < list.size(); ++i) {
        const auto& r = list[i];
        const int st = PcStatus(r, now);
        std::wstring issues;
        for (const auto& it : r.items)
            if (it.state == L"NG" || it.state == L"ERR") issues += (issues.empty() ? L"" : L"・") + it.name;
        if (issues.empty()) issues = (st == 3) ? L"（まだ点検していません）" : L"なし";
        if (st == 2) issues = L"―";
        const std::wstring cols[8] = {r.name, kStatus[st], issues, ItemCurrent(r, L"office"), ItemCurrent(r, L"health-disk"), ItemCurrent(r, L"health-clock"),
                                      ItemCurrent(r, L"health-battery"), r.at};
        const int row = static_cast<int>(i);
        // 色分けに使う lParam（状態）も、変わったときだけ
        LVITEMW cur{};
        cur.mask = LVIF_PARAM;
        cur.iItem = row;
        ListView_GetItem(lv, &cur);
        if (cur.lParam != st) {
            LVITEMW it{};
            it.mask = LVIF_PARAM;
            it.iItem = row;
            it.lParam = st;
            ListView_SetItem(lv, &it);
            ListView_RedrawItems(lv, row, row);
        }
        for (int c = 0; c < 8; ++c) SetCellText(lv, row, c, cols[c]);
        const UINT want = (r.name == selName) ? LVIS_SELECTED : 0;
        if ((ListView_GetItemState(lv, row, LVIS_SELECTED) & LVIS_SELECTED) != want) ListView_SetItemState(lv, row, want, LVIS_SELECTED);
    }
    // 選んでいる PC の詳しい様子（変わったときだけ書き換える）
    std::wstring detail = L"一覧の PC を選ぶと、そろっていない項目と健康状態がここに出ます。";
    for (const auto& r : list) {
        if (r.name != selName) continue;
        detail = r.name + L"（" + r.ip + L"・まなびと講習準備 v" + r.version + L"・最後の報告 " + r.at + L"）\r\n";
        if (r.ok + r.ng + r.na == 0) detail += L"まだ点検していません。［いますぐ点検してもらう］を押すと、点検してから知らせます。\r\n";
        for (const auto& it : r.items) {
            std::wstring mark = it.state == L"OK" ? L"［そろっている］" : it.state == L"NG" ? L"［そろっていない］" : it.state == L"ERR" ? L"［エラー］" : L"［参考］";
            detail += mark + L" " + it.name + L"：" + it.current + L"\r\n";
        }
    }
    HWND ed = GetDlgItem(hwnd, IDS_PCDETAIL);
    std::wstring curText(static_cast<size_t>(GetWindowTextLengthW(ed)) + 1, L'\0');
    GetWindowTextW(ed, &curText[0], static_cast<int>(curText.size()));
    curText.resize(wcslen(curText.c_str()));
    if (detail != curText) SetWindowTextW(ed, detail.c_str());
    // 集計の文：変わったときだけ、その行だけを描き直す
    const std::wstring summary = PcsSummaryText();
    if (summary != g_pcsSummaryShown) {
        g_pcsSummaryShown = summary;
        RECT rc;
        GetClientRect(hwnd, &rc);
        RECT line{0, g_sl.contentTop, rc.right, g_sl.contentTop + MulDiv(34, Dpi(hwnd), 96)};
        InvalidateRect(hwnd, &line, FALSE);
    }
    g_refreshingPcs = false;
}

void RefreshPrnStatus(HWND hwnd) {
    HWND lv = GetDlgItem(hwnd, IDS_PRN_STATUS);
    std::vector<std::pair<std::wstring, RepItem>> rows;
    {
        Lock l(&g_cs);
        for (const auto& kv : g_reports) {
            RepItem item{L"printers", L"NA", L"", L"報告なし"};
            for (const auto& it : kv.second.items)
                if (it.id == L"printers") item = it;
            rows.push_back({kv.first, item});
        }
    }
    EnsureRowCount(lv, static_cast<int>(rows.size()));
    for (size_t i = 0; i < rows.size(); ++i) {
        const int row = static_cast<int>(i);
        std::wstring st = rows[i].second.state == L"OK" ? L"そろっている" : rows[i].second.state == L"NG" ? L"そろっていない" : L"―";
        SetCellText(lv, row, 0, rows[i].first);
        SetCellText(lv, row, 1, st);
        SetCellText(lv, row, 2, rows[i].second.current);
    }
}

void ShowServerTab(HWND hwnd, int t) {
    g_stab = t;
    const int ids0[] = {IDS_PCS, IDS_PCDETAIL, IDS_INSPECT};
    const int ids1[] = {IDS_SOURCES, IDS_REMOVE, IDS_CLEAR, IDS_PUSH, IDS_CLIENTS, IDS_OFFICE, IDS_ADOBE};
    const int ids2[] = {IDS_PRINTERS, IDS_PRN_DEFAULT, IDS_PRN_SHARE, IDS_PRN_RELOAD, IDS_PRN_STATUS,
                        IDS_MAN_NAME, IDS_MAN_HOST, IDS_MAN_PORT, IDS_MAN_PATH, IDS_MAN_TEST, IDS_MAN_ADD, IDS_MAN_REMOVE};
    for (int id : ids0) ShowWindow(GetDlgItem(hwnd, id), t == 0 ? SW_SHOW : SW_HIDE);
    for (int id : ids1) ShowWindow(GetDlgItem(hwnd, id), t == 1 ? SW_SHOW : SW_HIDE);
    for (int id : ids2) ShowWindow(GetDlgItem(hwnd, id), t == 2 ? SW_SHOW : SW_HIDE);
    for (int i = 0; i < 3; ++i) InvalidateRect(GetDlgItem(hwnd, IDS_TAB0 + i), nullptr, FALSE);
    if (t == 0) {
        g_pcsSummaryShown.clear();
        RefreshPcs(hwnd, true);
    }
    if (t == 2 && g_prnRows.empty() && !g_prnLoading) StartPrinterList(hwnd);
    if (t == 2) RefreshPrnStatus(hwnd);
    InvalidateRect(hwnd, nullptr, TRUE);
}

void DrawServerTab(const DRAWITEMSTRUCT* d) {
    const int t = static_cast<int>(d->CtlID) - IDS_TAB0;
    const bool sel = (t == g_stab);
    RECT r = d->rcItem;
    HBRUSH paper = CreateSolidBrush(RGB(0xF7, 0xF3, 0xEA));
    FillRect(d->hDC, &r, paper);
    DeleteObject(paper);
    if (sel) {
        RECT bar{r.left, r.bottom - 3, r.right, r.bottom};
        HBRUSH ab = CreateSolidBrush(RGB(0x2D, 0x5B, 0x8A));
        FillRect(d->hDC, &bar, ab);
        DeleteObject(ab);
    }
    SetBkMode(d->hDC, TRANSPARENT);
    SetTextColor(d->hDC, sel ? RGB(0x2D, 0x5B, 0x8A) : RGB(0x6B, 0x64, 0x5C));
    HGDIOBJ old = SelectObject(d->hDC, sel ? g_sbold : g_sfont);
    DrawTextW(d->hDC, kTabNames[t], -1, &r, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    SelectObject(d->hDC, old);
    if ((d->itemState & ODS_FOCUS) && !(d->itemState & ODS_NOFOCUSRECT)) {
        RECT f = r;
        InflateRect(&f, -3, -3);
        DrawFocusRect(d->hDC, &f);
    }
}

// 教室の PC の一覧：「準備」の列を色分けする
LRESULT PcsCustomDraw(NMLVCUSTOMDRAW* cd) {
    switch (cd->nmcd.dwDrawStage) {
        case CDDS_PREPAINT:
            return CDRF_NOTIFYITEMDRAW;
        case CDDS_ITEMPREPAINT:
            return CDRF_NOTIFYSUBITEMDRAW;
        case CDDS_ITEMPREPAINT | CDDS_SUBITEM: {
            const int st = static_cast<int>(cd->nmcd.lItemlParam);
            if (cd->iSubItem == 1) {
                cd->clrText = st == 0 ? RGB(0x2E, 0x7D, 0x32) : st == 1 ? RGB(0xB3, 0x41, 0x2E) : RGB(0x6B, 0x64, 0x5C);
                SelectObject(cd->nmcd.hdc, g_sbold);
            } else {
                cd->clrText = st == 2 ? RGB(0x6B, 0x64, 0x5C) : RGB(0x33, 0x2E, 0x29);
                SelectObject(cd->nmcd.hdc, g_sfont);
            }
            return CDRF_NEWFONT;
        }
        default:
            return CDRF_DODEFAULT;
    }
}

void RefreshClients(HWND hwnd) {
    HWND lv = GetDlgItem(hwnd, IDS_CLIENTS);
    std::vector<ClientInfo> list;
    {
        Lock l(&g_cs);
        for (const auto& kv : g_clients) list.push_back(kv.second);
    }
    EnsureRowCount(lv, static_cast<int>(list.size()));
    for (size_t i = 0; i < list.size(); ++i) {
        const auto& c = list[i];
        std::wstring pct = c.total ? std::to_wstring(static_cast<int>(c.sent * 100 / c.total)) + L"%" : L"―";
        std::wstring amount = FormatBytes(c.sent) + L" / " + FormatBytes(c.total);
        const int row = static_cast<int>(i);
        SetCellText(lv, row, 0, c.name);
        SetCellText(lv, row, 1, c.state);
        SetCellText(lv, row, 2, amount);
        SetCellText(lv, row, 3, pct);
    }
}


// ---------------------------------------------------------------- 先生用：登録済みの PC に自動で配る（v0.18.0）
//   先生の PC：授業ファイルの欄のファイルについて SHA-256 を計算し、「一覧（m 行・f 行）」に署名して配る。
//     一覧：m|配る番号|個数|合計バイト  のあと  f|相対パス|大きさ|SHA-256（16 進）
//   受講生の PC：報告の返事（OK|…|配る番号）で気づき、PUSHMAN で一覧と署名、PUSH|番号|PC名 のあと PGET|i で中身を受け取る。
//   中身は合言葉なし（本物かは署名と SHA-256 で確かめる）。人に見られて困るファイルは、合言葉つきの配り方で。

bool g_pushPreparing = false;  // 画面のスレッドだけ
const UINT kPushReady = WM_APP + 62;
const size_t kPushMaxFiles = 5000;
const unsigned long long kPushMaxBytes = 20ULL << 30;  // 20 GB

// ファイルの SHA-256（読み込みながら計算）
bool FileSha256(const std::wstring& path, std::vector<BYTE>& out, unsigned long long expectSize) {
    HANDLE f = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
    if (f == INVALID_HANDLE_VALUE) return false;
    BCRYPT_ALG_HANDLE alg = nullptr;
    BCRYPT_HASH_HANDLE h = nullptr;
    bool ok = BCRYPT_SUCCESS(BCryptOpenAlgorithmProvider(&alg, BCRYPT_SHA256_ALGORITHM, nullptr, 0)) &&
              BCRYPT_SUCCESS(BCryptCreateHash(alg, &h, nullptr, 0, nullptr, 0, 0));
    std::vector<BYTE> buf(1 << 20);
    unsigned long long total = 0;
    while (ok) {
        DWORD got = 0;
        if (!ReadFile(f, buf.data(), static_cast<DWORD>(buf.size()), &got, nullptr)) { ok = false; break; }
        if (got == 0) break;
        total += got;
        ok = BCRYPT_SUCCESS(BCryptHashData(h, buf.data(), got, 0));
    }
    out.assign(32, 0);
    if (ok) ok = BCRYPT_SUCCESS(BCryptFinishHash(h, out.data(), 32, 0)) && total == expectSize;  // 計算の途中で変わっていないか
    if (h) BCryptDestroyHash(h);
    if (alg) BCryptCloseAlgorithmProvider(alg, 0);
    CloseHandle(f);
    return ok;
}

struct PushPrepJob {
    HWND hwnd;
    std::vector<Entry> entries;
    PushState* result = nullptr;
    std::wstring error;
};

DWORD WINAPI PushPrepThread(LPVOID param) {
    PushPrepJob* job = static_cast<PushPrepJob*>(param);
    PushState* ps = new PushState();
    ps->id = NowVersion();
    std::string lines;
    for (const auto& e : job->entries) {
        if (e.dir) continue;
        if (ps->files.size() >= kPushMaxFiles) { job->error = L"ファイルが多すぎます（" + std::to_wstring(kPushMaxFiles) + L" 個まで）。"; break; }
        std::vector<BYTE> hsh;
        if (!FileSha256(e.full, hsh, e.size)) { job->error = L"読めないファイル、または途中で変わったファイルがあります：" + e.rel; break; }
        std::wstring rel = e.rel;
        for (auto& ch : rel)
            if (ch == L'|' || ch < 0x20) ch = L'_';
        lines += "f|" + ToUtf8(rel) + "|" + std::to_string(e.size) + "|" + Hex(hsh) + "\r\n";
        ps->files.push_back({e.full, e.rel, e.size});
        ps->bytes += e.size;
    }
    if (job->error.empty() && ps->bytes > kPushMaxBytes) job->error = L"合計が大きすぎます（20 GB まで）。";
    if (job->error.empty() && ps->files.empty()) job->error = L"配るファイルがありません。";
    if (job->error.empty()) {
        ps->manifest = "m|" + ps->id + "|" + std::to_string(ps->files.size()) + "|" + std::to_string(ps->bytes) + "\r\n" + lines;
        if (!SignWithTeacherKey(ps->manifest, ps->sig)) job->error = L"署名できませんでした。";
    }
    if (!job->error.empty()) {
        delete ps;
        ps = nullptr;
    }
    job->result = ps;
    if (!IsWindow(job->hwnd) || !PostMessageW(job->hwnd, kPushReady, 0, reinterpret_cast<LPARAM>(job))) {
        delete job->result;
        delete job;
    }
    return 0;
}

std::wstring PushStatusText() {
    std::string id;
    size_t count;
    unsigned long long bytes;
    int done = 0, total = 0;
    {
        Lock l(&g_cs);
        id = g_push.id;
        count = g_push.files.size();
        bytes = g_push.bytes;
        for (const auto& kv : g_reports) {
            ++total;
            for (const auto& it : kv.second.items)
                if (it.id == L"push" && it.state == L"OK" && it.current.rfind(FromUtf8(id), 0) == 0) ++done;
        }
    }
    if (g_pushPreparing) return L"自動で配る準備をしています（ファイルの SHA-256 を計算しています）…";
    if (id.empty()) return L"自動で配る：していません。授業ファイルを入れて［登録済みの PC に自動で配る］を押すと、登録済みの PC が 30 秒以内に受け取ります。";
    return L"自動で配っています：ファイル " + std::to_wstring(count) + L" 個・" + FormatBytes(bytes) + L"　受け取り済み " + std::to_wstring(done) + L" 台（報告のある " +
           std::to_wstring(total) + L" 台のうち）";
}

void StartPush(HWND hwnd) {
    if (g_pushPreparing) return;
    bool stopped = false;
    {
        Lock l(&g_cs);  // この中では画面を書き換えない
        if (!g_push.id.empty()) {  // 配っている → やめる
            g_push = PushState();
            stopped = true;
        }
    }
    if (stopped) {
        SetDlgItemTextW(hwnd, IDS_PUSH, L"登録済みの PC に自動で配る");
        InvalidateRect(hwnd, nullptr, TRUE);
        return;
    }
    if (!g_tkey.key) {
        MessageBoxW(hwnd, L"先生の PC の鍵が無いため、自動で配れません。", kAppName, MB_OK | MB_ICONWARNING);
        return;
    }
    PushPrepJob* job = new PushPrepJob();
    job->hwnd = hwnd;
    int files = 0;
    unsigned long long bytes = 0;
    {
        Lock l(&g_cs);
        job->entries = g_entries;
        files = g_totalFiles;
        bytes = g_totalBytes;
    }
    if (files == 0) {
        delete job;
        MessageBoxW(hwnd, L"授業ファイルの欄に、配るファイルをドロップしてください。", kAppName, MB_OK | MB_ICONINFORMATION);
        return;
    }
    std::wstring ask = L"授業ファイル（" + std::to_wstring(files) + L" 個・" + FormatBytes(bytes) +
                       L"）を、先生の PC を登録した受講生の PC に自動で配ります。\n\n"
                       L"・受講生の操作は要りません（30 秒以内に受け取り、「先生からファイルが届きました」と出ます）。\n"
                       L"・合言葉なしで取りに行ける配り方です（中身は署名で確かめます）。人に見られて困るファイルは、合言葉つきの配り方で配ってください。\n\n配りますか？";
    if (MessageBoxW(hwnd, ask.c_str(), kAppName, MB_YESNO | MB_ICONQUESTION) != IDYES) {
        delete job;
        return;
    }
    HANDLE th = CreateThread(nullptr, 0, PushPrepThread, job, 0, nullptr);
    if (!th) {
        delete job;
        return;
    }
    CloseHandle(th);
    g_pushPreparing = true;
    EnableWindow(GetDlgItem(hwnd, IDS_PUSH), FALSE);
    InvalidateRect(hwnd, nullptr, TRUE);
}

void FinishPushPrep(HWND hwnd, PushPrepJob* job) {
    g_pushPreparing = false;
    EnableWindow(GetDlgItem(hwnd, IDS_PUSH), TRUE);
    if (job->result) {
        {
            Lock l(&g_cs);
            g_push = *job->result;
        }
        SetDlgItemTextW(hwnd, IDS_PUSH, L"自動で配るのをやめる");
    } else {
        MessageBoxW(hwnd, (L"自動で配れませんでした。\n\n" + job->error).c_str(), kAppName, MB_OK | MB_ICONWARNING);
    }
    delete job->result;
    delete job;
    InvalidateRect(hwnd, nullptr, TRUE);
}

// PUSHMAN  ← PSH|番号|長さ|署名  のあと一覧
void ServePushManifest(Conn& c) {
    std::string man, id;
    std::vector<BYTE> sig;
    {
        Lock l(&g_cs);
        man = g_push.manifest;
        id = g_push.id;
        sig = g_push.sig;
    }
    if (man.empty() || sig.size() != 64) { c.SendLine("ERR|自動で配っているファイルはありません。"); return; }
    if (c.SendLine("PSH|" + id + "|" + std::to_string(man.size()) + "|" + Hex(sig))) c.SendAll(man.data(), man.size());
}

// PUSH|番号|PC名  のあと  PGET|i（中身）を繰り返し、DONE|できた数|できなかった数
void ServePushFiles(Conn& c, const std::wstring& ip, const std::vector<std::string>& first) {
    if (first.size() < 3) return;
    std::vector<PushFile> files;
    std::string id;
    unsigned long long bytes = 0;
    unsigned long cid;
    {
        Lock l(&g_cs);
        files = g_push.files;
        id = g_push.id;
        bytes = g_push.bytes;
        cid = g_nextClient++;
        ClientInfo ci;
        ci.name = FromUtf8(first[2]).substr(0, 64) + L"（自動）";
        ci.ip = ip;
        ci.state = L"受け取り中";
        ci.total = bytes;
        ci.lastTick = GetTickCount();
        g_clients[cid] = ci;
    }
    if (id.empty() || first[1] != id) {  // 古い番号：配り直されたので、一覧から取り直してもらう
        c.SendLine("ERR|配るファイルが変わりました。");
        Lock l(&g_cs);
        g_clients.erase(cid);
        return;
    }
    if (!c.SendLine("OK|" + id)) return;
    std::string line;
    bool done = false;
    while (!g_serverStop && c.ReadLine(line)) {
        auto f = SplitBar(line);
        if (f[0] == "PGET" && f.size() >= 2) {
            size_t i = static_cast<size_t>(strtoul(f[1].c_str(), nullptr, 10));
            if (i >= files.size()) { c.SendLine("ERR|番号が違います。"); continue; }
            if (!SendFileTo(c, files[i].full, cid)) break;
        } else if (f[0] == "DONE") {
            done = true;
            Lock l(&g_cs);
            int failed = f.size() >= 3 ? atoi(f[2].c_str()) : 0;
            g_clients[cid].state = failed ? L"完了（" + std::to_wstring(failed) + L" 個できず）" : L"完了";
            g_clients[cid].sent = g_clients[cid].total;
            break;
        }
    }
    if (!done) {
        Lock l(&g_cs);
        g_clients[cid].state = L"途中で切れました";
    }
}

// 先生用の窓の部品（タブ・教室の PC・プリンター）を作る。「配る」の部品は ServerProc の WM_CREATE が作る
void CreateServerTabControls(HWND hwnd) {
    for (int i = 0; i < 3; ++i)
        CreateWindowExW(0, WC_BUTTONW, kTabNames[i], WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW, 0, 0, 10, 10, hwnd,
                        reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDS_TAB0 + i)), g_inst, nullptr);
    HWND pcs = CreateWindowExW(0, WC_LISTVIEWW, L"", WS_CHILD | WS_BORDER | WS_TABSTOP | LVS_REPORT | LVS_SINGLESEL | LVS_SHOWSELALWAYS | LVS_NOSORTHEADER,
                               0, 0, 10, 10, hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDS_PCS)), g_inst, nullptr);
    ListView_SetExtendedListViewStyle(pcs, LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER);
    const wchar_t* heads[] = {L"PC 名", L"準備", L"そろっていない項目", L"Office", L"空き容量", L"時刻のずれ", L"電池", L"最後の報告"};
    for (int i = 0; i < 8; ++i) {
        LVCOLUMNW col{};
        col.mask = LVCF_TEXT | LVCF_WIDTH;
        col.pszText = const_cast<LPWSTR>(heads[i]);
        col.cx = 100;
        ListView_InsertColumn(pcs, i, &col);
    }
    CreateWindowExW(WS_EX_CLIENTEDGE, WC_EDITW, L"", WS_CHILD | WS_VSCROLL | ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL, 0, 0, 10, 10, hwnd,
                    reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDS_PCDETAIL)), g_inst, nullptr);
    CreateWindowExW(0, WC_BUTTONW, L"登録済みの PC に自動で配る", WS_CHILD | WS_TABSTOP, 0, 0, 10, 10, hwnd,
                    reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDS_PUSH)), g_inst, nullptr);
    CreateWindowExW(0, WC_BUTTONW, L"いますぐ点検してもらう", WS_CHILD | WS_TABSTOP, 0, 0, 10, 10, hwnd,
                    reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDS_INSPECT)), g_inst, nullptr);
    HWND prn = CreateWindowExW(0, WC_LISTVIEWW, L"", WS_CHILD | WS_BORDER | WS_TABSTOP | LVS_REPORT | LVS_SINGLESEL | LVS_NOSORTHEADER, 0, 0, 10, 10,
                               hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDS_PRINTERS)), g_inst, nullptr);
    ListView_SetExtendedListViewStyle(prn, LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER | LVS_EX_CHECKBOXES);
    const wchar_t* pheads[] = {L"プリンター", L"つなぎ方", L"ドライバー", L"配れるか"};
    for (int i = 0; i < 4; ++i) {
        LVCOLUMNW col{};
        col.mask = LVCF_TEXT | LVCF_WIDTH;
        col.pszText = const_cast<LPWSTR>(pheads[i]);
        col.cx = 100;
        ListView_InsertColumn(prn, i, &col);
    }
    CreateWindowExW(0, WC_COMBOBOXW, L"", WS_CHILD | WS_TABSTOP | WS_VSCROLL | CBS_DROPDOWNLIST, 0, 0, 10, 300, hwnd,
                    reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDS_PRN_DEFAULT)), g_inst, nullptr);
    CreateWindowExW(0, WC_BUTTONW, L"チェックしたプリンターを配る", WS_CHILD | WS_TABSTOP, 0, 0, 10, 10, hwnd,
                    reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDS_PRN_SHARE)), g_inst, nullptr);
    CreateWindowExW(0, WC_BUTTONW, L"一覧を読み直す", WS_CHILD | WS_TABSTOP, 0, 0, 10, 10, hwnd,
                    reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDS_PRN_RELOAD)), g_inst, nullptr);
    {
        struct EditDef { int id; const wchar_t* cue; const wchar_t* init; };
        const EditDef eds[] = {{IDS_MAN_NAME, L"名前（例 教室-複合機）", L""}, {IDS_MAN_HOST, L"IP アドレス（例 192.168.1.50）", L""},
                               {IDS_MAN_PORT, L"ポート", L"631"}, {IDS_MAN_PATH, L"パス", L"/ipp/print"}};
        for (const auto& e : eds) {
            HWND ed = CreateWindowExW(WS_EX_CLIENTEDGE, WC_EDITW, e.init, WS_CHILD | WS_TABSTOP | ES_AUTOHSCROLL, 0, 0, 10, 10, hwnd,
                                      reinterpret_cast<HMENU>(static_cast<INT_PTR>(e.id)), g_inst, nullptr);
            SendMessageW(ed, EM_SETCUEBANNER, TRUE, reinterpret_cast<LPARAM>(e.cue));
        }
        CreateWindowExW(0, WC_BUTTONW, L"つながるか確かめる", WS_CHILD | WS_TABSTOP, 0, 0, 10, 10, hwnd,
                        reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDS_MAN_TEST)), g_inst, nullptr);
        CreateWindowExW(0, WC_BUTTONW, L"一覧に加える", WS_CHILD | WS_TABSTOP, 0, 0, 10, 10, hwnd,
                        reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDS_MAN_ADD)), g_inst, nullptr);
        CreateWindowExW(0, WC_BUTTONW, L"選んだものを外す", WS_CHILD | WS_TABSTOP, 0, 0, 10, 10, hwnd,
                        reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDS_MAN_REMOVE)), g_inst, nullptr);
    }
    HWND st = CreateWindowExW(0, WC_LISTVIEWW, L"", WS_CHILD | WS_BORDER | LVS_REPORT | LVS_NOSORTHEADER, 0, 0, 10, 10, hwnd,
                              reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDS_PRN_STATUS)), g_inst, nullptr);
    ListView_SetExtendedListViewStyle(st, LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER);
    const wchar_t* sheads[] = {L"PC 名", L"プリンター", L"この PC の様子"};
    for (int i = 0; i < 3; ++i) {
        LVCOLUMNW col{};
        col.mask = LVCF_TEXT | LVCF_WIDTH;
        col.pszText = const_cast<LPWSTR>(sheads[i]);
        col.cx = 100;
        ListView_InsertColumn(st, i, &col);
    }
}

void SetColumns(HWND lv, std::initializer_list<int> percents) {
    RECT r;
    GetClientRect(lv, &r);
    int w = r.right - GetSystemMetrics(SM_CXVSCROLL);
    int i = 0, used = 0;
    const int n = static_cast<int>(percents.size());
    for (int p : percents) {
        int cw = (i == n - 1) ? std::max(40, w - used) : w * p / 100;
        ListView_SetColumnWidth(lv, i, cw);
        used += cw;
        ++i;
    }
}

void LayoutServer(HWND hwnd) {
    RECT rc;
    GetClientRect(hwnd, &rc);
    int d = Dpi(hwnd);
    auto S = [d](int v) { return MulDiv(v, d, 96); };
    int m = S(16), W = rc.right, H = rc.bottom;
    // ヘッダー（題名・合言葉・この PC の印）の下にタブ
    g_sl.tabTop = S(110) + (g_serverWarn.empty() ? 0 : S(34));
    MoveWindow(GetDlgItem(hwnd, IDS_NETWORK), W - m - S(200), S(70), S(200), S(30), TRUE);
    ShowWindow(GetDlgItem(hwnd, IDS_NETWORK), g_serverWarn.empty() ? SW_HIDE : SW_SHOW);
    for (int i = 0; i < 3; ++i) MoveWindow(GetDlgItem(hwnd, IDS_TAB0 + i), m + i * S(144), g_sl.tabTop, S(140), S(38), TRUE);
    const int ct = g_sl.tabTop + S(48);
    g_sl.contentTop = ct;
    const int bottom = H - S(52);  // ［先生用をやめて閉じる］の上
    MoveWindow(GetDlgItem(hwnd, IDS_STOP), W - m - S(200), H - S(44), S(200), S(32), TRUE);

    // 教室の PC
    MoveWindow(GetDlgItem(hwnd, IDS_INSPECT), W - m - S(230), ct - S(2), S(230), S(32), TRUE);
    const int detailH = S(120);
    const int pcsTop = ct + S(40);
    const int pcsH = std::max(S(120), bottom - detailH - S(10) - pcsTop);
    MoveWindow(GetDlgItem(hwnd, IDS_PCS), m, pcsTop, W - 2 * m, pcsH, TRUE);
    MoveWindow(GetDlgItem(hwnd, IDS_PCDETAIL), m, pcsTop + pcsH + S(8), W - 2 * m, detailH, TRUE);
    SetColumns(GetDlgItem(hwnd, IDS_PCS), {11, 10, 27, 11, 13, 9, 10, 9});

    // 配る（前の配布サーバーと同じ並び。上をタブの下にそろえる）
    int y = ct;
    int listH = (H - y - S(380));
    if (listH < S(80)) listH = S(80);
    MoveWindow(GetDlgItem(hwnd, IDS_SOURCES), m, y + S(22), W - 2 * m, listH, TRUE);
    int by = y + S(22) + listH + S(6);
    MoveWindow(GetDlgItem(hwnd, IDS_REMOVE), m, by, S(160), S(30), TRUE);
    MoveWindow(GetDlgItem(hwnd, IDS_CLEAR), m + S(170), by, S(120), S(30), TRUE);
    MoveWindow(GetDlgItem(hwnd, IDS_PUSH), m + S(300), by, S(250), S(30), TRUE);
    MoveWindow(GetDlgItem(hwnd, IDS_OFFICE), m, by + S(70), S(250), S(30), TRUE);
    MoveWindow(GetDlgItem(hwnd, IDS_ADOBE), m, by + S(106), S(250), S(30), TRUE);
    int cy = by + S(172);
    MoveWindow(GetDlgItem(hwnd, IDS_CLIENTS), m, cy, W - 2 * m, std::max(S(60), bottom - cy), TRUE);
    SetColumns(GetDlgItem(hwnd, IDS_CLIENTS), {30, 20, 25, 25});

    // プリンター
    const int prnTop = ct + S(24);
    const int avail = bottom - prnTop;
    const int prnH = std::max(S(100), avail * 45 / 100);
    MoveWindow(GetDlgItem(hwnd, IDS_PRINTERS), m, prnTop, W - 2 * m, prnH, TRUE);
    SetColumns(GetDlgItem(hwnd, IDS_PRINTERS), {26, 28, 26, 20});
    g_sl.prnRowTop = prnTop + prnH + S(10);
    HWND cb = GetDlgItem(hwnd, IDS_PRN_DEFAULT);
    MoveWindow(cb, m + S(100), g_sl.prnRowTop, S(300), S(300), TRUE);
    RECT cr;
    GetWindowRect(cb, &cr);
    const int comboH = cr.bottom - cr.top;
    MoveWindow(GetDlgItem(hwnd, IDS_PRN_SHARE), m + S(412), g_sl.prnRowTop + (comboH - S(32)) / 2, S(250), S(32), TRUE);
    MoveWindow(GetDlgItem(hwnd, IDS_PRN_RELOAD), m + S(672), g_sl.prnRowTop + (comboH - S(32)) / 2, S(150), S(32), TRUE);
    // IP アドレスで加える（2 段）
    g_sl.manRowTop = g_sl.prnRowTop + std::max(comboH, S(32)) + S(12);
    {
        int x = m + S(150);
        const int my = g_sl.manRowTop;
        MoveWindow(GetDlgItem(hwnd, IDS_MAN_NAME), x, my, S(200), S(28), TRUE);
        x += S(208);
        MoveWindow(GetDlgItem(hwnd, IDS_MAN_HOST), x, my, S(220), S(28), TRUE);
        x += S(228);
        MoveWindow(GetDlgItem(hwnd, IDS_MAN_PORT), x, my, S(70), S(28), TRUE);
        x += S(78);
        MoveWindow(GetDlgItem(hwnd, IDS_MAN_PATH), x, my, S(140), S(28), TRUE);
        const int y2 = my + S(36);
        MoveWindow(GetDlgItem(hwnd, IDS_MAN_TEST), m + S(150), y2, S(180), S(30), TRUE);
        MoveWindow(GetDlgItem(hwnd, IDS_MAN_ADD), m + S(340), y2, S(140), S(30), TRUE);
        MoveWindow(GetDlgItem(hwnd, IDS_MAN_REMOVE), m + S(490), y2, S(170), S(30), TRUE);
    }
    g_sl.prnTextTop = g_sl.manRowTop + S(74);
    g_sl.prnStatusLabelTop = g_sl.prnTextTop + S(44);
    const int stTop = g_sl.prnStatusLabelTop + S(24);
    MoveWindow(GetDlgItem(hwnd, IDS_PRN_STATUS), m, stTop, W - 2 * m, std::max(S(60), bottom - stTop), TRUE);
    SetColumns(GetDlgItem(hwnd, IDS_PRN_STATUS), {20, 16, 64});
    InvalidateRect(hwnd, nullptr, TRUE);
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
    const int m = S(16);
    const COLORREF ink = RGB(0x33, 0x2E, 0x29), muted = RGB(0x6B, 0x64, 0x5C), blue = RGB(0x2D, 0x5B, 0x8A);
    std::wstring pass, summary;
    {
        Lock l(&g_cs);
        pass = g_passcode;
        summary = L"配るもの：ファイル " + std::to_wstring(g_totalFiles) + L" 個・" + FormatBytes(g_totalBytes);
    }
    // ヘッダー
    HGDIOBJ old = SelectObject(dc, g_sbold);
    SetTextColor(dc, ink);
    RECT r1{m, S(14), rc.right - m, S(40)};
    DrawTextW(dc, L"先生用　―　教室の PC をまとめて見る・配る", -1, &r1, DT_SINGLELINE | DT_LEFT);
    SelectObject(dc, g_sbig);
    SetTextColor(dc, blue);
    std::wstring pc = L"合言葉　" + pass;
    RECT r2{m, S(40), rc.right - m, S(100)};
    DrawTextW(dc, pc.c_str(), -1, &r2, DT_SINGLELINE | DT_LEFT | DT_VCENTER);
    SelectObject(dc, g_sfont);
    SetTextColor(dc, muted);
    std::wstring info = L"この PC：" + ComputerName() + L"　　この PC の印：" + ShortFingerprint(g_tkey.fp) +
                        L"\n授業ファイルの受け取りと、先生の PC の登録に、この合言葉を使います。登録のときは、受講生の画面の印と同じか確かめてもらいます。";
    RECT r3{m + S(260), S(42), rc.right - m - S(210), S(104)};
    DrawTextW(dc, info.c_str(), -1, &r3, DT_WORDBREAK | DT_LEFT);
    if (!g_serverWarn.empty()) {
        SetTextColor(dc, RGB(0xC4, 0x52, 0x1E));
        RECT rw{m, S(110), rc.right - m - S(210), S(140)};
        DrawTextW(dc, g_serverWarn.c_str(), -1, &rw, DT_WORDBREAK | DT_LEFT);
    }
    {
        RECT line{m, g_sl.tabTop + S(38), rc.right - m, g_sl.tabTop + S(39)};
        HBRUSH lb = CreateSolidBrush(RGB(0xDD, 0xD5, 0xC8));
        FillRect(dc, &line, lb);
        DeleteObject(lb);
    }
    const int ct = g_sl.contentTop;
    if (g_stab == 0) {
        int total = 0, ok = 0, warn = 0, off = 0, before = 0;
        {
            Lock l(&g_cs);
            const DWORD now = GetTickCount();
            for (const auto& kv : g_reports) {
                ++total;
                switch (PcStatus(kv.second, now)) {
                    case 0: ++ok; break;
                    case 1: ++warn; break;
                    case 2: ++off; break;
                    default: ++before; break;
                }
            }
        }
        SelectObject(dc, g_sbold);
        SetTextColor(dc, ink);
        std::wstring t = std::to_wstring(total) + L" 台　　準備できた " + std::to_wstring(ok) + L"　・　要対応 " + std::to_wstring(warn) + L"　・　応答なし " +
                         std::to_wstring(off) + (before ? L"　・　点検前 " + std::to_wstring(before) : L"");
        if (total == 0) t = L"まだ報告がありません。受講生の PC で、まなびと講習準備を開いてもらってください（開いていれば 1 分以内に知らせます）。";
        RECT rt{m, ct + S(4), rc.right - m - S(240), ct + S(30)};
        DrawTextW(dc, t.c_str(), -1, &rt, DT_SINGLELINE | DT_LEFT | DT_END_ELLIPSIS);
    } else if (g_stab == 1) {
        SetTextColor(dc, ink);
        SelectObject(dc, g_sfont);
        RECT r4{m, ct, rc.right - m, ct + S(20)};
        DrawTextW(dc, L"授業ファイル（ファイル・フォルダー・ドライブを、ここにドロップしてください。受講生は合言葉を入れて受け取ります）", -1, &r4,
                  DT_SINGLELINE | DT_LEFT | DT_END_ELLIPSIS);
        HWND lb = GetDlgItem(hwnd, IDS_SOURCES);
        RECT lr;
        GetWindowRect(lb, &lr);
        MapWindowPoints(nullptr, hwnd, reinterpret_cast<POINT*>(&lr), 2);
        RECT r5{m + S(562), lr.bottom + S(12), rc.right - m, lr.bottom + S(32)};
        DrawTextW(dc, summary.c_str(), -1, &r5, DT_SINGLELINE | DT_LEFT | DT_END_ELLIPSIS);
        SetTextColor(dc, (g_pushPreparing || !g_push.id.empty()) ? blue : muted);
        RECT rp{m, lr.bottom + S(44), rc.right - m, lr.bottom + S(66)};
        std::wstring pushText = PushStatusText();
        DrawTextW(dc, pushText.c_str(), -1, &rp, DT_SINGLELINE | DT_LEFT | DT_END_ELLIPSIS);
        for (int k = 0; k < kPkgCount; ++k) {
            SetTextColor(dc, g_pkgs[k].prep ? blue : muted);
            RECT ro{m + S(262), lr.bottom + S(76 + 36 * k), rc.right - m, lr.bottom + S(106 + 36 * k)};
            DrawTextW(dc, g_pkgs[k].text.c_str(), -1, &ro, DT_WORDBREAK | DT_LEFT | DT_END_ELLIPSIS);
        }
        SetTextColor(dc, ink);
        RECT r6{m, lr.bottom + S(152), rc.right - m, lr.bottom + S(172)};
        DrawTextW(dc, L"受け取った PC", -1, &r6, DT_SINGLELINE | DT_LEFT);
    } else {
        SelectObject(dc, g_sfont);
        SetTextColor(dc, ink);
        RECT r4{m, ct, rc.right - m, ct + S(20)};
        DrawTextW(dc, L"この PC のプリンター　―　チェックしたものの設定を、教室の PC に配ります（IP アドレスでつないだネットワークのプリンターだけ）", -1, &r4,
                  DT_SINGLELINE | DT_LEFT | DT_END_ELLIPSIS);
        RECT rd{m, g_sl.prnRowTop + S(4), m + S(96), g_sl.prnRowTop + S(30)};
        DrawTextW(dc, L"既定にする：", -1, &rd, DT_SINGLELINE | DT_LEFT);
        RECT rm{m, g_sl.manRowTop + S(4), m + S(148), g_sl.manRowTop + S(30)};
        DrawTextW(dc, L"IP アドレスで加える：", -1, &rm, DT_SINGLELINE | DT_LEFT);
        SetTextColor(dc, muted);
        RECT rs{m, g_sl.prnTextTop, rc.right - m, g_sl.prnStatusLabelTop - S(2)};
        std::wstring pt = g_prnText +
                          L"\nIP アドレスで加えたものは、Windows に入っている IPP のドライバーで入ります（メーカーのドライバーは要りません）。この PC のプリンターは、受講生の PC にも同じドライバーが要ります。";
        DrawTextW(dc, pt.c_str(), -1, &rs, DT_WORDBREAK | DT_LEFT | DT_END_ELLIPSIS);
        SetTextColor(dc, ink);
        RECT r7{m, g_sl.prnStatusLabelTop, rc.right - m, g_sl.prnStatusLabelTop + S(20)};
        DrawTextW(dc, L"教室の PC の様子（受講生の PC の点検の報告から）", -1, &r7, DT_SINGLELINE | DT_LEFT);
    }
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
            CreateServerTabControls(hwnd);
            CreateWindowExW(0, WC_BUTTONW, L"先生用をやめて閉じる", WS_CHILD | WS_VISIBLE | WS_TABSTOP, 0, 0, 10, 10, hwnd,
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
            g_prnText = DescribeSharedPrinters();
            ShowServerTab(hwnd, 0);
            return 0;
        }
        case WM_SIZE:
            LayoutServer(hwnd);
            return 0;
        case WM_ERASEBKGND:
            return 1;
        case WM_PAINT: {
            // 裏の画用紙に描いてから一度に写す（描きかけの状態が見えて、ちらつかないように）
            PAINTSTRUCT ps;
            HDC dc = BeginPaint(hwnd, &ps);
            RECT rc;
            GetClientRect(hwnd, &rc);
            HDC mem = CreateCompatibleDC(dc);
            HBITMAP bmp = CreateCompatibleBitmap(dc, std::max<LONG>(1, rc.right), std::max<LONG>(1, rc.bottom));
            if (mem && bmp) {
                HGDIOBJ oldBmp = SelectObject(mem, bmp);
                PaintServer(hwnd, mem);
                BitBlt(dc, ps.rcPaint.left, ps.rcPaint.top, ps.rcPaint.right - ps.rcPaint.left, ps.rcPaint.bottom - ps.rcPaint.top, mem, ps.rcPaint.left,
                       ps.rcPaint.top, SRCCOPY);
                SelectObject(mem, oldBmp);
            } else {
                PaintServer(hwnd, dc);  // 画用紙を作れないとき（ふつうは無い）は、そのまま描く
            }
            if (bmp) DeleteObject(bmp);
            if (mem) DeleteDC(mem);
            EndPaint(hwnd, &ps);
            return 0;
        }
        case WM_DROPFILES:
            AddDrops(hwnd, reinterpret_cast<HDROP>(wp));
            return 0;
        case WM_TIMER:
            if (wp == kServerTimer) {
                static unsigned tickCount = 0;
                ++tickCount;
                RefreshClients(hwnd);
                for (int k = 0; k < kPkgCount; ++k) PollPrep(hwnd, k);
                if (g_stab == 0) RefreshPcs(hwnd);
                if (g_stab == 2 && tickCount % 6 == 0) RefreshPrnStatus(hwnd);  // 3 秒ごと
                if (g_stab == 1 && tickCount % 6 == 0) {  // 受け取り済みの台数：変わったときだけ、その行だけを描き直す
                    static std::wstring shownPush;
                    std::wstring t = PushStatusText();
                    if (t != shownPush) {
                        shownPush = t;
                        HWND src = GetDlgItem(hwnd, IDS_SOURCES);
                        RECT lr, rc;
                        GetWindowRect(src, &lr);
                        MapWindowPoints(nullptr, hwnd, reinterpret_cast<POINT*>(&lr), 2);
                        GetClientRect(hwnd, &rc);
                        RECT line{0, lr.bottom, rc.right, lr.bottom + MulDiv(70, Dpi(hwnd), 96)};
                        InvalidateRect(hwnd, &line, FALSE);
                    }
                }
            }
            return 0;
        case WM_DRAWITEM: {
            const DRAWITEMSTRUCT* d = reinterpret_cast<const DRAWITEMSTRUCT*>(lp);
            if (d->CtlType == ODT_BUTTON && d->CtlID >= IDS_TAB0 && d->CtlID <= IDS_TAB2) {
                DrawServerTab(d);
                return TRUE;
            }
            break;
        }
        case WM_NOTIFY: {
            NMHDR* nh = reinterpret_cast<NMHDR*>(lp);
            if (nh->idFrom == IDS_PCS && nh->code == NM_CUSTOMDRAW) return PcsCustomDraw(reinterpret_cast<NMLVCUSTOMDRAW*>(lp));
            if (nh->idFrom == IDS_PCS && nh->code == LVN_ITEMCHANGED) {
                const NMLISTVIEW* nv = reinterpret_cast<const NMLISTVIEW*>(lp);
                if ((nv->uChanged & LVIF_STATE) && ((nv->uNewState ^ nv->uOldState) & LVIS_SELECTED)) RefreshPcs(hwnd, true);
            }
            break;
        }
        case kPrnListDone:
            FinishPrinterList(hwnd, reinterpret_cast<PrnListJob*>(lp));
            return 0;
        case kPushReady:
            FinishPushPrep(hwnd, reinterpret_cast<PushPrepJob*>(lp));
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
                case IDS_TAB0:
                case IDS_TAB1:
                case IDS_TAB2:
                    ShowServerTab(hwnd, LOWORD(wp) - IDS_TAB0);
                    LayoutServer(hwnd);
                    return 0;
                case IDS_INSPECT: {
                    int n = 0;
                    {
                        Lock l(&g_cs);
                        for (auto& kv : g_reports) {
                            kv.second.inspectReq = true;
                            ++n;
                        }
                    }
                    MessageBoxW(hwnd, n ? (std::to_wstring(n) + L" 台の PC に、次の報告（1 分以内）のときに点検してもらいます。").c_str()
                                        : L"まだ報告してきた PC がありません。受講生の PC でまなびと講習準備を開いてもらってください。",
                                kAppName, MB_OK | MB_ICONINFORMATION);
                    return 0;
                }
                case IDS_PRN_SHARE:
                    SharePrinters(hwnd);
                    return 0;
                case IDS_PRN_RELOAD:
                    StartPrinterList(hwnd);
                    return 0;
                case IDS_PUSH:
                    StartPush(hwnd);
                    return 0;
                case IDS_MAN_TEST:
                    ManualTest(hwnd);
                    return 0;
                case IDS_MAN_ADD:
                    ManualAdd(hwnd);
                    return 0;
                case IDS_MAN_REMOVE:
                    ManualRemove(hwnd);
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
                g_reports.clear();
                g_push = PushState();  // 窓を閉じたら、自動で配るのもやめる
                g_totalBytes = 0;
                g_totalFiles = 0;
            }
            g_prnRows.clear();
            g_prnLoading = false;
            g_pushPreparing = false;
            // 先生の鍵（g_tkey）は閉じない：閉じる直前まで動いていた受け渡しが読むことがあるため、アプリを終えるまで持つ
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
        wc.hIcon = LoadIconW(g_inst, MAKEINTRESOURCEW(101));
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
        wc.hIcon = LoadIconW(g_inst, MAKEINTRESOURCEW(101));
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


// ---------------------------------------------------------------- 受講生の PC：先生の PC への報告・登録・プリンターの設定

TeacherInfo GetTeacher() {
    TeacherInfo t;
    Pin pin;
    const bool hasPin = LoadPin(pin);
    Lock l(&g_cs);
    if (!g_teacher.tick || GetTickCount() - g_teacher.tick > 30000) return t;  // 30 秒、答えが無ければ見えていない
    t.valid = true;
    t.name = g_teacher.name;
    t.ip = g_teacher.ip;
    t.port = g_teacher.port;
    t.printersVersion = g_teacher.printersVer;
    t.fingerprint = g_teacher.fp;
    t.paired = hasPin && pin.fp == g_teacher.fp;
    return t;
}

bool SendReport(const TeacherInfo& t, const std::string& payload, ReportReply& reply) {
    reply = ReportReply();
    if (!t.valid) return false;
    Conn c;
    if (!ConnectTo(c, t.ip, t.port)) return false;
    DWORD tmo = 15000;
    setsockopt(c.s, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&tmo), sizeof(tmo));
    std::string line;
    bool ok = c.SendAll(payload.data(), payload.size()) && c.ReadLine(line, 1024);
    c.Close();
    if (!ok) return false;
    auto f = SplitBar(line);
    if (f.size() < 4 || f[0] != "OK") return false;
    reply.ok = true;
    reply.inspect = (f[1] == "1");
    reply.printersVersion = f[2];
    reply.fingerprint = f[3];
    if (f.size() >= 5) reply.pushId = f[4];
    return true;
}

// 先生の PC を登録する：合言葉で確かめ、公開の鍵を覚える。メッセージは画面に出す文
bool PairWithTeacher(const TeacherInfo& t, const std::wstring& pass, std::wstring& message) {
    if (!t.valid) {
        message = L"先生の PC が見つかりません。";
        return false;
    }
    Conn c;
    if (!ConnectTo(c, t.ip, t.port)) {
        message = L"先生の PC（" + t.name + L"）につながりませんでした。";
        return false;
    }
    DWORD tmo = 10000;  // 画面のスレッドから呼ぶので、待つのは 10 秒まで
    setsockopt(c.s, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&tmo), sizeof(tmo));
    std::string line;
    bool got = c.SendLine("PAIR|" + ToUtf8(pass) + "|" + ToUtf8(ComputerName())) && c.ReadLine(line, 4096);
    c.Close();
    if (!got) {
        message = L"先生の PC から返事がありませんでした。";
        return false;
    }
    auto f = SplitBar(line);
    if (f[0] != "KEY" || f.size() < 3) {
        message = f.size() >= 2 ? FromUtf8(f[1]) : L"登録できませんでした。";
        return false;
    }
    std::vector<BYTE> pub;
    if (!Unhex(f[1], pub) || !ValidPublicBlob(pub)) {
        message = L"先生の PC の鍵の形が違います。登録しませんでした。";
        return false;
    }
    if (Fingerprint(pub) != t.fingerprint) {
        message = L"先生の PC の印が、見つけたときと違います。登録しませんでした。";
        return false;
    }
    if (!SavePin(FromUtf8(f[2]), pub)) {
        message = L"登録を保存できませんでした。";
        return false;
    }
    message = L"先生の PC（" + FromUtf8(f[2]) + L"）を登録しました。";
    return true;
}

std::wstring PrintersConfigPath() { return AppDataDir() + L"\\printers.txt"; }

std::string LocalPrintersVersion() {
    std::vector<BYTE> b;
    if (!ReadWholeFile(PrintersConfigPath(), b, 64 * 1024)) return std::string();
    std::string s(b.begin(), b.end());
    if (s.rfind("\xEF\xBB\xBF", 0) == 0) s.erase(0, 3);
    auto f = SplitBar(s.substr(0, s.find_first_of("\r\n")));
    return (f.size() >= 2 && f[0] == "v") ? f[1] : std::string();
}

// プリンターの設定を受け取る：登録した鍵で署名を確かめてから printers.txt に書く
bool FetchPrinters(const TeacherInfo& t, std::wstring& message) {
    Pin pin;
    if (!t.valid || !LoadPin(pin) || pin.fp != t.fingerprint) {
        message = L"先生の PC が登録されていません。";
        return false;
    }
    Conn c;
    if (!ConnectTo(c, t.ip, t.port)) {
        message = L"先生の PC につながりませんでした。";
        return false;
    }
    DWORD tmo = 15000;
    setsockopt(c.s, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&tmo), sizeof(tmo));
    std::string line;
    if (!c.SendLine("PRINTERS") || !c.ReadLine(line, 4096)) {
        c.Close();
        message = L"先生の PC から返事がありませんでした。";
        return false;
    }
    auto f = SplitBar(line);
    if (f[0] != "PRN" || f.size() < 4) {
        c.Close();
        message = L"先生の PC は、プリンターの設定を配っていません。";
        return false;
    }
    const std::string ver = f[1];
    const unsigned long long len = _strtoui64(f[2].c_str(), nullptr, 10);
    std::vector<BYTE> sig;
    bool digits = !ver.empty() && ver.size() <= 20 && ver.find_first_not_of("0123456789") == std::string::npos;
    if (!digits || len == 0 || len > 64 * 1024 || !Unhex(f[3], sig) || sig.size() != 64) {
        c.Close();
        message = L"プリンターの設定の形が違います。受け取りませんでした。";
        return false;
    }
    // 手元の版より古い設定は受け取らない（前に配られた本物の設定を、あとから流し直されても戻らないように）
    const std::string localVer = LocalPrintersVersion();
    if (!localVer.empty() && (ver.size() < localVer.size() || (ver.size() == localVer.size() && ver < localVer))) {
        c.Close();
        message = L"受け取った設定は、この PC にある設定より古いため受け取りませんでした。";
        return false;
    }
    // 本文を受け取る（読みすぎた分から先に使う）
    std::string body = c.buf.substr(0, static_cast<size_t>(std::min<unsigned long long>(len, c.buf.size())));
    c.buf.erase(0, body.size());
    char tmp[4096];
    while (body.size() < len) {
        int k = recv(c.s, tmp, static_cast<int>(std::min<unsigned long long>(sizeof(tmp), len - body.size())), 0);
        if (k <= 0) break;
        body.append(tmp, static_cast<size_t>(k));
    }
    c.Close();
    if (body.size() != len) {
        message = L"プリンターの設定を最後まで受け取れませんでした。";
        return false;
    }
    if (!VerifySignature(pin.pub, body, sig)) {
        message = L"プリンターの設定の署名が、登録した先生の PC のものと合いません。受け取りませんでした。";
        return false;
    }
    // 本文の 1 行目の版が、知らされた版と同じか（古い設定を新しいものに見せかけていないか）
    auto first = SplitBar(body.substr(0, body.find_first_of("\r\n")));
    if (first.size() < 2 || first[0] != "v" || first[1] != ver) {
        message = L"プリンターの設定の版が合いません。受け取りませんでした。";
        return false;
    }
    MakeDirs(AppDataDir());
    std::string withBom = "\xEF\xBB\xBF" + body;
    if (!WriteFileAtomic(PrintersConfigPath(), withBom.data(), withBom.size())) {
        message = L"プリンターの設定を保存できませんでした。";
        return false;
    }
    message = L"先生用からプリンターの設定を受け取りました。［設定をそろえる］か［まとめて実行］で入ります。";
    return true;
}

// 合言葉を聞いて登録する（画面のスレッドから）。
// 偽の「先生用」に登録しないよう、先生の画面の「この PC の印」と同じかを目で確かめてもらう（4 桁の合言葉だけでは相手が本物か確かめきれないため）
bool PairInteractive(HWND owner, const TeacherInfo& t, std::wstring& message) {
    std::wstring confirm = L"先生の PC（" + t.name + L"）を登録します。\n\n先生の画面の「この PC の印」が\n\n　　" + ShortFingerprint(t.fingerprint) +
                           L"\n\nと同じことを確かめてください。違うときは「いいえ」を押し、先生に知らせてください（別の PC がなりすましている可能性があります）。\n\n同じですか？";
    if (MessageBoxW(owner, confirm.c_str(), kAppName, MB_YESNO | MB_ICONQUESTION | MB_DEFBUTTON2) != IDYES) {
        message.clear();
        return false;
    }
    std::wstring pass = AskPasscode(owner, t.name, L"先生の PC を登録します（プリンターの設定などを受け取るため。1 回だけです）。");
    if (pass.empty()) {
        message.clear();
        return false;
    }
    HCURSOR old = SetCursor(LoadCursorW(nullptr, IDC_WAIT));
    bool ok = PairWithTeacher(t, pass, message);
    SetCursor(old);
    return ok;
}


// ---------------------------------------------------------------- 受講生の PC：自動で配られたファイルを受け取る（v0.18.0）

std::string LastPushId(const std::string& fp) {
    std::vector<BYTE> b;
    if (!ReadWholeFile(AppDataDir() + L"\\push_last.txt", b, 4096)) return std::string();
    std::string s(b.begin(), b.end());
    while (!s.empty() && (s.back() == '\r' || s.back() == '\n')) s.pop_back();
    auto f = SplitBar(s);
    return (f.size() >= 2 && f[0] == fp) ? f[1] : std::string();
}

static bool SaveLastPushId(const std::string& fp, const std::string& id) {
    std::string s = fp + "|" + id + "\r\n";
    MakeDirs(AppDataDir());
    return WriteFileAtomic(AppDataDir() + L"\\push_last.txt", s.data(), s.size());
}

static bool DigitsOnly(const std::string& s, size_t maxLen) {
    return !s.empty() && s.size() <= maxLen && s.find_first_not_of("0123456789") == std::string::npos;
}

// 本文（長さ len）を受け取る。読みすぎた分（c.buf）から先に使う
static bool ReadExact(Conn& c, size_t len, std::string& out) {
    out = c.buf.substr(0, std::min(len, c.buf.size()));
    c.buf.erase(0, out.size());
    char tmp[8192];
    while (out.size() < len) {
        int k = recv(c.s, tmp, static_cast<int>(std::min(sizeof(tmp), len - out.size())), 0);
        if (k <= 0) return false;
        out.append(tmp, static_cast<size_t>(k));
    }
    return true;
}

bool FetchPush(const TeacherInfo& t, const std::string& pushId, PushResult& r, std::wstring& message) {
    r = PushResult();
    Pin pin;
    if (!t.valid || !LoadPin(pin) || pin.fp != t.fingerprint) {
        message = L"先生の PC が登録されていません。";
        return false;
    }
    if (!DigitsOnly(pushId, 20)) {
        message = L"配る番号の形が違います。";
        return false;
    }
    // 1. 一覧と署名
    std::string man;
    std::vector<BYTE> sig;
    {
        Conn c;
        if (!ConnectTo(c, t.ip, t.port)) {
            message = L"先生の PC につながりませんでした。";
            return false;
        }
        DWORD tmo = 15000;
        setsockopt(c.s, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&tmo), sizeof(tmo));
        std::string line;
        if (!c.SendLine("PUSHMAN") || !c.ReadLine(line, 4096)) {
            c.Close();
            message = L"先生の PC から返事がありませんでした。";
            return false;
        }
        auto f = SplitBar(line);
        const unsigned long long len = f.size() >= 4 ? _strtoui64(f[2].c_str(), nullptr, 10) : 0;
        if (f[0] != "PSH" || f.size() < 4 || f[1] != pushId || len == 0 || len > (2u << 20) || !Unhex(f[3], sig) || sig.size() != 64 ||
            !ReadExact(c, static_cast<size_t>(len), man)) {
            c.Close();
            message = L"配るファイルの一覧を受け取れませんでした。";
            return false;
        }
        c.Close();
    }
    if (!VerifySignature(pin.pub, man, sig)) {
        message = L"配るファイルの一覧の署名が、登録した先生の PC のものと合いません。受け取りませんでした。";
        return false;
    }
    // 2. 一覧を読む（形・名前・大きさ・SHA-256 を確かめる）
    struct Item {
        std::wstring rel;
        unsigned long long size = 0;
        std::vector<BYTE> sha;
    };
    std::vector<Item> items;
    unsigned long long total = 0;
    size_t declared = 0;
    {
        size_t pos = 0;
        bool head = false;
        while (pos < man.size()) {
            size_t nl = man.find('\n', pos);
            std::string line = man.substr(pos, nl == std::string::npos ? std::string::npos : nl - pos);
            pos = nl == std::string::npos ? man.size() : nl + 1;
            while (!line.empty() && line.back() == '\r') line.pop_back();
            if (line.empty()) continue;
            auto f = SplitBar(line);
            if (!head) {
                if (f.size() < 4 || f[0] != "m" || f[1] != pushId || !DigitsOnly(f[2], 6) || !DigitsOnly(f[3], 15)) {
                    message = L"配るファイルの一覧の形が違います。受け取りませんでした。";
                    return false;
                }
                declared = static_cast<size_t>(strtoul(f[2].c_str(), nullptr, 10));
                head = true;
                continue;
            }
            Item it;
            if (f.size() < 4 || f[0] != "f" || !DigitsOnly(f[2], 15) || f[3].size() != 64 || !Unhex(f[3], it.sha)) {
                message = L"配るファイルの一覧の形が違います。受け取りませんでした。";
                return false;
            }
            it.rel = FromUtf8(f[1]);
            if (!SafeRelPath(it.rel)) {
                message = L"受け取るフォルダーの外を指す名前があったため、受け取りませんでした。";
                return false;
            }
            it.size = _strtoui64(f[2].c_str(), nullptr, 10);
            total += it.size;
            items.push_back(it);
        }
        if (!head || items.size() != declared || items.empty() || items.size() > 5000 || total > (20ULL << 30)) {
            message = L"配るファイルの一覧の形が違います。受け取りませんでした。";
            return false;
        }
    }
    // 3. 受け取る場所と空き容量
    const std::wstring dest = DestFolder();
    if (!MakeDirs(dest)) {
        message = L"受け取るフォルダーを作れませんでした：" + dest;
        return false;
    }
    r.folder = dest;
    ULARGE_INTEGER freeBytes{};
    if (GetDiskFreeSpaceExW(dest.c_str(), &freeBytes, nullptr, nullptr) && freeBytes.QuadPart < total + (512ULL << 20)) {
        message = L"空き容量が足りないため、受け取りませんでした（" + FormatBytes(total) + L" と、余裕 512 MB が要ります）。";
        return false;
    }
    // 4. 中身（同じものが手元にあれば受け取らない）
    Conn c;
    if (!ConnectTo(c, t.ip, t.port)) {
        message = L"先生の PC につながりませんでした。";
        return false;
    }
    DWORD tmo = 60000;
    setsockopt(c.s, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&tmo), sizeof(tmo));
    std::string line;
    if (!c.SendLine("PUSH|" + pushId + "|" + ToUtf8(ComputerName())) || !c.ReadLine(line, 4096) || line != "OK|" + pushId) {
        c.Close();
        message = L"先生の PC が、配るのをやめたか、配るファイルを変えました。";
        return false;
    }
    BCRYPT_ALG_HANDLE alg = nullptr;
    if (!BCRYPT_SUCCESS(BCryptOpenAlgorithmProvider(&alg, BCRYPT_SHA256_ALGORITHM, nullptr, 0))) {
        c.Close();
        message = L"確かめの仕組みを使えませんでした。";
        return false;
    }
    bool broken = false;
    std::vector<char> buf(256 * 1024);
    for (size_t i = 0; i < items.size() && !broken; ++i) {
        const Item& it = items[i];
        const std::wstring path = dest + L"\\" + it.rel;
        // 手元に同じ大きさのファイルがあれば、SHA-256 を比べて同じなら省く
        WIN32_FILE_ATTRIBUTE_DATA fa{};
        if (GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &fa) && !(fa.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) &&
            ((static_cast<unsigned long long>(fa.nFileSizeHigh) << 32) | fa.nFileSizeLow) == it.size) {
            std::vector<BYTE> have;
            if (FileSha256(path, have, it.size) && have == it.sha) {
                ++r.received;
                continue;
            }
        }
        size_t slash = path.find_last_of(L'\\');
        MakeDirs(path.substr(0, slash));
        if (!c.SendLine("PGET|" + std::to_string(i)) || !c.ReadLine(line, 4096)) {
            broken = true;
            break;
        }
        if (line.rfind("DATA|", 0) != 0 || _strtoui64(line.c_str() + 5, nullptr, 10) != it.size) {
            ++r.failed;
            if (line.rfind("DATA|", 0) == 0) {
                broken = true;  // 大きさが一覧と違う：本文が続くので、つながりを保てない
            }
            continue;
        }
        const std::wstring part = path + L".part";
        HANDLE h = CreateFileW(part.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        BCRYPT_HASH_HANDLE hh = nullptr;
        bool ok = (h != INVALID_HANDLE_VALUE) && BCRYPT_SUCCESS(BCryptCreateHash(alg, &hh, nullptr, 0, nullptr, 0, 0));
        unsigned long long left = it.size;
        // 読みすぎた分から
        while (left > 0 && !c.buf.empty()) {
            size_t take = static_cast<size_t>(std::min<unsigned long long>(left, c.buf.size()));
            DWORD w = 0;
            if (ok) ok = WriteFile(h, c.buf.data(), static_cast<DWORD>(take), &w, nullptr) && w == take &&
                         BCRYPT_SUCCESS(BCryptHashData(hh, reinterpret_cast<PUCHAR>(&c.buf[0]), static_cast<ULONG>(take), 0));
            c.buf.erase(0, take);
            left -= take;
        }
        while (left > 0) {
            int k = recv(c.s, buf.data(), static_cast<int>(std::min<unsigned long long>(left, buf.size())), 0);
            if (k <= 0) {
                broken = true;
                break;
            }
            DWORD w = 0;
            if (ok) ok = WriteFile(h, buf.data(), static_cast<DWORD>(k), &w, nullptr) && w == static_cast<DWORD>(k) &&
                         BCRYPT_SUCCESS(BCryptHashData(hh, reinterpret_cast<PUCHAR>(buf.data()), static_cast<ULONG>(k), 0));
            left -= static_cast<unsigned long long>(k);
        }
        std::vector<BYTE> got(32, 0);
        if (ok && !broken) ok = BCRYPT_SUCCESS(BCryptFinishHash(hh, got.data(), 32, 0)) && got == it.sha;
        if (hh) BCryptDestroyHash(hh);
        if (h != INVALID_HANDLE_VALUE) CloseHandle(h);
        if (ok && !broken && MoveFileExW(part.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING)) {
            ++r.received;
            r.bytes += it.size;
        } else {
            DeleteFileW(part.c_str());  // 壊れたもの・合わないものは残さない
            ++r.failed;
        }
    }
    BCryptCloseAlgorithmProvider(alg, 0);
    if (!broken) c.SendLine("DONE|" + std::to_string(r.received) + "|" + std::to_string(r.failed));
    c.Close();
    if (broken) r.failed += static_cast<int>(items.size()) - r.received - r.failed;
    if (r.failed == 0) {
        SaveLastPushId(t.fingerprint, pushId);
        message = L"先生からファイルが届きました（" + std::to_wstring(r.received) + L" 個）。";
        return true;
    }
    message = std::to_wstring(r.failed) + L" 個のファイルを受け取れませんでした（開いているファイルは置き換えられません）。";
    return false;
}

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
        wc.hIcon = LoadIconW(g_inst, MAKEINTRESOURCEW(101));
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
    // 先生の PC の鍵（プリンターの設定の署名と、受講生の PC の登録に使う）。読めなくても、ほかの働き（配る・教室の PC）は続ける
    {
        std::wstring keyErr;
        if (LoadOrCreateTeacherKey(keyErr)) {
            LoadSharedPrinters();
        } else {
            MessageBoxW(owner, (keyErr + L"\n\nプリンターの設定は配れません。ほかの働きは使えます。").c_str(), kAppName, MB_OK | MB_ICONWARNING);
        }
    }
    std::wstring err;
    if (!StartServerSockets(err)) {
        StopServerSockets();
        MessageBoxW(owner, (L"先生用になれませんでした。\n\n" + err).c_str(), kAppName, MB_OK | MB_ICONWARNING);
        return;
    }
    g_serverWarn = OnPublicNetwork()
                       ? L"この PC のネットワークが「パブリック」のままです。ほかの PC から見えないことがあります。右の［プライベートに切り替える］を押してください。"
                       : L"";
    int d = Dpi(owner);
    RECT orc;
    GetWindowRect(owner, &orc);
    int w = MulDiv(1100, d, 96), h = MulDiv(700, d, 96);  // 教室の PC の一覧（8 列）とプリンターの入力欄が収まる大きさ
    g_server = CreateWindowExW(WS_EX_ACCEPTFILES, L"ManabitoShareServer", L"まなびと講習準備 ― 先生用",
                               WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN, orc.left + MulDiv(40, d, 96), orc.top + MulDiv(40, d, 96), w, h, owner, nullptr, g_inst,
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
