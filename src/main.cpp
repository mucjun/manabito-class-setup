// まなびと講習準備
//
// 画面（Win32）は殻だけ。中身は PowerShell の部品が行う。
//   点検する          → scripts\inspect.ps1（見るだけ。この EXE と同じ権限で動く）
//   設定をそろえる    → scripts\apply.ps1（管理者として動く。元に戻せる設定だけを変える）
// どちらも結果を UTF-8 の TSV に書き、この EXE はそれを読んで表示する。

#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>
#include <commctrl.h>
#include <shellapi.h>
#include <shlobj.h>
#include <uxtheme.h>
#include <netlistmgr.h>
#include <winver.h>

#include "share.h"

#include <algorithm>
#include <string>
#include <utility>
#include <vector>

namespace Gdiplus {
using std::max;
using std::min;
}  // namespace Gdiplus
#include <gdiplus.h>

namespace {

const wchar_t kAppName[] = L"まなびと講習準備";
const wchar_t kVersion[] = L"0.13.2";
const wchar_t kClassName[] = L"ManabitoClassSetupWindow";
const DWORD kTimeoutMs = 180000;       // 点検が 3 分を超えたら打ち切る
const DWORD kApplyTimeoutMs = 300000;  // そろえるのが 5 分を超えたら待つのをやめる

// 色（ほんうつし・ほんよみ・PDF ツールと同じ紙色の地。強調色は藍）
const COLORREF kPaper = RGB(0xF7, 0xF3, 0xEA);
const COLORREF kInk = RGB(0x33, 0x2E, 0x29);
const COLORREF kMuted = RGB(0x7A, 0x72, 0x68);
const COLORREF kAccent = RGB(0x2D, 0x5B, 0x8A);
const COLORREF kAccentDark = RGB(0x22, 0x47, 0x6C);
const COLORREF kLine = RGB(0xC9, 0xC0, 0xB0);
const COLORREF kWhite = RGB(0xFF, 0xFF, 0xFF);
const COLORREF kDisabled = RGB(0xB8, 0xB2, 0xA8);

const COLORREF kOk = RGB(0x2E, 0x7D, 0x32);
const COLORREF kNg = RGB(0xC4, 0x52, 0x1E);
const COLORREF kErr = RGB(0x9A, 0x6B, 0x00);
const COLORREF kInfo = RGB(0x4F, 0x5B, 0x6A);
const COLORREF kNa = RGB(0x9A, 0x96, 0x90);
const COLORREF kNgBack = RGB(0xFC, 0xEC, 0xE3);
const COLORREF kErrBack = RGB(0xFB, 0xF3, 0xDC);
const COLORREF kPart = RGB(0x8A, 0x5A, 0x00);      // 一部異なる（文字）
const COLORREF kPartBack = RGB(0xFF, 0xEE, 0xA8);  // 一部異なる（黄色い帯）

enum : int {
    IDC_PROFILE = 101,
    IDC_RUN,
    IDC_ONECLICK,
    IDC_SHARE,
    IDC_APPLY,
    IDC_CLEAN,
    IDC_LOGS,
    IDC_TOG_OFFICE,
    IDC_TOG_ADOBE,
    IDC_TOG_APPS,
    IDC_RUN_NOW,
    IDC_RUN_AT,
    IDC_TIME,
    IDC_LIST,
    IDC_DETAIL,
};
const UINT WM_APP_DONE = WM_APP + 1;
const UINT WM_APP_APPLIED = WM_APP + 2;   // wParam: 0=そろえる 1=スマート アプリ コントロールをオフ
const UINT WM_APP_STARTUP = WM_APP + 3;
const UINT WM_APP_UPDCHECK = WM_APP + 4;  // 新しい版の確認が終わった
const UINT WM_APP_SHARE_FOUND = WM_APP + 5;  // LAN 内に配布サーバーを見つけた（lParam = share::Offer*）
const UINT_PTR kTimerUpdate = 2;

const int kJobApply = 0;
const int kJobSacOff = 1;
const int kJobClean = 2;
const int kJobDeploy = 3;
const int kJobOneClick = 4;
const UINT_PTR kTimerDeploy = 1;
const int kBtnSacOff = 1001;
const int kBtnSacKeep = 1002;

struct Row {
    std::wstring group, id, name, state, current, expected, detail;
};

// 時間のかかる準備の、作業ごとの状況（deploy.ps1 の状況ファイルから読む）
struct ProgressItem {
    std::wstring id, name, state, stepName, text;
    int step = 0, steps = 0, percent = -1;
};

struct ProfileEntry {
    std::wstring name, path;
};

struct Segment {
    std::wstring text;
    COLORREF color;
};

struct Job {
    std::wstring cmd, tsv, outLog;
    HWND hwnd = nullptr;
    UINT doneMsg = WM_APP_DONE;  // 終わったら送るメッセージ
};

struct ApplyJob {
    std::wstring exe, params;
    HWND hwnd = nullptr;
    int kind = kJobApply;
    DWORD timeoutMs = kApplyTimeoutMs;
};

struct JobResult {
    bool started = false;
    bool timedOut = false;
    DWORD exitCode = 0;
    DWORD error = 0;
    DWORD elapsedMs = 0;
};

struct App {
    HINSTANCE inst = nullptr;
    HWND hwnd = nullptr, combo = nullptr, run = nullptr, oneclick = nullptr, share = nullptr, apply = nullptr, clean = nullptr,
         logs = nullptr, list = nullptr, detail = nullptr;
    HFONT font = nullptr, fontBold = nullptr, fontTitle = nullptr, fontSmall = nullptr;
    HBRUSH whiteBrush = nullptr;
    UINT dpi = 96;
    std::wstring exeDir, logDir;
    std::vector<ProfileEntry> profiles;
    std::vector<Row> rows;
    std::vector<Segment> summary;
    bool running = false;
    bool applying = false;  // running のうち「そろえる」の最中
    std::wstring currentTsv, currentDetail, currentOut, lastDetailFile;
    std::wstring applyTsv, applyLog, sacTsv, sacLog, cleanTsv, cleanLog;
    std::vector<Segment> applyNote;  // 「そろえる」のあと、点検の結果の前に出す一言
    // 配置（描画で使う）
    int ctrlTop = 0, ctrlHeight = 0, summaryTop = 0, detailLabelTop = 0, row2Top = 0;
    // 時間のかかる準備（Office・Adobe・独自アプリ）
    HWND tog[3] = {nullptr, nullptr, nullptr};
    bool toggleOn[3] = {false, false, false};
    bool appsAvailable = false;  // プロファイルに独自アプリの目録があるか
    bool installed[2] = {false, false};  // この PC に Office・Adobe CC が入っているか（オフにすると削除の対象）
    DWORD lastElapsed = 0;               // 直近の点検にかかった時間（要約の表示用）
    bool lastFromSample = false;
    bool closeAfterClean = false;  // 閉じるときの［履歴を消す］の最中
    HWND runNow = nullptr, runAt = nullptr, timePick = nullptr;
    bool deploying = false, deployCancelRequested = false, deployScheduled = false;
    SYSTEMTIME deployStart{};
    std::wstring deployTsv, deployLog, deployStatus, deployCancel, deployShown;
    bool oneClick = false;  // 今の準備が［まとめて実行］か
    bool updating = false;  // 新しい版に入れ替える準備中
    std::wstring updCheckOut, updStatus, updVersion;
    DWORD updStartTick = 0;
    std::vector<ProgressItem> progress;  // 作業ごとの行（準備中と、終わった直後に表示）
    std::wstring progressMode;           // wait / run / done
    SYSTEMTIME progressStart{};          // 作業を始めた時刻（全体の経過時間に使う）
    int progressTop = 0, progressHeight = 0;
} g;

void RefreshInstalled();  // 後で定義（Office・Adobe CC が入っているかを調べ直す）
void ApplyToggleJudgement();  // 後で定義（Office・Adobe の判定をトグルに合わせる）
void Layout();  // 後で定義（部品の配置）

// ---------------------------------------------------------------- DPI

typedef UINT(WINAPI* PFN_GetDpiForWindow)(HWND);
typedef UINT(WINAPI* PFN_GetDpiForSystem)();
typedef BOOL(WINAPI* PFN_SystemParametersInfoForDpi)(UINT, UINT, PVOID, UINT, UINT);
PFN_GetDpiForWindow pGetDpiForWindow = nullptr;
PFN_GetDpiForSystem pGetDpiForSystem = nullptr;
PFN_SystemParametersInfoForDpi pSystemParametersInfoForDpi = nullptr;

void LoadDpiFunctions() {
    HMODULE u = GetModuleHandleW(L"user32.dll");
    if (!u) return;
    pGetDpiForWindow = reinterpret_cast<PFN_GetDpiForWindow>(reinterpret_cast<void*>(GetProcAddress(u, "GetDpiForWindow")));
    pGetDpiForSystem = reinterpret_cast<PFN_GetDpiForSystem>(reinterpret_cast<void*>(GetProcAddress(u, "GetDpiForSystem")));
    pSystemParametersInfoForDpi = reinterpret_cast<PFN_SystemParametersInfoForDpi>(
        reinterpret_cast<void*>(GetProcAddress(u, "SystemParametersInfoForDpi")));
}

UINT SystemDpi() {
    if (pGetDpiForSystem) return pGetDpiForSystem();
    HDC dc = GetDC(nullptr);
    UINT d = static_cast<UINT>(GetDeviceCaps(dc, LOGPIXELSX));
    ReleaseDC(nullptr, dc);
    return d ? d : 96;
}

UINT WindowDpi(HWND h) {
    if (pGetDpiForWindow && h) {
        UINT d = pGetDpiForWindow(h);
        if (d) return d;
    }
    return SystemDpi();
}

int S(int v) { return MulDiv(v, static_cast<int>(g.dpi), 96); }

// ---------------------------------------------------------------- 文字コード

std::wstring Utf8ToWide(const char* p, int n, bool strict = false) {
    if (n <= 0) return std::wstring();
    DWORD flags = strict ? MB_ERR_INVALID_CHARS : 0;
    int len = MultiByteToWideChar(CP_UTF8, flags, p, n, nullptr, 0);
    if (len <= 0) return std::wstring();
    std::wstring w(static_cast<size_t>(len), L'\0');
    MultiByteToWideChar(CP_UTF8, flags, p, n, &w[0], len);
    return w;
}

bool ReadAllBytes(const std::wstring& path, std::string& out) {
    HANDLE h = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING,
                           FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    LARGE_INTEGER size{};
    if (!GetFileSizeEx(h, &size) || size.QuadPart > 16 * 1024 * 1024) {
        CloseHandle(h);
        return false;
    }
    out.assign(static_cast<size_t>(size.QuadPart), '\0');
    DWORD read = 0;
    BOOL ok = out.empty() ? TRUE : ReadFile(h, &out[0], static_cast<DWORD>(out.size()), &read, nullptr);
    CloseHandle(h);
    if (!ok) return false;
    out.resize(read);
    return true;
}

// BOM 付き UTF-8 → UTF-8 として読めるか → だめならコンソールの文字コード（日本語版なら Shift_JIS）
std::wstring DecodeText(const std::string& b) {
    if (b.size() >= 3 && static_cast<unsigned char>(b[0]) == 0xEF && static_cast<unsigned char>(b[1]) == 0xBB &&
        static_cast<unsigned char>(b[2]) == 0xBF) {
        return Utf8ToWide(b.data() + 3, static_cast<int>(b.size() - 3));
    }
    std::wstring w = Utf8ToWide(b.data(), static_cast<int>(b.size()), true);
    if (!w.empty() || b.empty()) return w;
    int n = static_cast<int>(b.size());
    int len = MultiByteToWideChar(CP_OEMCP, 0, b.data(), n, nullptr, 0);
    if (len <= 0) return std::wstring();
    std::wstring o(static_cast<size_t>(len), L'\0');
    MultiByteToWideChar(CP_OEMCP, 0, b.data(), n, &o[0], len);
    return o;
}

bool FileExists(const std::wstring& p) {
    DWORD a = GetFileAttributesW(p.c_str());
    return a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY);
}

// ---------------------------------------------------------------- プロファイル

// JSON の "name" の値だけを取り出す（簡易。プロファイルの中身は PowerShell 側が読む）
std::wstring ReadProfileName(const std::wstring& path) {
    std::string bytes;
    if (!ReadAllBytes(path, bytes)) return std::wstring();
    std::wstring t = DecodeText(bytes);
    size_t k = t.find(L"\"name\"");
    if (k == std::wstring::npos) return std::wstring();
    size_t c = t.find(L':', k + 6);
    if (c == std::wstring::npos) return std::wstring();
    size_t q = t.find(L'"', c + 1);
    if (q == std::wstring::npos) return std::wstring();
    std::wstring v;
    for (size_t i = q + 1; i < t.size(); ++i) {
        wchar_t ch = t[i];
        if (ch == L'\\' && i + 1 < t.size()) {
            v.push_back(t[++i]);
            continue;
        }
        if (ch == L'"') return v;
        v.push_back(ch);
    }
    return std::wstring();
}

// JSON の "key": "値" の値を取り出す（簡易）
std::wstring ReadJsonString(const std::wstring& path, const wchar_t* key) {
    std::string bytes;
    if (path.empty() || !ReadAllBytes(path, bytes)) return std::wstring();
    std::wstring t = DecodeText(bytes);
    std::wstring k = std::wstring(L"\"") + key + L"\"";
    size_t at = t.find(k);
    if (at == std::wstring::npos) return std::wstring();
    size_t c = t.find(L':', at + k.size());
    if (c == std::wstring::npos) return std::wstring();
    size_t q = t.find_first_not_of(L" \t\r\n", c + 1);
    if (q == std::wstring::npos || t[q] != L'"') return std::wstring();
    std::wstring v;
    for (size_t i = q + 1; i < t.size(); ++i) {
        if (t[i] == L'\\' && i + 1 < t.size()) {
            v.push_back(t[++i]);
            continue;
        }
        if (t[i] == L'"') return v;
        v.push_back(t[i]);
    }
    return std::wstring();
}

void LoadProfiles() {
    g.profiles.clear();
    std::wstring dir = g.exeDir + L"\\profiles\\";
    WIN32_FIND_DATAW fd{};
    HANDLE f = FindFirstFileW((dir + L"*.json").c_str(), &fd);
    if (f != INVALID_HANDLE_VALUE) {
        do {
            if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
            ProfileEntry p;
            p.path = dir + fd.cFileName;
            p.name = ReadProfileName(p.path);
            if (p.name.empty()) p.name = fd.cFileName;
            g.profiles.push_back(p);
        } while (FindNextFileW(f, &fd));
        FindClose(f);
    }
    // standard.json を先頭に、あとはファイル名の順
    std::sort(g.profiles.begin(), g.profiles.end(), [](const ProfileEntry& a, const ProfileEntry& b) {
        bool sa = a.path.size() >= 13 && _wcsicmp(a.path.c_str() + a.path.size() - 13, L"standard.json") == 0;
        bool sb = b.path.size() >= 13 && _wcsicmp(b.path.c_str() + b.path.size() - 13, L"standard.json") == 0;
        if (sa != sb) return sa;
        return _wcsicmp(a.path.c_str(), b.path.c_str()) < 0;
    });
    SendMessageW(g.combo, CB_RESETCONTENT, 0, 0);
    for (const auto& p : g.profiles) SendMessageW(g.combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(p.name.c_str()));
    if (g.profiles.empty()) {
        SendMessageW(g.combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"（プロファイルなし）"));
    }
    SendMessageW(g.combo, CB_SETCURSEL, 0, 0);
}

// ---------------------------------------------------------------- 結果

std::wstring StateLabel(const std::wstring& s) {
    if (s == L"OK") return L"そろっている";
    if (s == L"NG") return L"違う";
    if (s == L"PART") return L"一部異なる";
    if (s == L"ERR") return L"確認できない";
    if (s == L"NA") return L"対象外";
    return L"情報";
}

COLORREF StateColor(const std::wstring& s) {
    if (s == L"OK") return kOk;
    if (s == L"NG") return kNg;
    if (s == L"PART") return kPart;
    if (s == L"ERR") return kErr;
    if (s == L"NA") return kNa;
    return kInfo;
}

bool LoadResults(const std::wstring& path, std::vector<Row>& out) {
    std::string bytes;
    if (!ReadAllBytes(path, bytes)) return false;
    std::wstring t = DecodeText(bytes);
    out.clear();
    size_t pos = 0;
    while (pos <= t.size()) {
        size_t nl = t.find(L'\n', pos);
        std::wstring line = t.substr(pos, nl == std::wstring::npos ? std::wstring::npos : nl - pos);
        pos = (nl == std::wstring::npos) ? t.size() + 1 : nl + 1;
        if (!line.empty() && line.back() == L'\r') line.pop_back();
        if (line.empty() || line[0] == L'#') continue;
        std::vector<std::wstring> f;
        size_t s = 0;
        for (;;) {
            size_t tab = line.find(L'\t', s);
            f.push_back(line.substr(s, tab == std::wstring::npos ? std::wstring::npos : tab - s));
            if (tab == std::wstring::npos) break;
            s = tab + 1;
        }
        while (f.size() < 7) f.push_back(std::wstring());
        out.push_back(Row{f[0], f[1], f[2], f[3], f[4], f[5], f[6]});
    }
    return true;
}

void BuildSummary(DWORD elapsedMs, bool fromSample) {
    int ok = 0, ng = 0, err = 0, info = 0, na = 0, part = 0;
    for (const auto& r : g.rows) {
        if (r.state == L"OK") ++ok;
        else if (r.state == L"NG") ++ng;
        else if (r.state == L"PART") ++part;
        else if (r.state == L"ERR") ++err;
        else if (r.state == L"NA") ++na;
        else ++info;
    }
    g.summary.clear();
    for (const auto& seg : g.applyNote) g.summary.push_back(seg);
    g.summary.push_back({L"そろっている " + std::to_wstring(ok), kOk});
    g.summary.push_back({L"　／　", kMuted});
    g.summary.push_back({L"違う " + std::to_wstring(ng), ng ? kNg : kMuted});
    if (part) {
        g.summary.push_back({L"　／　", kMuted});
        g.summary.push_back({L"一部異なる " + std::to_wstring(part), kPart});
    }
    g.summary.push_back({L"　／　", kMuted});
    g.summary.push_back({L"確認できない " + std::to_wstring(err), err ? kErr : kMuted});
    g.summary.push_back({L"　／　", kMuted});
    g.summary.push_back({L"情報 " + std::to_wstring(info), kInfo});
    if (na) {
        g.summary.push_back({L"　／　", kMuted});
        g.summary.push_back({L"対象外 " + std::to_wstring(na), kNa});
    }
    SYSTEMTIME st;
    GetLocalTime(&st);
    wchar_t tail[96];
    if (fromSample) {
        wsprintfW(tail, L"　　（見本の結果を表示しています）");
    } else {
        wsprintfW(tail, L"　　（%02d:%02d 点検・%lu 秒）", st.wHour, st.wMinute,
                  static_cast<unsigned long>((elapsedMs + 500) / 1000));
    }
    g.summary.push_back({tail, kMuted});
}

void SetSummaryText(const std::wstring& text, COLORREF color) {
    g.summary.clear();
    g.summary.push_back({text, color});
    RECT r{0, g.summaryTop, 32767, g.summaryTop + S(28)};
    InvalidateRect(g.hwnd, &r, FALSE);
}

// 説明欄のスクロールバーは、文字が入りきらないときだけ出す
void UpdateDetailScroll() {
    if (!g.detail) return;
    RECT rc;
    GetClientRect(g.detail, &rc);
    HDC dc = GetDC(g.detail);
    HGDIOBJ old = SelectObject(dc, g.font);
    TEXTMETRICW tm{};
    GetTextMetricsW(dc, &tm);
    SelectObject(dc, old);
    ReleaseDC(g.detail, dc);
    int lineH = tm.tmHeight > 0 ? tm.tmHeight : 16;
    int visible = std::max(1, static_cast<int>(rc.bottom) / lineH);
    int lines = static_cast<int>(SendMessageW(g.detail, EM_GETLINECOUNT, 0, 0));
    ShowScrollBar(g.detail, SB_VERT, lines > visible);
}

void ShowDetailFor(int index) {
    if (index < 0 || index >= static_cast<int>(g.rows.size())) {
        SetWindowTextW(g.detail, L"一覧の項目を選ぶと、ここに説明が出ます。");
        UpdateDetailScroll();
        return;
    }
    const Row& r = g.rows[static_cast<size_t>(index)];
    std::wstring t = L"項目：" + r.name + L"（" + r.group + L"）\r\n";
    t += L"判定：" + StateLabel(r.state) + L"\r\n";
    t += L"今の状態：" + (r.current.empty() ? std::wstring(L"―") : r.current) + L"\r\n";
    t += L"講習用の状態：" + (r.expected.empty() ? std::wstring(L"―") : r.expected) + L"\r\n";
    if (!r.detail.empty()) t += L"説明：" + r.detail;
    SetWindowTextW(g.detail, t.c_str());
    UpdateDetailScroll();
}

void FillList() {
    SendMessageW(g.list, WM_SETREDRAW, FALSE, 0);
    ListView_DeleteAllItems(g.list);
    ListView_RemoveAllGroups(g.list);
    std::vector<std::wstring> groups;
    for (size_t i = 0; i < g.rows.size(); ++i) {
        const Row& r = g.rows[i];
        auto it = std::find(groups.begin(), groups.end(), r.group);
        int gid;
        if (it == groups.end()) {
            groups.push_back(r.group);
            gid = static_cast<int>(groups.size());
            LVGROUP lg{};
            lg.cbSize = sizeof(lg);
            lg.mask = LVGF_HEADER | LVGF_GROUPID | LVGF_STATE;
            lg.pszHeader = const_cast<LPWSTR>(groups.back().c_str());
            lg.iGroupId = gid;
            lg.stateMask = LVGS_NORMAL;
            lg.state = LVGS_NORMAL;
            ListView_InsertGroup(g.list, -1, &lg);
        } else {
            gid = static_cast<int>(it - groups.begin()) + 1;
        }
        LVITEMW item{};
        item.mask = LVIF_TEXT | LVIF_PARAM | LVIF_GROUPID;
        item.iItem = static_cast<int>(i);
        item.pszText = const_cast<LPWSTR>(r.name.c_str());
        item.lParam = static_cast<LPARAM>(i);
        item.iGroupId = gid;
        int at = ListView_InsertItem(g.list, &item);
        std::wstring label = StateLabel(r.state);
        ListView_SetItemText(g.list, at, 1, const_cast<LPWSTR>(r.current.c_str()));
        ListView_SetItemText(g.list, at, 2, const_cast<LPWSTR>(r.expected.c_str()));
        ListView_SetItemText(g.list, at, 3, const_cast<LPWSTR>(label.c_str()));
    }
    SendMessageW(g.list, WM_SETREDRAW, TRUE, 0);
    InvalidateRect(g.list, nullptr, TRUE);
    ShowDetailFor(-1);
}

// ---------------------------------------------------------------- 点検の実行

DWORD WINAPI JobThread(LPVOID param) {
    Job* job = static_cast<Job*>(param);
    JobResult* res = new JobResult();
    SECURITY_ATTRIBUTES sa{};
    sa.nLength = sizeof(sa);
    sa.bInheritHandle = TRUE;
    HANDLE out = CreateFileW(job->outLog.c_str(), GENERIC_WRITE, FILE_SHARE_READ, &sa, CREATE_ALWAYS,
                             FILE_ATTRIBUTE_NORMAL, nullptr);
    HANDLE in = CreateFileW(L"NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, &sa, OPEN_EXISTING, 0, nullptr);
    STARTUPINFOW si{};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;
    if (out != INVALID_HANDLE_VALUE && in != INVALID_HANDLE_VALUE) {
        si.dwFlags |= STARTF_USESTDHANDLES;
        si.hStdInput = in;
        si.hStdOutput = out;
        si.hStdError = out;
    }
    PROCESS_INFORMATION pi{};
    std::vector<wchar_t> cmd(job->cmd.begin(), job->cmd.end());
    cmd.push_back(L'\0');
    DWORD t0 = GetTickCount();
    if (CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi)) {
        res->started = true;
        if (WaitForSingleObject(pi.hProcess, kTimeoutMs) == WAIT_TIMEOUT) {
            TerminateProcess(pi.hProcess, 1);
            WaitForSingleObject(pi.hProcess, 5000);
            res->timedOut = true;
        }
        GetExitCodeProcess(pi.hProcess, &res->exitCode);
        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
    } else {
        res->error = GetLastError();
    }
    res->elapsedMs = GetTickCount() - t0;
    if (out != INVALID_HANDLE_VALUE) CloseHandle(out);
    if (in != INVALID_HANDLE_VALUE) CloseHandle(in);
    HWND hwnd = job->hwnd;
    UINT doneMsg = job->doneMsg;
    delete job;
    if (!PostMessageW(hwnd, doneMsg, 0, reinterpret_cast<LPARAM>(res))) delete res;
    return 0;
}

std::wstring MeasureScreen() {
    HMONITOR mon = MonitorFromWindow(g.hwnd, MONITOR_DEFAULTTONEAREST);
    MONITORINFOEXW mi{};
    mi.cbSize = sizeof(mi);
    DWORD w = 0, h = 0;
    if (GetMonitorInfoW(mon, &mi)) {
        DEVMODEW dm{};
        dm.dmSize = sizeof(dm);
        if (EnumDisplaySettingsW(mi.szDevice, ENUM_CURRENT_SETTINGS, &dm)) {
            w = dm.dmPelsWidth;
            h = dm.dmPelsHeight;
        } else {
            w = static_cast<DWORD>(mi.rcMonitor.right - mi.rcMonitor.left);
            h = static_cast<DWORD>(mi.rcMonitor.bottom - mi.rcMonitor.top);
        }
    }
    wchar_t buf[64];
    wsprintfW(buf, L"%lux%lux%u", static_cast<unsigned long>(w), static_cast<unsigned long>(h), WindowDpi(g.hwnd));
    return buf;
}

std::wstring Timestamp() {
    SYSTEMTIME st;
    GetLocalTime(&st);
    wchar_t buf[32];
    wsprintfW(buf, L"%04d%02d%02d_%02d%02d%02d", st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
    return buf;
}

std::wstring AppDataDir() {
    wchar_t local[MAX_PATH];
    DWORD n = GetEnvironmentVariableW(L"LOCALAPPDATA", local, MAX_PATH);
    if (n > 0 && n < MAX_PATH) return std::wstring(local) + L"\\ManabitoClassSetup";
    return g.exeDir;
}

// 設定（%LOCALAPPDATA%\ManabitoClassSetup\settings.ini）
std::wstring SettingsPath() {
    std::wstring dir = AppDataDir();
    SHCreateDirectoryExW(nullptr, dir.c_str(), nullptr);
    return dir + L"\\settings.ini";
}

bool ReadSettingFlag(const wchar_t* key) {
    return GetPrivateProfileIntW(L"startup", key, 0, SettingsPath().c_str()) != 0;
}

void WriteSettingFlag(const wchar_t* key, bool value) {
    WritePrivateProfileStringW(L"startup", key, value ? L"1" : L"0", SettingsPath().c_str());
}

// スマート アプリ コントロールの状態：0=オフ 1=オン 2=評価中 -1=この Windows には無い
int ReadSacState() {
    DWORD v = 0, size = sizeof(v);
    LSTATUS r = RegGetValueW(HKEY_LOCAL_MACHINE, L"SYSTEM\\CurrentControlSet\\Control\\CI\\Policy",
                             L"VerifiedAndReputablePolicyState", RRF_RT_REG_DWORD, nullptr, &v, &size);
    if (r != ERROR_SUCCESS) return -1;
    return static_cast<int>(v);
}

void EnsureLogDir() {
    if (!g.logDir.empty()) {
        SHCreateDirectoryExW(nullptr, g.logDir.c_str(), nullptr);
        return;
    }
    g.logDir = AppDataDir() + L"\\logs";
    SHCreateDirectoryExW(nullptr, g.logDir.c_str(), nullptr);
}

void SetBusy(bool busy) {
    g.running = busy;
    for (HWND h : {g.run, g.oneclick, g.apply, g.clean, g.combo, g.tog[0], g.tog[1], g.tog[2], g.runNow, g.timePick}) {
        if (!h) continue;
        EnableWindow(h, !busy);
        InvalidateRect(h, nullptr, FALSE);
    }
    // 準備の最中は［時間を指定して実行］が［取り消す］になる
    if (g.runAt) {
        EnableWindow(g.runAt, !busy || (g.deploying && !g.deployCancelRequested));
        InvalidateRect(g.runAt, nullptr, FALSE);
    }
}

std::wstring PowerShellPath() {
    wchar_t sys[MAX_PATH];
    UINT n = GetSystemDirectoryW(sys, MAX_PATH);
    std::wstring ps = (n > 0 && n < MAX_PATH) ? std::wstring(sys) : std::wstring(L"C:\\Windows\\System32");
    return ps + L"\\WindowsPowerShell\\v1.0\\powershell.exe";
}

std::wstring SelectedProfilePath() {
    int sel = static_cast<int>(SendMessageW(g.combo, CB_GETCURSEL, 0, 0));
    if (sel >= 0 && sel < static_cast<int>(g.profiles.size())) return g.profiles[static_cast<size_t>(sel)].path;
    return std::wstring();
}

void StartInspection(bool afterApply = false) {
    if (g.running) return;
    if (!afterApply) {
        g.applyNote.clear();
        if (!g.progress.empty()) {
            g.progress.clear();
            Layout();
        }
    }
    std::wstring script = g.exeDir + L"\\scripts\\inspect.ps1";
    if (!FileExists(script)) {
        MessageBoxW(g.hwnd,
                    (L"点検の部品が見つかりません。\n\n" + script +
                     L"\n\nEXE と同じフォルダーに scripts フォルダーを置いてください。").c_str(),
                    kAppName, MB_OK | MB_ICONWARNING);
        return;
    }
    std::wstring profile = SelectedProfilePath();

    EnsureLogDir();
    std::wstring base = g.logDir + L"\\点検_" + Timestamp();
    g.currentTsv = base + L".tsv";
    g.currentDetail = base + L"_詳細.txt";
    g.currentOut = base + L"_PowerShell.txt";

    std::wstring cmd = L"\"" + PowerShellPath() + L"\" -NoProfile -NonInteractive -ExecutionPolicy Bypass -File \"" + script + L"\"";
    if (!profile.empty()) cmd += L" -ProfilePath \"" + profile + L"\"";
    cmd += L" -OutFile \"" + g.currentTsv + L"\" -DetailFile \"" + g.currentDetail + L"\" -Screen " + MeasureScreen();
    cmd += L" -AppPath \"" + g.exeDir + L"\"";

    Job* job = new Job();
    job->cmd = cmd;
    job->tsv = g.currentTsv;
    job->outLog = g.currentOut;
    job->hwnd = g.hwnd;
    HANDLE th = CreateThread(nullptr, 0, JobThread, job, 0, nullptr);
    if (!th) {
        delete job;
        MessageBoxW(g.hwnd, L"点検を始められませんでした。", kAppName, MB_OK | MB_ICONERROR);
        return;
    }
    CloseHandle(th);
    SetBusy(true);
    SetSummaryText(afterApply ? L"そろえ終わりました。続けて点検しています…"
                              : L"点検しています…（設定は何も変えません。30 秒ほどかかることがあります）",
                   kAccent);
}

// ---------------------------------------------------------------- 設定をそろえる（管理者として）

DWORD WINAPI ApplyThread(LPVOID param) {
    ApplyJob* job = static_cast<ApplyJob*>(param);
    JobResult* res = new JobResult();
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    SHELLEXECUTEINFOW sei{};
    sei.cbSize = sizeof(sei);
    sei.fMask = SEE_MASK_NOCLOSEPROCESS | SEE_MASK_NOASYNC;
    sei.hwnd = job->hwnd;
    sei.lpVerb = L"runas";
    sei.lpFile = job->exe.c_str();
    sei.lpParameters = job->params.c_str();
    sei.nShow = SW_HIDE;
    DWORD t0 = GetTickCount();
    if (ShellExecuteExW(&sei) && sei.hProcess) {
        res->started = true;
        if (WaitForSingleObject(sei.hProcess, job->timeoutMs) == WAIT_TIMEOUT) res->timedOut = true;
        GetExitCodeProcess(sei.hProcess, &res->exitCode);
        CloseHandle(sei.hProcess);
    } else {
        res->error = GetLastError();
    }
    res->elapsedMs = GetTickCount() - t0;
    CoUninitialize();
    HWND hwnd = job->hwnd;
    WPARAM kind = static_cast<WPARAM>(job->kind);
    delete job;
    if (!PostMessageW(hwnd, WM_APP_APPLIED, kind, reinterpret_cast<LPARAM>(res))) delete res;
    return 0;
}

std::wstring CurrentUserName() {
    wchar_t buf[257];
    DWORD n = 257;
    if (GetUserNameW(buf, &n)) return buf;
    return std::wstring();
}

void StartApply() {
    if (g.running) return;
    std::wstring script = g.exeDir + L"\\scripts\\apply.ps1";
    if (!FileExists(script)) {
        MessageBoxW(g.hwnd, (L"そろえる部品が見つかりません。\n\n" + script).c_str(), kAppName, MB_OK | MB_ICONWARNING);
        return;
    }
    std::wstring profile = SelectedProfilePath();
    std::wstring profileName = L"（なし）";
    int sel = static_cast<int>(SendMessageW(g.combo, CB_GETCURSEL, 0, 0));
    if (sel >= 0 && sel < static_cast<int>(g.profiles.size())) profileName = g.profiles[static_cast<size_t>(sel)].name;

    std::wstring ask =
        L"「" + profileName +
        L"」の内容で、この PC の設定を講習用にそろえます。\n\n"
        L"変えるもの：スリープ・画面の消灯・休止、ふたを閉じたとき、デスクトップの背景、ファイルの拡張子、"
        L"タスクバー（ウィジェット・検索）、通知、OneDrive の自動起動、音量（消音）、Windows Update\n\n"
        L"・履歴やファイルは消しません。\n"
        L"・管理者の確認が出たら「はい」を押してください。\n"
        L"・タスクバーなどを変えるときは、エクスプローラーを起動し直します（画面が一瞬消え、開いているフォルダーの窓が閉じます）。\n\n"
        L"そろえますか？";
    if (MessageBoxW(g.hwnd, ask.c_str(), kAppName, MB_YESNO | MB_ICONQUESTION | MB_DEFBUTTON2) != IDYES) return;

    EnsureLogDir();
    std::wstring base = g.logDir + L"\\そろえる_" + Timestamp();
    g.applyTsv = base + L".tsv";
    g.applyLog = base + L"_PowerShell.txt";

    std::wstring params = L"-NoProfile -NonInteractive -ExecutionPolicy Bypass -WindowStyle Hidden -File \"" + script + L"\"";
    if (!profile.empty()) params += L" -ProfilePath \"" + profile + L"\"";
    params += L" -OutFile \"" + g.applyTsv + L"\" -LogFile \"" + g.applyLog + L"\"";
    std::wstring user = CurrentUserName();
    if (!user.empty()) params += L" -ExpectedUser \"" + user + L"\"";

    ApplyJob* job = new ApplyJob();
    job->exe = PowerShellPath();
    job->params = params;
    job->hwnd = g.hwnd;
    HANDLE th = CreateThread(nullptr, 0, ApplyThread, job, 0, nullptr);
    if (!th) {
        delete job;
        MessageBoxW(g.hwnd, L"そろえる作業を始められませんでした。", kAppName, MB_OK | MB_ICONERROR);
        return;
    }
    CloseHandle(th);
    g.applying = true;
    SetBusy(true);
    SetSummaryText(L"設定をそろえています…（管理者の確認が出たら「はい」を押してください）", kAccent);
}

// 管理者として動かした作業（そろえる・履歴を消す）の結果を受け取り、続けて点検する
struct ElevatedText {
    const wchar_t* cancelled;   // UAC で「いいえ」
    const wchar_t* cannotStart; // 起動できない（%lu にエラー番号）
    const wchar_t* noResult;    // 結果を受け取れない
    const wchar_t* doneLabel;   // 「そろえました」「消しました」
    const wchar_t* countLabel;  // 「変えた」「消した」
    const wchar_t* failTitle;   // 失敗の一覧の見出し
};

void FinishElevated(JobResult* res, const std::wstring& tsv, const std::wstring& log, const ElevatedText& t) {
    g.applying = false;
    SetBusy(false);
    g.applyNote.clear();
    if (!res->started) {
        if (res->error == ERROR_CANCELLED) {
            SetSummaryText(t.cancelled, kMuted);
        } else {
            wchar_t buf[200];
            wsprintfW(buf, t.cannotStart, static_cast<unsigned long>(res->error));
            SetSummaryText(buf, kNg);
            MessageBoxW(g.hwnd, buf, kAppName, MB_OK | MB_ICONERROR);
        }
        return;
    }
    std::vector<Row> results;
    if (!FileExists(tsv) || !LoadResults(tsv, results)) {
        std::wstring msg = res->timedOut ? L"5 分たっても終わらないため、待つのをやめました。" : std::wstring(t.noResult);
        msg += L"\n\n記録：" + log;
        g.lastDetailFile = log;
        SetSummaryText(std::wstring(t.noResult) + L"［記録のフォルダー］を見てください。", kNg);
        MessageBoxW(g.hwnd, msg.c_str(), kAppName, MB_OK | MB_ICONWARNING);
        StartInspection(true);
        return;
    }
    int done = 0, failed = 0;
    std::wstring failText, total;
    for (const auto& r : results) {
        if (r.id == L"total") total = L"・" + r.detail;  // 全体の所要時間
        if (r.state == L"DONE" && r.id != L"explorer") ++done;
        if (r.state == L"FAIL") {
            ++failed;
            failText += L"・" + r.name + (r.detail.empty() ? std::wstring() : L"：" + r.detail) + L"\n";
        }
    }
    g.lastDetailFile = tsv;
    g.applyNote.push_back({std::wstring(t.doneLabel) + L"（" + t.countLabel + L" " + std::to_wstring(done) + L"・失敗 " +
                               std::to_wstring(failed) + total + L"）",
                           failed ? kNg : kAccent});
    g.applyNote.push_back({L"　｜　点検：", kMuted});
    if (failed) {
        MessageBoxW(g.hwnd, (std::wstring(t.failTitle) + L"\n\n" + failText + L"\n記録：" + log).c_str(), kAppName,
                    MB_OK | MB_ICONWARNING);
    }
    StartInspection(true);
}

void FinishApply(JobResult* res) {
    static const ElevatedText t = {
        L"そろえるのを取りやめました（管理者の確認で「いいえ」が押されました）。設定は変えていません。",
        L"そろえる作業を始められませんでした（エラー %lu）。",
        L"そろえた結果を受け取れませんでした。",
        L"そろえました",
        L"変えた",
        L"そろえられなかった項目があります。",
    };
    FinishElevated(res, g.applyTsv, g.applyLog, t);
}

// ---------------------------------------------------------------- 前の受講生の履歴を消す（管理者として）

// atExit：アプリを閉じるときに呼ばれた（確認は済んでいるので聞かず、終わったら閉じる）
void StartClean(bool atExit = false) {
    if (g.running) return;
    std::wstring script = g.exeDir + L"\\scripts\\clean.ps1";
    if (!FileExists(script)) {
        MessageBoxW(g.hwnd, (L"履歴を消す部品が見つかりません。\n\n" + script).c_str(), kAppName, MB_OK | MB_ICONWARNING);
        return;
    }
    std::wstring ask =
        L"前の受講生の履歴とファイルを消します。消したものは元に戻せません（ごみ箱にも残りません）。\n\n"
        L"消すもの：\n"
        L"・最近使ったファイル、アドレス欄と「ファイル名を指定して実行」の履歴\n"
        L"・Office とメモ帳の履歴\n"
        L"・日本語入力（IME）の学習とユーザー辞書（登録した単語も消えます）\n"
        L"・ダウンロード フォルダーの中身、デスクトップのファイル（ショートカット以外）\n"
        L"・ごみ箱\n"
        L"・Edge・Chrome のお気に入り・履歴・Cookie・保存されたパスワード（ブラウザーは閉じます。開いているタブは失われます）\n\n"
        L"残すもの：このアプリが入っているフォルダー、OneDrive と同期しているフォルダー、クイック アクセスのピン留め\n\n"
        L"Word・Excel・PowerPoint・メモ帳は、閉じてから進めてください。\n"
        L"管理者の確認が出たら「はい」を押してください。\n\n"
        L"消せないものがあっても、消せるものは消し、消せなかったものだけを最後にお知らせします。\n\n"
        L"消しますか？";
    if (!atExit && MessageBoxW(g.hwnd, ask.c_str(), kAppName, MB_YESNO | MB_ICONWARNING | MB_DEFBUTTON2) != IDYES) return;

    EnsureLogDir();
    std::wstring base = g.logDir + L"\\履歴を消す_" + Timestamp();
    g.cleanTsv = base + L".tsv";
    g.cleanLog = base + L"_PowerShell.txt";
    std::wstring params = L"-NoProfile -NonInteractive -ExecutionPolicy Bypass -WindowStyle Hidden -File \"" + script + L"\"";
    std::wstring profile = SelectedProfilePath();
    if (!profile.empty()) params += L" -ProfilePath \"" + profile + L"\"";
    params += L" -OutFile \"" + g.cleanTsv + L"\" -LogFile \"" + g.cleanLog + L"\"";
    std::wstring user = CurrentUserName();
    if (!user.empty()) params += L" -ExpectedUser \"" + user + L"\"";
    params += L" -ProtectPath \"" + g.exeDir + L"\"";

    ApplyJob* job = new ApplyJob();
    job->exe = PowerShellPath();
    job->params = params;
    job->hwnd = g.hwnd;
    job->kind = kJobClean;
    HANDLE th = CreateThread(nullptr, 0, ApplyThread, job, 0, nullptr);
    if (!th) {
        delete job;
        MessageBoxW(g.hwnd, L"履歴を消す作業を始められませんでした。", kAppName, MB_OK | MB_ICONERROR);
        return;
    }
    CloseHandle(th);
    g.applying = true;
    g.closeAfterClean = atExit;
    SetBusy(true);
    SetSummaryText(atExit ? L"作業でできた履歴を消してから閉じます…（管理者の確認が出たら「はい」を押してください）"
                          : L"履歴を消しています…（管理者の確認が出たら「はい」を押してください）",
                   kAccent);
}

void FinishClean(JobResult* res) {
    if (g.closeAfterClean) {
        // 閉じるときの［履歴を消す］：点検はせず、消せなかったものがあれば知らせてから閉じる
        g.closeAfterClean = false;
        g.applying = false;
        SetBusy(false);
        std::vector<Row> results;
        std::wstring failText;
        if (res->started && FileExists(g.cleanTsv) && LoadResults(g.cleanTsv, results)) {
            for (const auto& r : results)
                if (r.state == L"FAIL") failText += L"・" + r.name + (r.detail.empty() ? std::wstring() : L"：" + r.detail) + L"\n";
        } else if (res->started) {
            failText = L"・消した結果を受け取れませんでした。\n";
        } else if (res->error == ERROR_CANCELLED) {
            failText = L"・管理者の確認で「いいえ」が押されたため、何も消していません。\n";
        } else {
            failText = L"・履歴を消す作業を始められませんでした。\n";
        }
        if (!failText.empty())
            MessageBoxW(g.hwnd, (L"消せなかったものがあります（このまま閉じます）。\n\n" + failText + L"\n記録：" + g.cleanLog).c_str(),
                        kAppName, MB_OK | MB_ICONWARNING);
        DestroyWindow(g.hwnd);
        return;
    }
    static const ElevatedText t = {
        L"履歴を消すのを取りやめました（管理者の確認で「いいえ」が押されました）。何も消していません。",
        L"履歴を消す作業を始められませんでした（エラー %lu）。",
        L"消した結果を受け取れませんでした。",
        L"消しました",
        L"消した",
        L"消せなかったものがあります。",
    };
    FinishElevated(res, g.cleanTsv, g.cleanLog, t);
}

void FinishInspection(JobResult* res) {
    SetBusy(false);
    if (!res->started) {
        wchar_t buf[256];
        wsprintfW(buf, L"PowerShell を起動できませんでした（エラー %lu）。", static_cast<unsigned long>(res->error));
        SetSummaryText(buf, kNg);
        MessageBoxW(g.hwnd, buf, kAppName, MB_OK | MB_ICONERROR);
        return;
    }
    RefreshInstalled();
    if (FileExists(g.currentTsv) && LoadResults(g.currentTsv, g.rows)) {
        g.lastDetailFile = g.currentDetail;
        g.lastElapsed = res->elapsedMs;
        g.lastFromSample = false;
        ApplyToggleJudgement();
        FillList();
        BuildSummary(res->elapsedMs, false);
        InvalidateRect(g.hwnd, nullptr, FALSE);
        return;
    }
    // 結果ができなかった：PowerShell の出力を見せる
    std::wstring msg = res->timedOut ? L"点検が 3 分を超えたため、打ち切りました。" : L"点検の結果を受け取れませんでした。";
    std::string bytes;
    if (ReadAllBytes(g.currentOut, bytes) && !bytes.empty()) {
        std::wstring t = DecodeText(bytes);
        if (t.size() > 900) t = t.substr(0, 900) + L"…";
        msg += L"\n\nPowerShell の出力：\n" + t;
    }
    msg += L"\n\n記録：" + g.currentOut;
    g.lastDetailFile = g.currentOut;
    SetSummaryText(L"点検できませんでした。［記録のフォルダー］の PowerShell.txt を見てください。", kNg);
    MessageBoxW(g.hwnd, msg.c_str(), kAppName, MB_OK | MB_ICONWARNING);
}

// ---------------------------------------------------------------- インターネットへのつながり

// Windows のネットワーク一覧（NLM）に、インターネットにつながっているかを聞く。分からないときは「つながっている」とみなす。
bool IsInternetConnected() {
    static const CLSID kClsidNlm = {0xDCB00C01, 0x570F, 0x4A9B, {0x8D, 0x69, 0x19, 0x9F, 0xDB, 0xA5, 0x72, 0x3B}};
    static const IID kIidNlm = {0xDCB00000, 0x570F, 0x4A9B, {0x8D, 0x69, 0x19, 0x9F, 0xDB, 0xA5, 0x72, 0x3B}};
    INetworkListManager* nlm = nullptr;
    if (FAILED(CoCreateInstance(kClsidNlm, nullptr, CLSCTX_ALL, kIidNlm, reinterpret_cast<void**>(&nlm))) || !nlm) return true;
    NLM_CONNECTIVITY c = NLM_CONNECTIVITY_DISCONNECTED;
    HRESULT hr = nlm->GetConnectivity(&c);
    nlm->Release();
    if (FAILED(hr)) return true;
    return (c & (NLM_CONNECTIVITY_IPV4_INTERNET | NLM_CONNECTIVITY_IPV6_INTERNET)) != 0;
}

void OpenNetworkSettings() {
    // 右下の「ネットワーク」の一覧（Wi-Fi を選べる）。開けなければ設定アプリのネットワーク
    if (reinterpret_cast<INT_PTR>(ShellExecuteW(g.hwnd, L"open", L"ms-availablenetworks:", nullptr, nullptr, SW_SHOWNORMAL)) <= 32)
        ShellExecuteW(g.hwnd, L"open", L"ms-settings:network", nullptr, nullptr, SW_SHOWNORMAL);
}

// つながっていなければ、つなぐよう促す。戻り値：つながった、または「つながずに続ける」→ true。やめる → false。
// purpose：何のためにつなぐのか（画面に出す）
bool EnsureInternet(const wchar_t* purpose, bool allowContinue) {
    const int kOpen = 3001, kRetry = 3002, kContinue = 3003;
    while (!IsInternetConnected()) {
        TASKDIALOG_BUTTON buttons[] = {
            {kOpen, L"ネットワークにつなぐ\nWi-Fi の一覧を開きます。つないだら［もう一度確かめる］を押してください。"},
            {kRetry, L"もう一度確かめる"},
            {kContinue, allowContinue ? L"つながずに続ける\n点検や設定はできますが、アプリの入手やインストールはできません。" : L"やめる"},
        };
        TASKDIALOGCONFIG c{};
        c.cbSize = sizeof(c);
        c.hwndParent = g.hwnd;
        c.hInstance = g.inst;
        c.dwFlags = TDF_USE_COMMAND_LINKS | TDF_ALLOW_DIALOG_CANCELLATION | TDF_POSITION_RELATIVE_TO_WINDOW;
        c.pszWindowTitle = kAppName;
        c.pszMainIcon = TD_WARNING_ICON;
        c.pszMainInstruction = L"インターネットにつながっていません";
        c.pszContent = purpose;
        c.cButtons = 3;
        c.pButtons = buttons;
        c.nDefaultButton = kOpen;
        int button = 0;
        if (FAILED(TaskDialogIndirect(&c, &button, nullptr, nullptr))) {
            std::wstring m = std::wstring(L"インターネットにつながっていません。\n\n") + purpose +
                             L"\n\n［はい］ネットワークの設定を開く　［いいえ］つながずに続ける";
            button = (MessageBoxW(g.hwnd, m.c_str(), kAppName, MB_YESNO | MB_ICONWARNING) == IDYES) ? kOpen : kContinue;
        }
        if (button == kOpen) {
            OpenNetworkSettings();
            continue;  // 開いたあと、もう一度この案内を出す（つないだら［もう一度確かめる］）
        }
        if (button == kRetry) continue;
        return allowContinue;  // つながずに続ける（起動時）／やめる（作業の前）
    }
    return true;
}

// ---------------------------------------------------------------- 時間のかかる準備（管理者として、時刻まで待てる）

const wchar_t* kToggleNames[3] = {L"Office", L"Adobe CC", L"独自アプリ"};

bool FileExistsIn(const wchar_t* envName, const wchar_t* rel) {
    wchar_t base[MAX_PATH];
    DWORD n = GetEnvironmentVariableW(envName, base, MAX_PATH);
    if (n == 0 || n >= MAX_PATH) return false;
    return FileExists(std::wstring(base) + rel);
}

// Office・Adobe CC が入っているか（トグルをオフにしたときに「削除」になるかどうか）
void RefreshInstalled() {
    wchar_t buf[128];
    DWORD size = sizeof(buf);
    g.installed[0] = RegGetValueW(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Microsoft\\Office\\ClickToRun\\Configuration",
                                  L"VersionToReport", RRF_RT_REG_SZ, nullptr, buf, &size) == ERROR_SUCCESS;
    const wchar_t* cc = L"\\Adobe\\Adobe Creative Cloud\\ACC\\Creative Cloud.exe";
    g.installed[1] = FileExistsIn(L"ProgramFiles", cc) || FileExistsIn(L"ProgramFiles(x86)", cc);
    for (int i = 0; i < 2; ++i)
        if (g.tog[i]) InvalidateRect(g.tog[i], nullptr, FALSE);
}

// トグルがオフで、その PC に入っている → 実行すると削除する
bool WillRemove(int i) { return i < 2 && !g.toggleOn[i] && g.installed[i]; }

// 点検の一覧の Office・Adobe の行を、トグル（オン＝入っているべき、オフ＝入っていないべき）で判定し直す
void ApplyToggleJudgement() {
    for (auto& r : g.rows) {
        int i = (r.id == L"office") ? 0 : (r.id == L"adobe") ? 1 : -1;
        if (i < 0) continue;
        const bool want = g.toggleOn[i];
        r.expected = want ? L"入っている（トグル：オン）" : L"入っていない（トグル：オフ）";
        r.state = (g.installed[i] == want) ? L"OK" : L"NG";
    }
}

// トグルを押したあと、一覧と要約をその場で直す（選んでいた行はそのまま）
void RefreshResultsView() {
    if (g.rows.empty()) return;
    int sel = ListView_GetNextItem(g.list, -1, LVNI_SELECTED);
    LPARAM selParam = -1;
    if (sel >= 0) {
        LVITEMW it{};
        it.mask = LVIF_PARAM;
        it.iItem = sel;
        if (ListView_GetItem(g.list, &it)) selParam = it.lParam;
    }
    FillList();
    BuildSummary(g.lastElapsed, g.lastFromSample);
    if (selParam >= 0) {
        LVFINDINFOW fi{};
        fi.flags = LVFI_PARAM;
        fi.lParam = selParam;
        int at = ListView_FindItem(g.list, -1, &fi);
        if (at >= 0) {
            ListView_SetItemState(g.list, at, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
            ListView_EnsureVisible(g.list, at, FALSE);
        }
    }
    RECT r{0, g.summaryTop, 32767, g.summaryTop + S(28)};
    InvalidateRect(g.hwnd, &r, FALSE);
}

// プロファイルの値から、トグルの初期値を決める
void ApplyProfileToggles() {
    RefreshInstalled();
    std::wstring p = SelectedProfilePath();
    g.toggleOn[0] = (ReadJsonString(p, L"office") == L"install");
    g.toggleOn[1] = (ReadJsonString(p, L"adobe") == L"install");
    g.appsAvailable = !ReadJsonString(p, L"ownAppsManifest").empty();
    g.toggleOn[2] = g.appsAvailable;
    for (HWND h : g.tog)
        if (h) InvalidateRect(h, nullptr, FALSE);
}

void ToggleClicked(int i) {
    if (i == 2 && !g.appsAvailable) {
        MessageBoxW(g.hwnd,
                    L"このプロファイルには、独自アプリの目録（ownAppsManifest）が書かれていません。\n\n"
                    L"目録（apps.json）の置き場所を決めて、プロファイルに URL か共有フォルダーのパスを書くと使えるようになります。",
                    kAppName, MB_OK | MB_ICONINFORMATION);
        return;
    }
    g.toggleOn[i] = !g.toggleOn[i];
    InvalidateRect(g.tog[i], nullptr, FALSE);
    if (i < 2) {
        ApplyToggleJudgement();
        RefreshResultsView();
    }
}

ULONGLONG ToTicks(const SYSTEMTIME& st) {
    FILETIME ft{};
    SystemTimeToFileTime(&st, &ft);
    return (static_cast<ULONGLONG>(ft.dwHighDateTime) << 32) | ft.dwLowDateTime;
}

SYSTEMTIME FromTicks(ULONGLONG t) {
    FILETIME ft{static_cast<DWORD>(t & 0xFFFFFFFFull), static_cast<DWORD>(t >> 32)};
    SYSTEMTIME st{};
    FileTimeToSystemTime(&ft, &st);
    return st;
}

std::wstring StartLabel(const SYSTEMTIME& when) {
    SYSTEMTIME now;
    GetLocalTime(&now);
    wchar_t buf[64];
    const wchar_t* day = (when.wDay == now.wDay && when.wMonth == now.wMonth) ? L"今日" : L"明日";
    wsprintfW(buf, L"%s %d:%02d", day, when.wHour, when.wMinute);
    return buf;
}

void StartDeploy(bool scheduled) {
    if (g.running) return;
    RefreshInstalled();
    bool office = g.toggleOn[0], adobe = g.toggleOn[1], apps = g.toggleOn[2] && g.appsAvailable;
    bool removeOffice = WillRemove(0), removeAdobe = WillRemove(1);
    if (!office && !adobe && !apps && !removeOffice && !removeAdobe) {
        MessageBoxW(g.hwnd, L"準備するものを選んでください（Office・Adobe CC・独自アプリのトグルをオンにします）。", kAppName,
                    MB_OK | MB_ICONINFORMATION);
        return;
    }
    SYSTEM_POWER_STATUS power{};
    if (GetSystemPowerStatus(&power) && power.ACLineStatus == 0) {
        MessageBoxW(g.hwnd,
                    L"電池で動いています。\n\n時間のかかる準備は、電源ケーブルをつないでから始めてください"
                    L"（途中で電池が切れると、インストールが壊れることがあります）。",
                    kAppName, MB_OK | MB_ICONWARNING);
        return;
    }
    std::wstring script = g.exeDir + L"\\scripts\\deploy.ps1";
    if (!FileExists(script)) {
        MessageBoxW(g.hwnd, (L"準備の部品が見つかりません。\n\n" + script).c_str(), kAppName, MB_OK | MB_ICONWARNING);
        return;
    }
    if (!scheduled && !EnsureInternet(L"アプリの入手・インストール・削除ツールの入手には、インターネットが必要です。"
                                      L"Wi-Fi や LAN ケーブルをつないでください。", false))
        return;

    std::wstring whenText = L"今すぐ";
    std::wstring startArg;
    if (scheduled) {
        SYSTEMTIME pick{}, now{};
        DateTime_GetSystemtime(g.timePick, &pick);
        GetLocalTime(&now);
        SYSTEMTIME target = now;
        target.wHour = pick.wHour;
        target.wMinute = pick.wMinute;
        target.wSecond = 0;
        target.wMilliseconds = 0;
        ULONGLONG t = ToTicks(target);
        if (t <= ToTicks(now)) t += 864000000000ull;  // 過ぎていたら翌日
        g.deployStart = FromTicks(t);
        wchar_t buf[32];
        wsprintfW(buf, L"%04d-%02d-%02d %02d:%02d", g.deployStart.wYear, g.deployStart.wMonth, g.deployStart.wDay,
                  g.deployStart.wHour, g.deployStart.wMinute);
        startArg = buf;
        whenText = StartLabel(g.deployStart);
        wchar_t hm[8];
        wsprintfW(hm, L"%02d:%02d", pick.wHour, pick.wMinute);
        WritePrivateProfileStringW(L"deploy", L"time", hm, SettingsPath().c_str());
    }

    std::wstring items;
    if (office) items += L"・Microsoft 365（Office）のインストール（入っていれば何もしません）\n";
    if (adobe) {
        items += L"・Adobe Creative Cloud デスクトップアプリのインストール（入っていれば何もしません）\n";
        if (!g.installed[1])
            items += L"　※ Adobe のインストーラーは、サインインなどの画面を出して待つことがあります。"
                     L"人がいるときに［今すぐ実行］で行うのがおすすめです（15 分進まなければ打ち切ります）。\n";
    }
    if (apps) items += L"・独自アプリの最新版の入手と展開（同じ版なら何もしません）\n";
    if ((office && !g.installed[0]) || (adobe && !g.installed[1])) {
        items += L"\n【時間の目安】ダウンロードの量と回線の速さで決まります（インストールの時間は別にかかります）\n";
        if (office && !g.installed[0])
            items += L"・Office：約 3.5 GB　→ 100 Mbps なら約 5 分、30 Mbps なら約 16 分、10 Mbps なら約 47 分\n";
        if (adobe && !g.installed[1])
            items += L"・Adobe CC デスクトップアプリ：約 0.6 GB　→ 100 Mbps なら約 1 分、30 Mbps なら約 3 分\n";
        items += L"　作業中は、進み具合・回線の速さ・残り時間の目安を画面に表示します。\n";
    }
    const bool removing = removeOffice || removeAdobe;
    if (removing) {
        items += L"\n【削除するもの（元に戻せません）】トグルがオフで、この PC に入っているもの\n";
        if (removeOffice)
            items += L"・Microsoft 365（Office）：マイクロソフトの削除ツールで完全に削除します。Word・Excel などはすべて使えなくなります。\n";
        if (removeAdobe)
            items += L"・Adobe Creative Cloud：Adobe の削除ツールで、Creative Cloud と Adobe のアプリをすべて削除します。\n";
        items += L"　残したいときは「いいえ」を押し、そのトグルをオンにしてから実行してください。\n";
    }
    std::wstring ask = L"次の準備を「" + whenText + L"」に始めます。\n\n" + items +
                       L"\n・管理者の確認が出たら「はい」を押してください（確認はこの 1 回だけです）。\n"
                       L"・終わるまで PC の電源を切らないでください。スリープはしないようにします。\n"
                       L"・インターネットにつないだままにしてください。\n"
                       L"・このアプリを開いたままにすると、進み具合と結果が見られます（閉じても作業は続きます）。\n\n"
                       L"始めますか？";
    UINT style = removing ? (MB_YESNO | MB_ICONWARNING | MB_DEFBUTTON2) : (MB_YESNO | MB_ICONQUESTION | MB_DEFBUTTON1);
    if (MessageBoxW(g.hwnd, ask.c_str(), kAppName, style) != IDYES) return;

    EnsureLogDir();
    std::wstring base = g.logDir + L"\\準備_" + Timestamp();
    g.deployTsv = base + L".tsv";
    g.deployLog = base + L"_PowerShell.txt";
    g.deployStatus = base + L"_状況.txt";
    g.deployCancel = base + L"_取り消し.flag";
    DeleteFileW(g.deployCancel.c_str());
    std::wstring params = L"-NoProfile -NonInteractive -ExecutionPolicy Bypass -WindowStyle Hidden -File \"" + script + L"\"";
    std::wstring profile = SelectedProfilePath();
    if (!profile.empty()) params += L" -ProfilePath \"" + profile + L"\"";
    params += L" -OutFile \"" + g.deployTsv + L"\" -LogFile \"" + g.deployLog + L"\" -StatusFile \"" + g.deployStatus +
              L"\" -CancelFile \"" + g.deployCancel + L"\"";
    // 0=何もしない 1=入れる 2=削除する
    params += std::wstring(L" -Office ") + (office ? L"1" : removeOffice ? L"2" : L"0") + L" -Adobe " +
              (adobe ? L"1" : removeAdobe ? L"2" : L"0") + L" -OwnApps " + (apps ? L"1" : L"0");
    if (!startArg.empty()) params += L" -StartAt \"" + startArg + L"\"";

    ApplyJob* job = new ApplyJob();
    job->exe = PowerShellPath();
    job->params = params;
    job->hwnd = g.hwnd;
    job->kind = kJobDeploy;
    job->timeoutMs = INFINITE;
    HANDLE th = CreateThread(nullptr, 0, ApplyThread, job, 0, nullptr);
    if (!th) {
        delete job;
        MessageBoxW(g.hwnd, L"準備を始められませんでした。", kAppName, MB_OK | MB_ICONERROR);
        return;
    }
    CloseHandle(th);
    g.applying = true;
    g.deploying = true;
    g.deployScheduled = scheduled;
    g.deployCancelRequested = false;
    g.deployShown.clear();
    g.progress.clear();
    g.progressMode.clear();
    Layout();
    SetWindowTextW(g.runAt, L"取り消す");
    SetBusy(true);
    // この画面も「動作中」を Windows に伝える（作業側の PowerShell も伝えている）
    SetThreadExecutionState(ES_CONTINUOUS | ES_SYSTEM_REQUIRED | ES_DISPLAY_REQUIRED);
    SetTimer(g.hwnd, kTimerDeploy, 1000, nullptr);
    SetSummaryText(L"管理者の確認を待っています…（「はい」を押してください）", kAccent);
}

// ［まとめて実行］：設定をそろえる → アプリの削除とインストール（トグルのとおり）→ 履歴を消す、を 1 回の管理者の確認で
void StartOneClick() {
    if (g.running) return;
    std::wstring script = g.exeDir + L"\\scripts\\oneclick.ps1";
    if (!FileExists(script)) {
        MessageBoxW(g.hwnd, (L"まとめて実行の部品が見つかりません。\n\n" + script).c_str(), kAppName, MB_OK | MB_ICONWARNING);
        return;
    }
    RefreshInstalled();
    bool office = g.toggleOn[0], adobe = g.toggleOn[1], apps = g.toggleOn[2] && g.appsAvailable;
    bool removeOffice = WillRemove(0), removeAdobe = WillRemove(1);
    bool needNet = (office && !g.installed[0]) || (adobe && !g.installed[1]) || apps || removeOffice || removeAdobe;
    if (needNet && !EnsureInternet(L"アプリの入手・インストール・削除ツールの入手には、インターネットが必要です。"
                                   L"Wi-Fi や LAN ケーブルをつないでください。", false))
        return;
    SYSTEM_POWER_STATUS power{};
    if (needNet && GetSystemPowerStatus(&power) && power.ACLineStatus == 0) {
        MessageBoxW(g.hwnd, L"電池で動いています。電源ケーブルをつないでから始めてください（途中で電池が切れると、インストールが壊れることがあります）。",
                    kAppName, MB_OK | MB_ICONWARNING);
        return;
    }
    std::wstring ask = L"次の作業を、今すぐ続けて行います。管理者の確認は 1 回だけです。\n\n";
    ask += L"1. 設定をそろえる（スリープ・背景・通知・Windows Update など）\n";
    ask += L"2. アプリ\n";
    bool anyApp = false;
    if (office) { ask += L"　・Office：入っていなければインストール\n"; anyApp = true; }
    if (adobe) { ask += L"　・Adobe CC：入っていなければインストール（サインインなどの画面が出ることがあります）\n"; anyApp = true; }
    if (apps) { ask += L"　・独自アプリ：最新版を入手して展開\n"; anyApp = true; }
    if (removeOffice) { ask += L"　・Office：削除（元に戻せません）\n"; anyApp = true; }
    if (removeAdobe) { ask += L"　・Adobe CC：削除（元に戻せません）\n"; anyApp = true; }
    if (!anyApp) ask += L"　・（なし）\n";
    ask += L"3. 前の受講生の履歴を消す（最近使ったファイル・ダウンロード・IME・Edge と Chrome など。元に戻せません。ブラウザーは閉じます）\n\n";
    ask += L"Word・Excel・PowerPoint・メモ帳は閉じてから進めてください。\n\n始めますか？";
    UINT style = (removeOffice || removeAdobe) ? (MB_YESNO | MB_ICONWARNING | MB_DEFBUTTON2) : (MB_YESNO | MB_ICONQUESTION);
    if (MessageBoxW(g.hwnd, ask.c_str(), kAppName, style) != IDYES) return;

    EnsureLogDir();
    std::wstring base = g.logDir + L"\\まとめて実行_" + Timestamp();
    g.deployTsv = base + L".tsv";
    g.deployLog = base + L"_PowerShell.txt";
    g.deployStatus = base + L"_状況.txt";
    g.deployCancel = base + L"_取り消し.flag";
    DeleteFileW(g.deployCancel.c_str());
    std::wstring params = L"-NoProfile -NonInteractive -ExecutionPolicy Bypass -WindowStyle Hidden -File \"" + script + L"\"";
    std::wstring profile = SelectedProfilePath();
    if (!profile.empty()) params += L" -ProfilePath \"" + profile + L"\"";
    params += L" -OutFile \"" + g.deployTsv + L"\" -LogFile \"" + g.deployLog + L"\" -StatusFile \"" + g.deployStatus +
              L"\" -CancelFile \"" + g.deployCancel + L"\"";
    std::wstring user = CurrentUserName();
    if (!user.empty()) params += L" -ExpectedUser \"" + user + L"\"";
    params += L" -ProtectPath \"" + g.exeDir + L"\"";
    params += std::wstring(L" -Office ") + (office ? L"1" : removeOffice ? L"2" : L"0") + L" -Adobe " +
              (adobe ? L"1" : removeAdobe ? L"2" : L"0") + L" -OwnApps " + (apps ? L"1" : L"0");

    ApplyJob* job = new ApplyJob();
    job->exe = PowerShellPath();
    job->params = params;
    job->hwnd = g.hwnd;
    job->kind = kJobOneClick;
    job->timeoutMs = INFINITE;
    HANDLE th = CreateThread(nullptr, 0, ApplyThread, job, 0, nullptr);
    if (!th) {
        delete job;
        return;
    }
    CloseHandle(th);
    g.applying = true;
    g.deploying = true;
    g.oneClick = true;
    g.deployScheduled = false;
    g.deployCancelRequested = false;
    g.deployShown.clear();
    g.progress.clear();
    g.progressMode.clear();
    Layout();
    SetWindowTextW(g.runAt, L"取り消す");
    SetBusy(true);
    SetThreadExecutionState(ES_CONTINUOUS | ES_SYSTEM_REQUIRED | ES_DISPLAY_REQUIRED);
    SetTimer(g.hwnd, kTimerDeploy, 1000, nullptr);
    SetSummaryText(L"管理者の確認を待っています…（「はい」を押してください）", kAccent);
}

void CancelDeploy() {
    if (!g.deploying || g.deployCancelRequested) return;
    if (MessageBoxW(g.hwnd,
                    L"準備を取り消しますか？\n\n開始前ならすぐに止まります。インストールの途中なら、今のものが終わってから止まります。",
                    kAppName, MB_YESNO | MB_ICONQUESTION | MB_DEFBUTTON2) != IDYES)
        return;
    HANDLE f = CreateFileW(g.deployCancel.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f != INVALID_HANDLE_VALUE) CloseHandle(f);
    g.deployCancelRequested = true;
    SetBusy(true);
    SetSummaryText(L"取り消しています…", kNg);
}

// 状況ファイルを読み、作業ごとの行（g.progress）を更新する。行の数が変わったら true。
bool ReadDeployStatus() {
    std::string bytes;
    if (!ReadAllBytes(g.deployStatus, bytes)) return false;
    std::wstring t = DecodeText(bytes);
    std::vector<ProgressItem> items;
    std::wstring mode;
    size_t pos = 0;
    while (pos < t.size()) {
        size_t nl = t.find(L'\n', pos);
        std::wstring line = t.substr(pos, nl == std::wstring::npos ? std::wstring::npos : nl - pos);
        pos = (nl == std::wstring::npos) ? t.size() : nl + 1;
        if (!line.empty() && line.back() == L'\r') line.pop_back();
        std::vector<std::wstring> f;
        size_t a = 0;
        for (;;) {
            size_t b = line.find(L'|', a);
            f.push_back(line.substr(a, b == std::wstring::npos ? std::wstring::npos : b - a));
            if (b == std::wstring::npos) break;
            a = b + 1;
        }
        if (f.size() >= 3 && f[0] == L"head") {
            mode = f[1];
            if (mode != L"wait") {
                SYSTEMTIME st{};
                int y = 0, mo = 0, d = 0, h = 0, mi = 0, se = 0;
                if (swscanf_s(f[2].c_str(), L"%d-%d-%d %d:%d:%d", &y, &mo, &d, &h, &mi, &se) >= 5) {
                    st.wYear = static_cast<WORD>(y); st.wMonth = static_cast<WORD>(mo); st.wDay = static_cast<WORD>(d);
                    st.wHour = static_cast<WORD>(h); st.wMinute = static_cast<WORD>(mi); st.wSecond = static_cast<WORD>(se);
                    g.progressStart = st;
                }
            }
        } else if (f.size() >= 9 && f[0] == L"item") {
            ProgressItem it;
            it.id = f[1]; it.name = f[2]; it.state = f[3];
            it.step = _wtoi(f[4].c_str()); it.steps = _wtoi(f[5].c_str());
            it.stepName = f[6]; it.percent = _wtoi(f[7].c_str()); it.text = f[8];
            items.push_back(it);
        }
    }
    bool countChanged = items.size() != g.progress.size();
    g.progress.swap(items);
    g.progressMode = mode;
    return countChanged;
}

void OnDeployTimer() {
    if (!g.deploying) return;
    std::wstring text;
    COLORREF color = kAccent;
    bool have = FileExists(g.deployStatus);
    if (have && ReadDeployStatus()) Layout();
    else if (have) {
        RECT r{0, g.progressTop, 32767, g.progressTop + g.progressHeight};
        InvalidateRect(g.hwnd, &r, FALSE);
    }
    if (!have) {
        text = L"管理者の確認を待っています…（「はい」を押してください）";
    } else if (g.progressMode == L"wait") {
        SYSTEMTIME now;
        GetLocalTime(&now);
        ULONGLONG a = ToTicks(now), b = ToTicks(g.deployStart);
        ULONGLONG left = (b > a) ? (b - a) / 10000000ull : 0;  // 秒
        wchar_t buf[200];
        wsprintfW(buf, L"「%s」に準備を始めます（あと %lu 時間 %lu 分）。PC はこのままにしてください。",
                  StartLabel(g.deployStart).c_str(), static_cast<unsigned long>(left / 3600),
                  static_cast<unsigned long>((left % 3600) / 60));
        text = buf;
    } else {
        SYSTEMTIME now;
        GetLocalTime(&now);
        ULONGLONG a = ToTicks(now), b = ToTicks(g.progressStart);
        ULONGLONG sec = (a > b) ? (a - b) / 10000000ull : 0;
        int done = 0;
        for (const auto& it : g.progress)
            if (it.state != L"wait" && it.state != L"run") ++done;
        wchar_t buf[200];
        wsprintfW(buf, g.oneClick ? L"まとめて実行しています（%d / %d 件済み・全体の経過 %lu 分 %02lu 秒）"
                                  : L"時間のかかる準備をしています（%d / %d 件済み・全体の経過 %lu 分 %02lu 秒）", done,
                  static_cast<int>(g.progress.size()), static_cast<unsigned long>(sec / 60),
                  static_cast<unsigned long>(sec % 60));
        text = buf;
    }
    if (g.deployCancelRequested) {
        text = L"取り消しています…（" + text + L"）";
        color = kNg;
    }
    if (text != g.deployShown) {
        g.deployShown = text;
        SetSummaryText(text, color);
    }
}

void FinishDeploy(JobResult* res) {
    KillTimer(g.hwnd, kTimerDeploy);
    // 最後の状況を読んで、作業ごとの行は結果として残す（次の準備か、手で点検するまで）
    if (FileExists(g.deployStatus)) ReadDeployStatus();
    Layout();
    SetThreadExecutionState(ES_CONTINUOUS);
    g.deploying = false;
    g.deployCancelRequested = false;
    SetWindowTextW(g.runAt, L"時間を指定して実行");
    // 入れた・消した結果に合わせて、トグルを実際の状態にそろえる（続く点検の判定もこれに従う）
    RefreshInstalled();
    g.toggleOn[0] = g.installed[0];
    g.toggleOn[1] = g.installed[1];
    for (int i = 0; i < 2; ++i) InvalidateRect(g.tog[i], nullptr, FALSE);
    static const ElevatedText t = {
        L"準備を取りやめました（管理者の確認で「いいえ」が押されました）。何もしていません。",
        L"準備を始められませんでした（エラー %lu）。",
        L"準備の結果を受け取れませんでした。",
        L"準備しました",
        L"済ませた",
        L"うまくいかなかったものがあります。",
    };
    static const ElevatedText t1 = {
        L"まとめて実行を取りやめました（管理者の確認で「いいえ」が押されました）。何もしていません。",
        L"まとめて実行を始められませんでした（エラー %lu）。",
        L"まとめて実行の結果を受け取れませんでした。",
        L"まとめて実行しました",
        L"済ませた",
        L"うまくいかなかったものがあります。",
    };
    const bool wasOneClick = g.oneClick;
    g.oneClick = false;
    FinishElevated(res, g.deployTsv, g.deployLog, wasOneClick ? t1 : t);
}

// ---------------------------------------------------------------- このアプリ自身の更新（目録 apps.json に載せた新しい版）

std::wstring InstallTarget();
bool IsSameOrUnder(const std::wstring& path, const std::wstring& base);

// 起動時：Program Files に入っているときだけ、目録に新しい版があるかを裏で確かめる
void StartUpdateCheck() {
    if (g.running) return;
    if (!IsSameOrUnder(g.exeDir, InstallTarget())) return;  // 入れていない（開発・試験）ときは確かめない
    std::wstring profile = SelectedProfilePath();
    if (profile.empty() || ReadJsonString(profile, L"ownAppsManifest").empty()) return;
    std::wstring script = g.exeDir + L"\\scripts\\selfupdate_check.ps1";
    if (!FileExists(script)) return;
    EnsureLogDir();
    g.updCheckOut = g.logDir + L"\\更新の確認_" + Timestamp() + L".txt";
    Job* job = new Job();
    job->cmd = L"\"" + PowerShellPath() + L"\" -NoProfile -NonInteractive -ExecutionPolicy Bypass -File \"" + script +
               L"\" -ProfilePath \"" + profile + L"\" -CurrentVersion " + kVersion + L" -OutFile \"" + g.updCheckOut + L"\"";
    job->outLog = g.updCheckOut + L".log";
    job->hwnd = g.hwnd;
    job->doneMsg = WM_APP_UPDCHECK;
    HANDLE th = CreateThread(nullptr, 0, JobThread, job, 0, nullptr);
    if (th) CloseHandle(th);
    else delete job;
}

struct UpdatePrompt {
    std::wstring version;
    int seconds = 30;
};
const int kBtnUpdateNow = 5001, kBtnUpdateSkip = 5002;

HRESULT CALLBACK UpdatePromptCallback(HWND h, UINT note, WPARAM wp, LPARAM, LONG_PTR ref) {
    if (note == TDN_TIMER) {
        const UpdatePrompt* u = reinterpret_cast<const UpdatePrompt*>(ref);
        int left = u->seconds - static_cast<int>(wp / 1000);
        if (left <= 0) {
            SendMessageW(h, TDM_CLICK_BUTTON, kBtnUpdateNow, 0);
            return S_OK;
        }
        std::wstring t = L"新しい版 v" + u->version + L" があります（今は v" + kVersion + L"）。\n" + std::to_wstring(left) +
                         L" 秒後に自動で更新して、起動し直します。";
        SendMessageW(h, TDM_UPDATE_ELEMENT_TEXT, TDE_CONTENT, reinterpret_cast<LPARAM>(t.c_str()));
    }
    return S_OK;
}

void StartSelfUpdate(const std::wstring& version, const std::wstring& url, const std::wstring& sha) {
    // 入れ替える scripts の中から動かないよう、更新の部品を一時フォルダーに写して使う
    wchar_t tmp[MAX_PATH];
    GetTempPathW(MAX_PATH, tmp);
    std::wstring dir = std::wstring(tmp) + L"ManabitoClassSetup";
    SHCreateDirectoryExW(nullptr, dir.c_str(), nullptr);
    std::wstring script = dir + L"\\selfupdate_" + Timestamp() + L".ps1";
    if (!CopyFileW((g.exeDir + L"\\scripts\\selfupdate.ps1").c_str(), script.c_str(), FALSE)) {
        MessageBoxW(g.hwnd, L"更新の部品を用意できませんでした。今回は更新しません。", kAppName, MB_OK | MB_ICONWARNING);
        return;
    }
    EnsureLogDir();
    g.updStatus = AppDataDir() + L"\\update_status.txt";
    DeleteFileW(g.updStatus.c_str());
    std::wstring result = AppDataDir() + L"\\update_result.txt";
    std::wstring log = g.logDir + L"\\更新_" + Timestamp() + L"_PowerShell.txt";
    wchar_t pid[16];
    wsprintfW(pid, L"%lu", static_cast<unsigned long>(GetCurrentProcessId()));
    std::wstring params = L"-NoProfile -NonInteractive -ExecutionPolicy Bypass -WindowStyle Hidden -File \"" + script + L"\" -Url \"" + url +
                          L"\" -Sha256 " + sha + L" -Version " + version + L" -Target \"" + InstallTarget() + L"\" -ParentPid " + pid +
                          L" -StatusFile \"" + g.updStatus + L"\" -ResultFile \"" + result + L"\" -LogFile \"" + log + L"\"";
    std::wstring ps = PowerShellPath();
    SHELLEXECUTEINFOW sei{};
    sei.cbSize = sizeof(sei);
    sei.fMask = SEE_MASK_NOASYNC;
    sei.hwnd = g.hwnd;
    sei.lpVerb = L"runas";
    sei.lpFile = ps.c_str();
    sei.lpParameters = params.c_str();
    sei.nShow = SW_HIDE;
    if (!ShellExecuteExW(&sei)) {
        SetSummaryText(L"更新を取りやめました（管理者の確認で「いいえ」が押されました）。今の版のまま使えます。", kMuted);
        return;
    }
    g.updating = true;
    g.updVersion = version;
    g.updStartTick = GetTickCount();
    SetBusy(true);
    SetSummaryText(L"新しい版 v" + version + L" をダウンロードして確かめています…（終わると自動で起動し直します）", kAccent);
    SetTimer(g.hwnd, kTimerUpdate, 500, nullptr);
}

void OnUpdateTimer() {
    std::string bytes;
    std::wstring st;
    if (ReadAllBytes(g.updStatus, bytes)) st = DecodeText(bytes);
    while (!st.empty() && (st.back() == L'\r' || st.back() == L'\n')) st.pop_back();
    if (st == L"ready") {
        // 確かめ終わった：閉じる（入れ替えと起動し直しは、更新の部品が行う）。閉じるときの履歴の削除は聞かない
        KillTimer(g.hwnd, kTimerUpdate);
        g.updating = false;
        g.running = false;
        DestroyWindow(g.hwnd);
        return;
    }
    if (st.rfind(L"fail|", 0) == 0 || GetTickCount() - g.updStartTick > 600000) {
        KillTimer(g.hwnd, kTimerUpdate);
        g.updating = false;
        SetBusy(false);
        std::wstring why = st.size() > 5 ? st.substr(5) : std::wstring(L"10 分たっても終わりませんでした。");
        SetSummaryText(L"新しい版に更新できませんでした。今の版のまま使えます。", kNg);
        MessageBoxW(g.hwnd, (L"新しい版 v" + g.updVersion + L" に更新できませんでした。今の版のまま使えます。\n\n" + why).c_str(), kAppName,
                    MB_OK | MB_ICONWARNING);
    }
}

void FinishUpdateCheck(JobResult* res) {
    (void)res;
    std::string bytes;
    if (g.updCheckOut.empty() || !ReadAllBytes(g.updCheckOut, bytes)) return;
    std::wstring t = DecodeText(bytes);
    while (!t.empty() && (t.back() == L'\r' || t.back() == L'\n')) t.pop_back();
    if (t.rfind(L"newer|", 0) != 0 || g.running) return;  // 新しい版なし・確かめられない・作業中は何もしない
    std::vector<std::wstring> f;
    size_t a = 0;
    for (;;) {
        size_t b = t.find(L'|', a);
        f.push_back(t.substr(a, b == std::wstring::npos ? std::wstring::npos : b - a));
        if (b == std::wstring::npos) break;
        a = b + 1;
    }
    if (f.size() < 4) return;
    UpdatePrompt up;
    up.version = f[1];
    std::wstring content = L"新しい版 v" + up.version + L" があります（今は v" + kVersion + L"）。\n30 秒後に自動で更新して、起動し直します。";
    TASKDIALOG_BUTTON buttons[] = {
        {kBtnUpdateNow, L"今すぐ更新して起動し直す\n管理者の確認が出たら「はい」を押してください。"},
        {kBtnUpdateSkip, L"今回は更新しない"},
    };
    TASKDIALOGCONFIG c{};
    c.cbSize = sizeof(c);
    c.hwndParent = g.hwnd;
    c.hInstance = g.inst;
    c.dwFlags = TDF_USE_COMMAND_LINKS | TDF_ALLOW_DIALOG_CANCELLATION | TDF_CALLBACK_TIMER | TDF_POSITION_RELATIVE_TO_WINDOW;
    c.pszWindowTitle = kAppName;
    c.pszMainIcon = TD_INFORMATION_ICON;
    c.pszMainInstruction = L"まなびと講習準備の新しい版があります";
    c.pszContent = content.c_str();
    c.cButtons = 2;
    c.pButtons = buttons;
    c.nDefaultButton = kBtnUpdateNow;
    c.pfCallback = UpdatePromptCallback;
    c.lpCallbackData = reinterpret_cast<LONG_PTR>(&up);
    int button = 0;
    if (FAILED(TaskDialogIndirect(&c, &button, nullptr, nullptr)))
        button = (MessageBoxW(g.hwnd, (content + L"\n\n更新しますか？").c_str(), kAppName, MB_YESNO | MB_ICONINFORMATION) == IDYES)
                     ? kBtnUpdateNow : kBtnUpdateSkip;
    if (button == kBtnUpdateNow) StartSelfUpdate(f[1], f[2], f[3]);
}

// 起動し直したあと：前回の更新の結果を表示する
void ShowUpdateResult() {
    std::wstring path = AppDataDir() + L"\\update_result.txt";
    std::string bytes;
    if (!ReadAllBytes(path, bytes)) return;
    DeleteFileW(path.c_str());
    std::wstring t = DecodeText(bytes);
    while (!t.empty() && (t.back() == L'\r' || t.back() == L'\n')) t.pop_back();
    if (t.rfind(L"OK|", 0) == 0) {
        SetSummaryText(L"新しい版 v" + t.substr(3) + L" に更新しました。", kOk);
    } else if (t.rfind(L"NG|", 0) == 0) {
        MessageBoxW(g.hwnd, (L"新しい版に更新できませんでした。前の版のまま起動しています。\n\n" + t.substr(3)).c_str(), kAppName,
                    MB_OK | MB_ICONWARNING);
    }
}

// ---------------------------------------------------------------- スマート アプリ コントロール

void StartSacOff() {
    if (g.running) return;
    std::wstring script = g.exeDir + L"\\scripts\\sac_off.ps1";
    if (!FileExists(script)) {
        MessageBoxW(g.hwnd, (L"部品が見つかりません。\n\n" + script).c_str(), kAppName, MB_OK | MB_ICONWARNING);
        return;
    }
    EnsureLogDir();
    std::wstring base = g.logDir + L"\\スマートアプリコントロール_" + Timestamp();
    g.sacTsv = base + L".tsv";
    g.sacLog = base + L"_PowerShell.txt";
    std::wstring params = L"-NoProfile -NonInteractive -ExecutionPolicy Bypass -WindowStyle Hidden -File \"" + script +
                          L"\" -OutFile \"" + g.sacTsv + L"\" -LogFile \"" + g.sacLog + L"\"";
    ApplyJob* job = new ApplyJob();
    job->exe = PowerShellPath();
    job->params = params;
    job->hwnd = g.hwnd;
    job->kind = kJobSacOff;
    HANDLE th = CreateThread(nullptr, 0, ApplyThread, job, 0, nullptr);
    if (!th) {
        delete job;
        return;
    }
    CloseHandle(th);
    g.applying = true;
    SetBusy(true);
    SetSummaryText(L"スマート アプリ コントロールをオフにしています…（管理者の確認が出たら「はい」を押してください）", kAccent);
}

void OpenWindowsSecurity() {
    // 「Windows セキュリティ」→「アプリとブラウザーの制御」
    if (reinterpret_cast<INT_PTR>(ShellExecuteW(g.hwnd, L"open", L"windowsdefender://appbrowser", nullptr, nullptr,
                                                SW_SHOWNORMAL)) <= 32) {
        ShellExecuteW(g.hwnd, L"open", L"windowsdefender:", nullptr, nullptr, SW_SHOWNORMAL);
    }
}

void FinishSacOff(JobResult* res) {
    g.applying = false;
    SetBusy(false);
    if (!res->started) {
        if (res->error == ERROR_CANCELLED) {
            SetSummaryText(L"スマート アプリ コントロールはそのままです（管理者の確認で「いいえ」が押されました）。", kMuted);
        } else {
            wchar_t buf[200];
            wsprintfW(buf, L"オフにする作業を始められませんでした（エラー %lu）。", static_cast<unsigned long>(res->error));
            SetSummaryText(buf, kNg);
        }
        return;
    }
    g.lastDetailFile = g.sacLog;
    int now = ReadSacState();
    if (now == 0) {
        SetSummaryText(L"スマート アプリ コントロールをオフにしました。PC を再起動してください。", kOk);
        MessageBoxW(g.hwnd,
                    L"スマート アプリ コントロールをオフにしました。\n\n"
                    L"確実に反映させるため、作業が一段落したら PC を再起動してください。",
                    kAppName, MB_OK | MB_ICONINFORMATION);
        return;
    }
    SetSummaryText(L"自動ではオフにできませんでした。「Windows セキュリティ」の画面でオフにしてください。", kNg);
    MessageBoxW(g.hwnd,
                L"自動ではオフにできませんでした。\n\n"
                L"このあと「Windows セキュリティ」の画面を開きます。\n"
                L"「スマート アプリ コントロールの設定」を開き、「オフ」を選んでください。",
                kAppName, MB_OK | MB_ICONWARNING);
    OpenWindowsSecurity();
}

// 起動時の案内。オフでないときに出す（「次回から表示しない」を選んだら出さない）。
void CheckSacOnStartup() {
    int st = ReadSacState();
    if (st != 1 && st != 2) return;  // オフ、または機能が無い
    if (ReadSettingFlag(L"sacPromptDismissed")) return;

    const wchar_t* heading = (st == 2) ? L"スマート アプリ コントロールが「評価中」です"
                                    : L"スマート アプリ コントロールが「オン」です";
    std::wstring content =
        L"この機能は、電子署名のないアプリの起動を止めます。まなびとPDFツール・ほんよみ・このアプリなどの独自アプリが"
        L"これに当たります。\n\n";
    if (st == 2) {
        content += L"「評価中」の PC は、ある日 Windows が自動で「オン」に切り替え、独自アプリが起動しなくなることがあります。"
                   L"\n\n講習で使う PC では、オフにすることをおすすめします。";
    } else {
        content += L"このままでは、独自アプリが起動しなくなる可能性があります。\n\n講習で使う PC では、オフにすることをおすすめします。";
    }
    TASKDIALOG_BUTTON buttons[] = {
        {kBtnSacOff, L"オフにする（おすすめ）\n管理者の確認が出ます。終わったら PC を再起動してください。"},
        {kBtnSacKeep, L"今はオフにしない\n独自アプリが起動しなくなる可能性があります。"},
    };
    TASKDIALOGCONFIG c{};
    c.cbSize = sizeof(c);
    c.hwndParent = g.hwnd;
    c.hInstance = g.inst;
    c.dwFlags = TDF_USE_COMMAND_LINKS | TDF_ALLOW_DIALOG_CANCELLATION | TDF_POSITION_RELATIVE_TO_WINDOW;
    c.pszWindowTitle = kAppName;
    c.pszMainIcon = TD_WARNING_ICON;
    c.pszMainInstruction = heading;
    c.pszContent = content.c_str();
    c.cButtons = 2;
    c.pButtons = buttons;
    c.nDefaultButton = kBtnSacOff;
    c.pszVerificationText = L"次回から、この案内を表示しない";
    c.pszFooterIcon = TD_INFORMATION_ICON;
    c.pszFooter =
        L"注意：一度オフにすると、Windows の版によっては PC を初期化するまでオンに戻せません。"
        L"ウイルス対策（Microsoft Defender）と SmartScreen は、オフにした後も働きます。";
    int button = 0;
    BOOL dontAsk = FALSE;
    if (FAILED(TaskDialogIndirect(&c, &button, nullptr, &dontAsk))) {
        std::wstring m = std::wstring(heading) + L"\n\n" + content + L"\n\nオフにしますか？\n（" + c.pszFooter + L"）";
        button = (MessageBoxW(g.hwnd, m.c_str(), kAppName, MB_YESNO | MB_ICONWARNING) == IDYES) ? kBtnSacOff : kBtnSacKeep;
    }
    if (dontAsk) WriteSettingFlag(L"sacPromptDismissed", true);
    if (button == kBtnSacOff) StartSacOff();
}

void OpenLogFolder() {
    EnsureLogDir();
    if (!g.lastDetailFile.empty() && FileExists(g.lastDetailFile)) {
        std::wstring args = L"/select,\"" + g.lastDetailFile + L"\"";
        ShellExecuteW(g.hwnd, L"open", L"explorer.exe", args.c_str(), nullptr, SW_SHOWNORMAL);
    } else {
        ShellExecuteW(g.hwnd, L"open", g.logDir.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
    }
}

// ---------------------------------------------------------------- 見た目

void CreateFonts() {
    HFONT old[] = {g.font, g.fontBold, g.fontTitle, g.fontSmall};
    NONCLIENTMETRICSW ncm{};
    ncm.cbSize = sizeof(ncm);
    BOOL ok = FALSE;
    if (pSystemParametersInfoForDpi) ok = pSystemParametersInfoForDpi(SPI_GETNONCLIENTMETRICS, sizeof(ncm), &ncm, 0, g.dpi);
    if (!ok) SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof(ncm), &ncm, 0);
    LOGFONTW lf = ncm.lfMessageFont;
    lf.lfQuality = CLEARTYPE_QUALITY;
    lf.lfWeight = FW_NORMAL;
    lf.lfHeight = -MulDiv(105, static_cast<int>(g.dpi), 720);  // 10.5pt（事務員さんにも読みやすい大きさ）
    g.font = CreateFontIndirectW(&lf);
    lf.lfWeight = FW_BOLD;
    g.fontBold = CreateFontIndirectW(&lf);
    lf.lfHeight = -MulDiv(160, static_cast<int>(g.dpi), 720);
    g.fontTitle = CreateFontIndirectW(&lf);
    lf.lfWeight = FW_NORMAL;
    lf.lfHeight = -MulDiv(90, static_cast<int>(g.dpi), 720);
    g.fontSmall = CreateFontIndirectW(&lf);
    for (HFONT f : old)
        if (f) DeleteObject(f);
}

void ApplyFonts() {
    for (HWND h : {g.combo, g.list, g.detail, g.timePick})
        if (h) SendMessageW(h, WM_SETFONT, reinterpret_cast<WPARAM>(g.font), TRUE);
    SendMessageW(g.detail, EM_SETMARGINS, EC_LEFTMARGIN | EC_RIGHTMARGIN, MAKELPARAM(S(8), S(8)));
}

void Layout() {
    RECT rc;
    GetClientRect(g.hwnd, &rc);
    const int W = rc.right, H = rc.bottom;
    const int m = S(20);
    g.ctrlTop = S(62);
    g.ctrlHeight = S(38);
    MoveWindow(g.share, W - m - S(190), S(14), S(190), S(34), TRUE);  // 右上：配布サーバーになる
    int x = m + S(132);
    RECT cr;
    MoveWindow(g.combo, x, g.ctrlTop, S(190), S(300), TRUE);
    GetWindowRect(g.combo, &cr);
    int comboH = cr.bottom - cr.top;
    MoveWindow(g.combo, x, g.ctrlTop + (g.ctrlHeight - comboH) / 2, S(190), S(300), TRUE);
    x += S(190) + S(18);
    MoveWindow(g.oneclick, x, g.ctrlTop, S(170), g.ctrlHeight, TRUE);
    x += S(170) + S(14);
    MoveWindow(g.run, x, g.ctrlTop, S(110), g.ctrlHeight, TRUE);
    x += S(110) + S(10);
    MoveWindow(g.apply, x, g.ctrlTop, S(150), g.ctrlHeight, TRUE);
    x += S(150) + S(10);
    MoveWindow(g.clean, x, g.ctrlTop, S(120), g.ctrlHeight, TRUE);
    x += S(120) + S(10);
    MoveWindow(g.logs, x, g.ctrlTop, S(140), g.ctrlHeight, TRUE);

    // 2 段目：時間のかかる準備
    g.row2Top = g.ctrlTop + g.ctrlHeight + S(12);
    x = m + S(132);
    for (int i = 0; i < 3; ++i) {
        const int w = S(i == 0 ? 150 : i == 1 ? 168 : 132);
        MoveWindow(g.tog[i], x, g.row2Top, w, g.ctrlHeight, TRUE);
        x += w + S(8);
    }
    x += S(10);
    MoveWindow(g.runNow, x, g.row2Top, S(120), g.ctrlHeight, TRUE);
    x += S(120) + S(10);
    MoveWindow(g.runAt, x, g.row2Top, S(170), g.ctrlHeight, TRUE);
    x += S(170) + S(8);
    MoveWindow(g.timePick, x, g.row2Top + (g.ctrlHeight - comboH) / 2, S(84), comboH, TRUE);

    g.summaryTop = g.row2Top + g.ctrlHeight + S(14);
    // 作業ごとの行（準備のときだけ）：1 件 2 段（名前と手順・進み具合のバー／説明）
    g.progressTop = g.summaryTop + S(30);
    g.progressHeight = g.progress.empty() ? 0 : static_cast<int>(g.progress.size()) * S(46) + S(10);
    const int listTop = g.progressTop + g.progressHeight + (g.progressHeight ? S(8) : S(2));
    const int detailH = S(104);
    const int detailTop = std::max(listTop + S(120), H - m - detailH);
    const int listBottom = detailTop - S(30);
    g.detailLabelTop = listBottom + S(6);
    MoveWindow(g.list, m, listTop, std::max(S(200), W - 2 * m), std::max(S(80), listBottom - listTop), TRUE);
    MoveWindow(g.detail, m, detailTop, std::max(S(200), W - 2 * m), detailH, TRUE);

    RECT lr;
    GetClientRect(g.list, &lr);
    int avail = std::max(S(400), static_cast<int>(lr.right) - S(4));
    int c0 = avail * 30 / 100, c1 = avail * 36 / 100, c2 = avail * 20 / 100;
    int c3 = avail - c0 - c1 - c2;
    ListView_SetColumnWidth(g.list, 0, c0);
    ListView_SetColumnWidth(g.list, 1, c1);
    ListView_SetColumnWidth(g.list, 2, c2);
    ListView_SetColumnWidth(g.list, 3, c3);
    UpdateDetailScroll();
    InvalidateRect(g.hwnd, nullptr, FALSE);
}

void DrawTextAt(HDC dc, const std::wstring& t, int x, int y, HFONT f, COLORREF c, int* widthOut = nullptr) {
    HGDIOBJ old = SelectObject(dc, f);
    SetTextColor(dc, c);
    TextOutW(dc, x, y, t.c_str(), static_cast<int>(t.size()));
    if (widthOut) {
        SIZE sz{};
        GetTextExtentPoint32W(dc, t.c_str(), static_cast<int>(t.size()), &sz);
        *widthOut = sz.cx;
    }
    SelectObject(dc, old);
}

int TextHeight(HDC dc, HFONT f) {
    HGDIOBJ old = SelectObject(dc, f);
    TEXTMETRICW tm{};
    GetTextMetricsW(dc, &tm);
    SelectObject(dc, old);
    return tm.tmHeight;
}

void DrawTextIn(HDC dc, const std::wstring& t, RECT r, HFONT f, COLORREF c, UINT extra = 0) {
    HGDIOBJ old = SelectObject(dc, f);
    SetTextColor(dc, c);
    DrawTextW(dc, t.c_str(), -1, &r, DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX | DT_END_ELLIPSIS | extra);
    SelectObject(dc, old);
}

void AddRoundRect(Gdiplus::GraphicsPath& p, float x, float y, float w, float h, float r);
Gdiplus::Color ToColor(COLORREF c);

// 作業ごとの行：1 段目「名前・状態・手順・進み具合のバー」、2 段目「説明（速さ・残り時間など）」
void PaintProgress(HDC dc, int width) {
    if (g.progress.empty()) return;
    const int m = S(20);
    const int left = m, right = width - m;
    {
        Gdiplus::Graphics gr(dc);
        gr.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
        Gdiplus::GraphicsPath box;
        AddRoundRect(box, static_cast<float>(left) + 0.5f, static_cast<float>(g.progressTop) + 0.5f,
                     static_cast<float>(right - left) - 1.0f, static_cast<float>(g.progressHeight) - 1.0f, static_cast<float>(S(8)));
        Gdiplus::SolidBrush white(ToColor(kWhite));
        gr.FillPath(&white, &box);
        Gdiplus::Pen pen(ToColor(kLine), 1.0f);
        gr.DrawPath(&pen, &box);
    }
    SetBkMode(dc, TRANSPARENT);
    int y = g.progressTop + S(5);
    const int barW = S(170), barH = S(10);
    for (const auto& it : g.progress) {
        COLORREF sc = kMuted;
        const wchar_t* label = L"待機";
        if (it.state == L"run") { sc = kAccent; label = L"作業中"; }
        else if (it.state == L"done") { sc = kOk; label = L"完了"; }
        else if (it.state == L"same") { sc = kOk; label = L"そのまま"; }
        else if (it.state == L"fail") { sc = kNg; label = L"失敗"; }
        else if (it.state == L"skip") { sc = kNa; label = L"見送り"; }
        const int line1 = S(24), line2 = S(18);
        // 1 段目
        RECT rn{left + S(12), y, left + S(300), y + line1};
        DrawTextIn(dc, it.name, rn, g.fontBold, kInk);
        RECT rs{left + S(304), y, left + S(372), y + line1};
        DrawTextIn(dc, label, rs, g.fontBold, sc);
        std::wstring step;
        if (it.steps > 0 && it.state == L"run")
            step = L"手順 " + std::to_wstring(it.step) + L"/" + std::to_wstring(it.steps) + L"　" + it.stepName;
        RECT rp{left + S(376), y, right - barW - S(70), y + line1};
        DrawTextIn(dc, step, rp, g.font, kInk);
        // 進み具合のバー
        const int bx = right - barW - S(62), by = y + (line1 - barH) / 2;
        {
            Gdiplus::Graphics gr(dc);
            gr.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
            Gdiplus::GraphicsPath track;
            AddRoundRect(track, static_cast<float>(bx), static_cast<float>(by), static_cast<float>(barW), static_cast<float>(barH),
                         static_cast<float>(barH) / 2.0f);
            Gdiplus::SolidBrush tb(ToColor(RGB(0xEE, 0xE8, 0xDC)));
            gr.FillPath(&tb, &track);
            int pct = it.percent;
            if (it.state == L"done" || it.state == L"same") pct = 100;
            if (pct > 0) {
                float w = static_cast<float>(barW) * static_cast<float>(std::min(pct, 100)) / 100.0f;
                if (w < static_cast<float>(barH)) w = static_cast<float>(barH);
                Gdiplus::GraphicsPath fill;
                AddRoundRect(fill, static_cast<float>(bx), static_cast<float>(by), w, static_cast<float>(barH), static_cast<float>(barH) / 2.0f);
                Gdiplus::SolidBrush fb(ToColor(it.state == L"fail" ? kNg : (it.state == L"run" ? kAccent : kOk)));
                gr.FillPath(&fb, &fill);
            }
        }
        std::wstring pctText = (it.percent >= 0 || it.state == L"done" || it.state == L"same")
                                   ? std::to_wstring((it.state == L"done" || it.state == L"same") ? 100 : it.percent) + L"%"
                                   : (it.state == L"run" ? L"―" : L"");
        RECT rpct{right - S(56), y, right - S(10), y + line1};
        DrawTextIn(dc, pctText, rpct, g.font, kInk, DT_RIGHT);
        // 2 段目
        RECT rt{left + S(28), y + line1, right - S(10), y + line1 + line2};
        DrawTextIn(dc, it.text, rt, g.fontSmall, it.state == L"fail" ? kNg : kMuted);
        y += S(46);
    }
}

void Paint(HDC target) {
    RECT rc;
    GetClientRect(g.hwnd, &rc);
    HDC dc = CreateCompatibleDC(target);
    HBITMAP bmp = CreateCompatibleBitmap(target, rc.right, rc.bottom);
    HGDIOBJ oldBmp = SelectObject(dc, bmp);
    HBRUSH paper = CreateSolidBrush(kPaper);
    FillRect(dc, &rc, paper);
    DeleteObject(paper);
    RECT bar{0, 0, rc.right, S(4)};
    HBRUSH accent = CreateSolidBrush(kAccent);
    FillRect(dc, &bar, accent);
    DeleteObject(accent);
    SetBkMode(dc, TRANSPARENT);

    const int m = S(20);
    int w = 0;
    DrawTextAt(dc, kAppName, m, S(16), g.fontTitle, kInk, &w);
    std::wstring ver = std::wstring(L"v") + kVersion;
    int th = TextHeight(dc, g.fontTitle), sh = TextHeight(dc, g.fontSmall);
    DrawTextAt(dc, ver, m + w + S(14), S(16) + th - sh - S(2), g.fontSmall, kMuted);

    int lh = TextHeight(dc, g.font);
    DrawTextAt(dc, L"講習プロファイル", m, g.ctrlTop + (g.ctrlHeight - lh) / 2, g.font, kInk);
    DrawTextAt(dc, L"時間のかかる準備", m, g.row2Top + (g.ctrlHeight - lh) / 2, g.font, kInk);

    int x = m;
    for (const auto& seg : g.summary) {
        int sw = 0;
        DrawTextAt(dc, seg.text, x, g.summaryTop, g.fontBold, seg.color, &sw);
        x += sw;
    }
    DrawTextAt(dc, L"説明", m, g.detailLabelTop, g.font, kMuted);
    PaintProgress(dc, rc.right);

    BitBlt(target, 0, 0, rc.right, rc.bottom, dc, 0, 0, SRCCOPY);
    SelectObject(dc, oldBmp);
    DeleteObject(bmp);
    DeleteDC(dc);
}

void AddRoundRect(Gdiplus::GraphicsPath& p, float x, float y, float w, float h, float r) {
    float d = r * 2;
    p.AddArc(x, y, d, d, 180, 90);
    p.AddArc(x + w - d, y, d, d, 270, 90);
    p.AddArc(x + w - d, y + h - d, d, d, 0, 90);
    p.AddArc(x, y + h - d, d, d, 90, 90);
    p.CloseFigure();
}

Gdiplus::Color ToColor(COLORREF c) { return Gdiplus::Color(255, GetRValue(c), GetGValue(c), GetBValue(c)); }

void DrawToggle(const DRAWITEMSTRUCT* d, int i) {
    const bool disabled = (d->itemState & ODS_DISABLED) != 0;
    const bool unavailable = (i == 2 && !g.appsAvailable);
    const bool on = g.toggleOn[i] && !unavailable;
    HDC dc = d->hDC;
    RECT r = d->rcItem;
    HBRUSH paper = CreateSolidBrush(kPaper);
    FillRect(dc, &r, paper);
    DeleteObject(paper);
    const float h = static_cast<float>(r.bottom - r.top);
    const float sw = static_cast<float>(S(34)), sh = static_cast<float>(S(18));
    const float sx = static_cast<float>(r.left + S(10)), sy = static_cast<float>(r.top) + (h - sh) / 2.0f;
    {
        Gdiplus::Graphics gr(dc);
        gr.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
        Gdiplus::GraphicsPath box;
        AddRoundRect(box, static_cast<float>(r.left) + 0.5f, static_cast<float>(r.top) + 0.5f,
                     static_cast<float>(r.right - r.left) - 1.0f, h - 1.0f, static_cast<float>(S(8)));
        Gdiplus::SolidBrush white(ToColor(kWhite));
        gr.FillPath(&white, &box);
        Gdiplus::Pen border(ToColor(disabled ? kLine : on ? kAccent : WillRemove(i) ? kNg : kLine), 1.0f);
        gr.DrawPath(&border, &box);
        Gdiplus::GraphicsPath pill;
        AddRoundRect(pill, sx, sy, sw, sh, sh / 2.0f);
        COLORREF track = on ? (disabled ? kDisabled : kAccent) : RGB(0xD6, 0xCF, 0xC3);
        Gdiplus::SolidBrush trackBrush(ToColor(track));
        gr.FillPath(&trackBrush, &pill);
        const float k = sh - 4.0f;
        const float kx = on ? sx + sw - k - 2.0f : sx + 2.0f;
        Gdiplus::SolidBrush knob(ToColor(kWhite));
        gr.FillEllipse(&knob, kx, sy + 2.0f, k, k);
    }
    SetBkMode(dc, TRANSPARENT);
    const bool removal = WillRemove(i);
    SetTextColor(dc, (disabled || unavailable) ? kDisabled : removal ? kNg : kInk);
    HGDIOBJ old = SelectObject(dc, (on || removal) ? g.fontBold : g.font);
    RECT tr = r;
    tr.left = r.left + S(10) + S(34) + S(8);
    std::wstring label = kToggleNames[i];
    if (removal) label += L" → 削除";
    DrawTextW(dc, label.c_str(), -1, &tr, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    SelectObject(dc, old);
    if ((d->itemState & ODS_FOCUS) && !(d->itemState & ODS_NOFOCUSRECT)) {
        RECT f = r;
        InflateRect(&f, -S(3), -S(3));
        DrawFocusRect(dc, &f);
    }
}

void DrawButton(const DRAWITEMSTRUCT* d) {
    if (d->CtlID >= IDC_TOG_OFFICE && d->CtlID <= IDC_TOG_APPS) {
        DrawToggle(d, static_cast<int>(d->CtlID - IDC_TOG_OFFICE));
        return;
    }
    const bool primary = (d->CtlID == IDC_ONECLICK);
    const bool outline = (d->CtlID == IDC_RUN || d->CtlID == IDC_SHARE || d->CtlID == IDC_APPLY || d->CtlID == IDC_CLEAN || d->CtlID == IDC_RUN_NOW ||
                          d->CtlID == IDC_RUN_AT);
    const COLORREF outlineColor = (d->CtlID == IDC_CLEAN || (d->CtlID == IDC_RUN_AT && g.deploying)) ? kNg : kAccent;
    const bool disabled = (d->itemState & ODS_DISABLED) != 0;
    const bool pressed = (d->itemState & ODS_SELECTED) != 0;
    HDC dc = d->hDC;
    RECT r = d->rcItem;
    HBRUSH paper = CreateSolidBrush(kPaper);
    FillRect(dc, &r, paper);
    DeleteObject(paper);

    COLORREF fill, border, text;
    if (primary) {
        fill = disabled ? kDisabled : (pressed ? kAccentDark : kAccent);
        border = fill;
        text = kWhite;
    } else if (outline) {
        fill = pressed ? RGB(0xF2, 0xEC, 0xE6) : kWhite;
        border = disabled ? kDisabled : outlineColor;
        text = disabled ? kDisabled : outlineColor;
    } else {
        fill = pressed ? RGB(0xEE, 0xE8, 0xDC) : kWhite;
        border = kLine;
        text = disabled ? kDisabled : kInk;
    }
    {
        Gdiplus::Graphics gr(dc);
        gr.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
        Gdiplus::GraphicsPath path;
        AddRoundRect(path, static_cast<float>(r.left) + 0.5f, static_cast<float>(r.top) + 0.5f,
                     static_cast<float>(r.right - r.left) - 1.0f, static_cast<float>(r.bottom - r.top) - 1.0f,
                     static_cast<float>(S(8)));
        Gdiplus::SolidBrush brush(ToColor(fill));
        gr.FillPath(&brush, &path);
        Gdiplus::Pen pen(ToColor(border), outline ? 1.5f * static_cast<float>(g.dpi) / 96.0f : 1.0f);
        gr.DrawPath(&pen, &path);
    }
    wchar_t label[64];
    GetWindowTextW(d->hwndItem, label, 64);
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, text);
    HGDIOBJ old = SelectObject(dc, (primary || outline) ? g.fontBold : g.font);
    DrawTextW(dc, label, -1, &r, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    SelectObject(dc, old);
    if ((d->itemState & ODS_FOCUS) && !(d->itemState & ODS_NOFOCUSRECT)) {
        RECT f = r;
        InflateRect(&f, -S(4), -S(4));
        DrawFocusRect(dc, &f);
    }
}

LRESULT ListCustomDraw(NMLVCUSTOMDRAW* cd) {
    switch (cd->nmcd.dwDrawStage) {
        case CDDS_PREPAINT:
            return CDRF_NOTIFYITEMDRAW;
        case CDDS_ITEMPREPAINT:
            return CDRF_NOTIFYSUBITEMDRAW;
        case CDDS_ITEMPREPAINT | CDDS_SUBITEM: {
            size_t i = static_cast<size_t>(cd->nmcd.lItemlParam);
            if (i >= g.rows.size()) return CDRF_DODEFAULT;
            const Row& r = g.rows[i];
            cd->clrTextBk = (r.state == L"NG")     ? kNgBack
                            : (r.state == L"PART") ? kPartBack
                            : (r.state == L"ERR")  ? kErrBack
                                                   : kWhite;
            if (cd->iSubItem == 3) {
                cd->clrText = StateColor(r.state);
                SelectObject(cd->nmcd.hdc, g.fontBold);
            } else {
                cd->clrText = (r.state == L"NA") ? kNa : kInk;
                SelectObject(cd->nmcd.hdc, g.font);
            }
            return CDRF_NEWFONT;
        }
        default:
            return CDRF_DODEFAULT;
    }
}

void CreateControls(HWND hwnd) {
    g.combo = CreateWindowExW(0, WC_COMBOBOXW, L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_VSCROLL | CBS_DROPDOWNLIST,
                              0, 0, 10, 10, hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_PROFILE)), g.inst,
                              nullptr);
    g.run = CreateWindowExW(0, WC_BUTTONW, L"点検する", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW, 0, 0, 10, 10,
                            hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_RUN)), g.inst, nullptr);
    g.share = CreateWindowExW(0, WC_BUTTONW, L"配布サーバーになる", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW, 0, 0, 10,
                              10, hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_SHARE)), g.inst, nullptr);
    g.oneclick = CreateWindowExW(0, WC_BUTTONW, L"まとめて実行", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW, 0, 0, 10,
                                 10, hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_ONECLICK)), g.inst, nullptr);
    g.apply = CreateWindowExW(0, WC_BUTTONW, L"設定をそろえる", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW,
                              0, 0, 10, 10, hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_APPLY)), g.inst,
                              nullptr);
    g.clean = CreateWindowExW(0, WC_BUTTONW, L"履歴を消す", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW, 0, 0, 10,
                              10, hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_CLEAN)), g.inst, nullptr);
    const int togIds[3] = {IDC_TOG_OFFICE, IDC_TOG_ADOBE, IDC_TOG_APPS};
    for (int i = 0; i < 3; ++i) {
        g.tog[i] = CreateWindowExW(0, WC_BUTTONW, kToggleNames[i], WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW, 0, 0,
                                   10, 10, hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(togIds[i])), g.inst, nullptr);
    }
    g.runNow = CreateWindowExW(0, WC_BUTTONW, L"今すぐ実行", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW, 0, 0, 10,
                               10, hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_RUN_NOW)), g.inst, nullptr);
    g.runAt = CreateWindowExW(0, WC_BUTTONW, L"時間を指定して実行", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW, 0,
                              0, 10, 10, hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_RUN_AT)), g.inst, nullptr);
    g.timePick = CreateWindowExW(0, DATETIMEPICK_CLASSW, L"",
                                 WS_CHILD | WS_VISIBLE | WS_TABSTOP | DTS_TIMEFORMAT | DTS_UPDOWN, 0, 0, 10, 10, hwnd,
                                 reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_TIME)), g.inst, nullptr);
    DateTime_SetFormat(g.timePick, L"HH:mm");
    {
        // 前回の時刻（なければ 22:00）
        wchar_t hm[16] = L"";
        GetPrivateProfileStringW(L"deploy", L"time", L"22:00", hm, 16, SettingsPath().c_str());
        int hh = 22, mi = 0;
        if (swscanf_s(hm, L"%d:%d", &hh, &mi) != 2 || hh < 0 || hh > 23 || mi < 0 || mi > 59) {
            hh = 22;
            mi = 0;
        }
        SYSTEMTIME st;
        GetLocalTime(&st);
        st.wHour = static_cast<WORD>(hh);
        st.wMinute = static_cast<WORD>(mi);
        st.wSecond = 0;
        st.wMilliseconds = 0;
        DateTime_SetSystemtime(g.timePick, GDT_VALID, &st);
    }
    g.logs = CreateWindowExW(0, WC_BUTTONW, L"記録のフォルダー", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW, 0, 0,
                             10, 10, hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_LOGS)), g.inst, nullptr);
    g.list = CreateWindowExW(0, WC_LISTVIEWW, L"",
                             WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_BORDER | LVS_REPORT | LVS_SINGLESEL |
                                 LVS_SHOWSELALWAYS,
                             0, 0, 10, 10, hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_LIST)), g.inst,
                             nullptr);
    ListView_SetExtendedListViewStyle(g.list, LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER);
    SetWindowTheme(g.list, L"Explorer", nullptr);
    ListView_EnableGroupView(g.list, TRUE);
    const wchar_t* heads[] = {L"項目", L"今の状態", L"講習用の状態", L"判定"};
    for (int i = 0; i < 4; ++i) {
        LVCOLUMNW col{};
        col.mask = LVCF_TEXT | LVCF_WIDTH | LVCF_SUBITEM;
        col.pszText = const_cast<LPWSTR>(heads[i]);
        col.cx = 100;
        col.iSubItem = i;
        ListView_InsertColumn(g.list, i, &col);
    }
    g.detail = CreateWindowExW(0, WC_EDITW, L"",
                               WS_CHILD | WS_VISIBLE | WS_BORDER | ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL,
                               0, 0, 10, 10, hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_DETAIL)), g.inst,
                               nullptr);
    ApplyFonts();
    ShowDetailFor(-1);
}

// 閉じるときに「作業でできた履歴を削除します」と知らせ、そのまま［履歴を消す］を行う。
// 戻り値：true＝消す作業を始めた（終わったら閉じる）、false＝そのまま閉じてよい。キャンセルなら閉じない。
const int kBtnExitClean = 2001, kBtnExitKeep = 2002;

bool ConfirmCleanOnExit() {
    std::wstring p = SelectedProfilePath();
    // cleanOnExit は true/false（文字列でない）なので、ここで簡易に探す
    std::string bytes;
    bool enabled = false;
    if (!p.empty() && ReadAllBytes(p, bytes)) {
        std::wstring t = DecodeText(bytes);
        size_t k = t.find(L"\"cleanOnExit\"");
        if (k != std::wstring::npos) {
            size_t c = t.find(L':', k);
            size_t v = (c == std::wstring::npos) ? c : t.find_first_not_of(L" \t\r\n", c + 1);
            enabled = (v != std::wstring::npos && t.compare(v, 4, L"true") == 0);
        }
    }
    if (!enabled) return false;

    TASKDIALOG_BUTTON buttons[] = {
        {kBtnExitClean, L"履歴を削除して終了する（講習 PC 用）\n※ 開発用 PC では実行しないでください。管理者の確認が出たら「はい」を押します。"},
        {kBtnExitKeep, L"履歴を削除せずに終了する（開発 PC 用）"},
    };
    TASKDIALOGCONFIG c{};
    c.cbSize = sizeof(c);
    c.hwndParent = g.hwnd;
    c.hInstance = g.inst;
    c.dwFlags = TDF_USE_COMMAND_LINKS | TDF_ALLOW_DIALOG_CANCELLATION | TDF_POSITION_RELATIVE_TO_WINDOW;
    c.pszWindowTitle = kAppName;
    c.pszMainIcon = TD_INFORMATION_ICON;
    c.pszMainInstruction = L"作業でできた履歴を削除します";
    c.pszContent =
        L"最近使ったファイル、ダウンロード、Office・メモ帳・IME の履歴、Edge・Chrome のお気に入り・履歴・Cookie などを消してから閉じます。"
        L"Edge・Chrome は閉じます。このアプリが入っているフォルダーは残します。";
    c.cButtons = 2;
    c.pButtons = buttons;
    c.nDefaultButton = kBtnExitClean;
    c.dwCommonButtons = TDCBF_CANCEL_BUTTON;
    int button = 0;
    if (FAILED(TaskDialogIndirect(&c, &button, nullptr, nullptr))) {
        int r = MessageBoxW(g.hwnd,
                            L"作業でできた履歴を削除してから閉じます。\n\n"
                            L"［はい］履歴を削除して終了する（講習 PC 用。開発用 PC では実行しないでください）\n"
                            L"［いいえ］履歴を削除せずに終了する（開発 PC 用）",
                            kAppName, MB_YESNOCANCEL | MB_ICONINFORMATION);
        button = (r == IDYES) ? kBtnExitClean : (r == IDNO) ? kBtnExitKeep : IDCANCEL;
    }
    if (button == kBtnExitKeep) return false;
    if (button != kBtnExitClean) return true;  // キャンセル：閉じない
    StartClean(true);
    if (!g.running) return false;  // 始められなかったときは、そのまま閉じる
    return true;
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_CREATE:
            g.hwnd = hwnd;
            g.dpi = WindowDpi(hwnd);
            CreateFonts();
            CreateControls(hwnd);
            LoadProfiles();
            ApplyProfileToggles();
            share::Init(g.inst, hwnd, WM_APP_SHARE_FOUND);  // LAN 内の配布サーバーを探し始める
            SetSummaryText(L"［点検する］を押すと、この PC の状態を調べます。設定は何も変えません。", kMuted);
            Layout();
            return 0;
        case WM_SIZE:
            if (g.list) Layout();
            return 0;
        case WM_GETMINMAXINFO: {
            MINMAXINFO* mm = reinterpret_cast<MINMAXINFO*>(lp);
            mm->ptMinTrackSize.x = S(1130);
            mm->ptMinTrackSize.y = S(560);
            return 0;
        }
        case WM_DPICHANGED: {
            g.dpi = HIWORD(wp);
            CreateFonts();
            ApplyFonts();
            const RECT* r = reinterpret_cast<const RECT*>(lp);
            SetWindowPos(hwnd, nullptr, r->left, r->top, r->right - r->left, r->bottom - r->top,
                         SWP_NOZORDER | SWP_NOACTIVATE);
            Layout();
            return 0;
        }
        case WM_ERASEBKGND:
            return 1;
        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC dc = BeginPaint(hwnd, &ps);
            Paint(dc);
            EndPaint(hwnd, &ps);
            return 0;
        }
        case WM_CTLCOLORSTATIC:
            if (reinterpret_cast<HWND>(lp) == g.detail) {
                HDC dc = reinterpret_cast<HDC>(wp);
                SetBkColor(dc, kWhite);
                SetTextColor(dc, kInk);
                return reinterpret_cast<LRESULT>(g.whiteBrush);
            }
            break;
        case WM_DRAWITEM: {
            const DRAWITEMSTRUCT* d = reinterpret_cast<const DRAWITEMSTRUCT*>(lp);
            if (d->CtlType == ODT_BUTTON) {
                DrawButton(d);
                return TRUE;
            }
            break;
        }
        case WM_COMMAND:
            switch (LOWORD(wp)) {
                case IDC_RUN:
                    StartInspection();
                    return 0;
                case IDC_APPLY:
                    StartApply();
                    return 0;
                case IDC_ONECLICK:
                    StartOneClick();
                    return 0;
                case IDC_SHARE:
                    share::OpenServerWindow(hwnd);
                    return 0;
                case IDC_CLEAN:
                    StartClean();
                    return 0;
                case IDC_LOGS:
                    OpenLogFolder();
                    return 0;
                case IDC_TOG_OFFICE:
                case IDC_TOG_ADOBE:
                case IDC_TOG_APPS:
                    ToggleClicked(LOWORD(wp) - IDC_TOG_OFFICE);
                    return 0;
                case IDC_RUN_NOW:
                    StartDeploy(false);
                    return 0;
                case IDC_RUN_AT:
                    if (g.deploying) CancelDeploy();
                    else StartDeploy(true);
                    return 0;
                case IDC_PROFILE:
                    if (HIWORD(wp) == CBN_SELCHANGE) {
                        ApplyProfileToggles();
                        ApplyToggleJudgement();
                        RefreshResultsView();
                    }
                    return 0;
            }
            break;
        case WM_TIMER:
            if (wp == kTimerDeploy) {
                OnDeployTimer();
                return 0;
            }
            if (wp == kTimerUpdate) {
                OnUpdateTimer();
                return 0;
            }
            break;
        case WM_NOTIFY: {
            NMHDR* h = reinterpret_cast<NMHDR*>(lp);
            if (h->hwndFrom == g.list) {
                if (h->code == NM_CUSTOMDRAW) return ListCustomDraw(reinterpret_cast<NMLVCUSTOMDRAW*>(lp));
                if (h->code == LVN_ITEMCHANGED) {
                    NMLISTVIEW* lv = reinterpret_cast<NMLISTVIEW*>(lp);
                    if ((lv->uChanged & LVIF_STATE) && (lv->uNewState & LVIS_SELECTED) &&
                        !(lv->uOldState & LVIS_SELECTED))
                        ShowDetailFor(static_cast<int>(lv->lParam));
                }
            }
            break;
        }
        case WM_APP_DONE: {
            JobResult* res = reinterpret_cast<JobResult*>(lp);
            FinishInspection(res);
            delete res;
            return 0;
        }
        case WM_APP_APPLIED: {
            JobResult* res = reinterpret_cast<JobResult*>(lp);
            if (static_cast<int>(wp) == kJobSacOff) FinishSacOff(res);
            else if (static_cast<int>(wp) == kJobClean) FinishClean(res);
            else if (static_cast<int>(wp) == kJobDeploy || static_cast<int>(wp) == kJobOneClick) FinishDeploy(res);
            else FinishApply(res);
            delete res;
            return 0;
        }
        case WM_APP_STARTUP:
            ShowUpdateResult();
            CheckSacOnStartup();
            if (!g.running &&
                EnsureInternet(L"アプリの入手やインストールには、インターネットが必要です。Wi-Fi や LAN ケーブルをつないでください。", true) &&
                IsInternetConnected())
                StartUpdateCheck();
            return 0;
        case WM_APP_SHARE_FOUND: {
            share::Offer* offer = reinterpret_cast<share::Offer*>(lp);
            if (g.running || g.updating || share::IsServerRunning() || !IsWindowEnabled(hwnd)) {
                share::Release(offer->session);  // 作業中などは、あとでもう一度知らせる
                delete offer;
            } else {
                share::HandleOffer(hwnd, offer);
            }
            return 0;
        }
        case WM_APP_UPDCHECK: {
            JobResult* res = reinterpret_cast<JobResult*>(lp);
            FinishUpdateCheck(res);
            delete res;
            return 0;
        }
        case WM_CLOSE:
            if (g.updating) {
                if (MessageBoxW(hwnd, L"新しい版を用意しています。終わると自動で起動し直します。\n今閉じますか？（更新は続きます）", kAppName,
                                MB_YESNO | MB_ICONQUESTION | MB_DEFBUTTON2) != IDYES)
                    return 0;
                DestroyWindow(hwnd);
                return 0;
            }
            if (g.running) {
                const wchar_t* q = g.deploying
                    ? L"時間のかかる準備の途中（または開始待ち）です。閉じますか？\n（閉じても作業は最後まで続きますが、この画面から取り消せなくなります）"
                    : g.applying
                    ? L"設定を変えている途中です。閉じますか？\n（閉じても、作業は最後まで続きます）"
                    : L"点検の途中です。閉じますか？\n（点検は見るだけなので、途中で閉じても PC には影響しません）";
                if (MessageBoxW(hwnd, q, kAppName, MB_YESNO | MB_ICONQUESTION | MB_DEFBUTTON2) != IDYES) return 0;
                DestroyWindow(hwnd);
                return 0;
            }
            if (ConfirmCleanOnExit()) return 0;  // 履歴を消してから閉じる（終わったら閉じる）
            DestroyWindow(hwnd);
            return 0;
        case WM_DESTROY:
            share::Shutdown();
            PostQuitMessage(0);
            return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

std::wstring ExeDirectory() {
    std::vector<wchar_t> buf(32768);
    DWORD n = GetModuleFileNameW(nullptr, buf.data(), static_cast<DWORD>(buf.size()));
    std::wstring p(buf.data(), n);
    size_t s = p.find_last_of(L"\\/");
    return s == std::wstring::npos ? std::wstring(L".") : p.substr(0, s);
}

// ---------------------------------------------------------------- Program Files に入れる（自分自身のインストール）

std::wstring NormalizePath(const std::wstring& p) {
    wchar_t buf[MAX_PATH * 4];
    DWORD n = GetFullPathNameW(p.c_str(), MAX_PATH * 4, buf, nullptr);
    std::wstring r = (n > 0 && n < MAX_PATH * 4) ? std::wstring(buf, n) : p;
    while (r.size() > 3 && (r.back() == L'\\' || r.back() == L'/')) r.pop_back();
    return r;
}

// path が base そのもの、または base の中なら true
bool IsSameOrUnder(const std::wstring& path, const std::wstring& base) {
    std::wstring p = NormalizePath(path), b = NormalizePath(base);
    if (_wcsicmp(p.c_str(), b.c_str()) == 0) return true;
    return p.size() > b.size() && _wcsnicmp(p.c_str(), b.c_str(), b.size()) == 0 && p[b.size()] == L'\\';
}

std::wstring KnownFolder(const GUID& id) {
    PWSTR p = nullptr;
    std::wstring r;
    if (SUCCEEDED(SHGetKnownFolderPath(id, 0, nullptr, &p)) && p) r = p;
    if (p) CoTaskMemFree(p);
    return r;
}

std::wstring InstallTarget() {
    wchar_t pf[MAX_PATH];
    DWORD n = GetEnvironmentVariableW(L"ProgramFiles", pf, MAX_PATH);
    std::wstring base = (n > 0 && n < MAX_PATH) ? std::wstring(pf) : std::wstring(L"C:\\Program Files");
    return base + L"\\Org_Apps\\ManabitoClassSetup";
}

// EXE の版（上位 16 ビットずつ 4 つ）。読めなければ 0。
ULONGLONG FileVersionOf(const std::wstring& path) {
    DWORD dummy = 0;
    DWORD size = GetFileVersionInfoSizeW(path.c_str(), &dummy);
    if (!size) return 0;
    std::vector<BYTE> data(size);
    if (!GetFileVersionInfoW(path.c_str(), 0, size, data.data())) return 0;
    VS_FIXEDFILEINFO* fi = nullptr;
    UINT len = 0;
    if (!VerQueryValueW(data.data(), L"\\", reinterpret_cast<void**>(&fi), &len) || !fi) return 0;
    return (static_cast<ULONGLONG>(fi->dwFileVersionMS) << 32) | fi->dwFileVersionLS;
}

std::wstring VersionText(ULONGLONG v) {
    wchar_t b[48];
    wsprintfW(b, L"%u.%u.%u", static_cast<unsigned>((v >> 48) & 0xFFFF), static_cast<unsigned>((v >> 32) & 0xFFFF),
              static_cast<unsigned>((v >> 16) & 0xFFFF));
    return b;
}

// 入れたあとに消す元：ダウンロード・デスクトップ・ドキュメントの中から起動したときだけ。
//   展開したフォルダーの名前が ManabitoClassSetup で始まる → そのフォルダーごと（同じ名前の zip も）
//   それ以外 → このアプリのファイル（EXE・scripts・profiles）だけ
struct CleanupPlan {
    std::wstring folder;   // フォルダーごと消すとき
    std::wstring zip;      // 一緒に消す zip
    std::wstring filesIn;  // ファイルだけ消すとき、そのフォルダー
    bool any() const { return !folder.empty() || !filesIn.empty(); }
};

CleanupPlan PlanCleanup(const std::wstring& exeDir) {
    static const GUID kDownloads = {0x374DE290, 0x123F, 0x4565, {0x91, 0x64, 0x39, 0xC4, 0x92, 0x5E, 0x46, 0x7B}};
    static const GUID kDesktop = {0xB4BFCC3A, 0xDB2C, 0x424C, {0xB0, 0x29, 0x7F, 0xE9, 0x9A, 0x87, 0xC6, 0x41}};
    static const GUID kDocuments = {0xFDD39AD0, 0x238F, 0x46AF, {0xAD, 0xB4, 0x6C, 0x85, 0x48, 0x03, 0x69, 0xC7}};
    CleanupPlan plan;
    for (const GUID* id : {&kDownloads, &kDesktop, &kDocuments}) {
        std::wstring base = KnownFolder(*id);
        if (base.empty() || !IsSameOrUnder(exeDir, base)) continue;
        std::wstring b = NormalizePath(base), e = NormalizePath(exeDir);
        if (_wcsicmp(b.c_str(), e.c_str()) == 0) {
            plan.filesIn = e;
            return plan;
        }
        std::wstring rest = e.substr(b.size() + 1);
        size_t sep = rest.find(L'\\');
        std::wstring topName = rest.substr(0, sep);
        std::wstring top = b + L"\\" + topName;
        if (_wcsnicmp(topName.c_str(), L"ManabitoClassSetup", 18) == 0) {
            plan.folder = top;
            if (FileExists(top + L".zip")) plan.zip = top + L".zip";
        } else {
            plan.filesIn = e;
        }
        return plan;
    }
    return plan;
}

// 起動したのが Program Files 以外なら、入れてから入れた先を起動する。この起動を終えるなら true。
bool SelfInstallIfNeeded(HANDLE& instanceMutex) {
    const std::wstring target = InstallTarget();
    if (IsSameOrUnder(g.exeDir, target)) return false;
    const std::wstring targetExe = target + L"\\ManabitoClassSetup.exe";
    const std::wstring selfExe = g.exeDir + L"\\ManabitoClassSetup.exe";
    const ULONGLONG mine = FileVersionOf(selfExe), theirs = FileExists(targetExe) ? FileVersionOf(targetExe) : 0;
    const CleanupPlan plan = PlanCleanup(g.exeDir);

    std::wstring content = L"このアプリを\n" + target + L"\nに入れて、そこから起動します（管理者の確認が出ます）。\n"
                           L"デスクトップとスタートメニューにショートカットを作ります。";
    if (theirs > mine) {
        content = L"より新しい版（" + VersionText(theirs) + L"）が " + target + L" に入っています。入れ替えずに、そちらを起動します。";
    }
    if (plan.any())
        content += L"\n\n起動したあと、元の場所（" + (plan.folder.empty() ? plan.filesIn : plan.folder) + L"）は削除します。";

    const int kInstall = 4001, kHere = 4002;
    TASKDIALOG_BUTTON buttons[] = {
        {kInstall, theirs > mine ? L"入っている新しい版を起動する" : L"Program Files に入れて起動する（おすすめ）"},
        {kHere, L"この場所のまま使う\n開発・試験のとき。元の場所は削除しません。"},
    };
    TASKDIALOGCONFIG c{};
    c.cbSize = sizeof(c);
    c.hInstance = g.inst;
    c.dwFlags = TDF_USE_COMMAND_LINKS | TDF_ALLOW_DIALOG_CANCELLATION;
    c.pszWindowTitle = kAppName;
    c.pszMainIcon = TD_INFORMATION_ICON;
    c.pszMainInstruction = L"まなびと講習準備を Program Files に入れます";
    c.pszContent = content.c_str();
    c.cButtons = 2;
    c.pButtons = buttons;
    c.nDefaultButton = kInstall;
    int button = 0;
    if (FAILED(TaskDialogIndirect(&c, &button, nullptr, nullptr)))
        button = (MessageBoxW(nullptr, (content + L"\n\n［はい］入れて起動する　［いいえ］この場所のまま使う").c_str(), kAppName,
                              MB_YESNO | MB_ICONINFORMATION) == IDYES) ? kInstall : kHere;
    if (button != kInstall) return false;

    if (theirs <= mine) {
        // 入れる（管理者として install.ps1）
        std::wstring script = g.exeDir + L"\\scripts\\install.ps1";
        if (!FileExists(script)) {
            MessageBoxW(nullptr, (L"入れる部品が見つかりません。この場所のまま起動します。\n\n" + script).c_str(), kAppName, MB_OK | MB_ICONWARNING);
            return false;
        }
        wchar_t tmp[MAX_PATH];
        GetTempPathW(MAX_PATH, tmp);
        std::wstring out = std::wstring(tmp) + L"ManabitoClassSetup_install_" + Timestamp() + L".txt";
        std::wstring params = L"-NoProfile -NonInteractive -ExecutionPolicy Bypass -WindowStyle Hidden -File \"" + script +
                              L"\" -Source \"" + g.exeDir + L"\" -Target \"" + target + L"\" -OutFile \"" + out + L"\"";
        wchar_t sys[MAX_PATH];
        UINT n = GetSystemDirectoryW(sys, MAX_PATH);
        std::wstring ps = std::wstring(n ? sys : L"C:\\Windows\\System32") + L"\\WindowsPowerShell\\v1.0\\powershell.exe";
        SHELLEXECUTEINFOW sei{};
        sei.cbSize = sizeof(sei);
        sei.fMask = SEE_MASK_NOCLOSEPROCESS | SEE_MASK_NOASYNC;
        sei.lpVerb = L"runas";
        sei.lpFile = ps.c_str();
        sei.lpParameters = params.c_str();
        sei.nShow = SW_HIDE;
        if (!ShellExecuteExW(&sei) || !sei.hProcess) {
            MessageBoxW(nullptr, L"Program Files に入れられませんでした（管理者の確認で「いいえ」など）。この場所のまま起動します。", kAppName,
                        MB_OK | MB_ICONINFORMATION);
            return false;
        }
        WaitForSingleObject(sei.hProcess, 120000);
        CloseHandle(sei.hProcess);
        std::string bytes;
        std::wstring result = ReadAllBytes(out, bytes) ? DecodeText(bytes) : std::wstring();
        DeleteFileW(out.c_str());
        if (result.rfind(L"OK", 0) != 0) {
            MessageBoxW(nullptr, (L"Program Files に入れられませんでした。この場所のまま起動します。\n\n" + result).c_str(), kAppName,
                        MB_OK | MB_ICONWARNING);
            return false;
        }
    }

    // 入れた先を起動する（元を消すのは、起動した側が、この起動が終わってから行う）
    std::wstring cmd = L"\"" + targetExe + L"\"";
    wchar_t pid[16];
    wsprintfW(pid, L"%lu", static_cast<unsigned long>(GetCurrentProcessId()));
    cmd += std::wstring(L" --parent-pid ") + pid;
    if (!plan.folder.empty()) cmd += L" --cleanup-folder \"" + plan.folder + L"\"";
    if (!plan.zip.empty()) cmd += L" --cleanup-zip \"" + plan.zip + L"\"";
    if (!plan.filesIn.empty()) cmd += L" --cleanup-files \"" + plan.filesIn + L"\"";
    if (instanceMutex) {
        CloseHandle(instanceMutex);  // 2 つ同時に起動しない印を手放してから、入れた先を起動する
        instanceMutex = nullptr;
    }
    std::vector<wchar_t> buf(cmd.begin(), cmd.end());
    buf.push_back(L'\0');
    STARTUPINFOW si{};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi{};
    if (!CreateProcessW(nullptr, buf.data(), nullptr, nullptr, FALSE, 0, nullptr, target.c_str(), &si, &pi)) {
        MessageBoxW(nullptr, (L"入れた先のアプリを起動できませんでした。\n\n" + targetExe).c_str(), kAppName, MB_OK | MB_ICONWARNING);
        return true;
    }
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    return true;
}

// 入れる前の場所を消す（Program Files 側で起動したとき、起動した元の終了を待ってから）
std::wstring g_cleanupFolder, g_cleanupZip, g_cleanupFiles;
DWORD g_parentPid = 0;

bool DeleteTree(const std::wstring& path) {
    if (GetFileAttributesW(path.c_str()) == INVALID_FILE_ATTRIBUTES) return true;
    std::vector<wchar_t> from(path.begin(), path.end());
    from.push_back(L'\0');
    from.push_back(L'\0');
    SHFILEOPSTRUCTW op{};
    op.wFunc = FO_DELETE;
    op.pFrom = from.data();
    op.fFlags = FOF_NOCONFIRMATION | FOF_NOERRORUI | FOF_SILENT;  // ごみ箱には入れない
    return SHFileOperationW(&op) == 0 && GetFileAttributesW(path.c_str()) == INVALID_FILE_ATTRIBUTES;
}

void CleanupInstallSource() {
    if (g_cleanupFolder.empty() && g_cleanupFiles.empty()) return;
    if (g_parentPid) {
        HANDLE h = OpenProcess(SYNCHRONIZE, FALSE, g_parentPid);
        if (h) {
            WaitForSingleObject(h, 15000);
            CloseHandle(h);
        }
    }
    // 念のため：Program Files の中や、入れた先そのものは決して消さない
    const std::wstring target = InstallTarget();
    std::wstring where = g_cleanupFolder.empty() ? g_cleanupFiles : g_cleanupFolder;
    if (IsSameOrUnder(where, target) || IsSameOrUnder(target, where)) return;
    bool ok = true;
    if (!g_cleanupFolder.empty()) {
        ok = DeleteTree(g_cleanupFolder);
        if (!g_cleanupZip.empty()) DeleteFileW(g_cleanupZip.c_str());
    } else {
        for (const wchar_t* name : {L"ManabitoClassSetup.exe", L"scripts", L"profiles"}) ok = DeleteTree(g_cleanupFiles + L"\\" + name) && ok;
        RemoveDirectoryW(g_cleanupFiles.c_str());  // 空になったときだけ消える
    }
    g.applyNote.clear();
    SetSummaryText(ok ? L"Program Files に入れました。元の場所（" + where + L"）は削除しました。"
                      : L"Program Files に入れました。元の場所（" + where + L"）の一部を削除できませんでした。手で削除してください。",
                   ok ? kOk : kNg);
}

}  // namespace

int WINAPI wWinMain(HINSTANCE inst, HINSTANCE, PWSTR, int show) {
    g.inst = inst;

    // 2 つ同時に起動しない（版や置き場所が違っても同じ名前）。すでに開いていれば、その窓を前に出して終わる。
    HANDLE instanceMutex = CreateMutexW(nullptr, TRUE, L"Local\\ManabitoClassSetup_Instance");
    if (instanceMutex && GetLastError() == ERROR_ALREADY_EXISTS) {
        HWND other = FindWindowW(kClassName, nullptr);
        if (!IsSameOrUnder(ExeDirectory(), InstallTarget())) {
            // 新しい版を入れようとしている：開いているほうを閉じてもらう（使用中の EXE は入れ替えられない）
            MessageBoxW(nullptr,
                        L"まなびと講習準備が、すでに開いています。\n\n新しい版に入れ替えるには、開いているほうを閉じてから、もう一度起動してください。",
                        kAppName, MB_OK | MB_ICONINFORMATION);
            if (other) SetForegroundWindow(other);
        } else if (other) {
            if (IsIconic(other)) ShowWindow(other, SW_RESTORE);
            SetForegroundWindow(other);
        } else {
            MessageBoxW(nullptr, L"まなびと講習準備は、すでに起動しています。", kAppName, MB_OK | MB_ICONINFORMATION);
        }
        CloseHandle(instanceMutex);
        return 0;
    }
    LoadDpiFunctions();
    g.exeDir = ExeDirectory();
    g.whiteBrush = CreateSolidBrush(kWhite);

    INITCOMMONCONTROLSEX icc{};
    icc.dwSize = sizeof(icc);
    icc.dwICC = ICC_LISTVIEW_CLASSES | ICC_STANDARD_CLASSES | ICC_DATE_CLASSES;
    InitCommonControlsEx(&icc);
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    Gdiplus::GdiplusStartupInput gsi;
    ULONG_PTR gdiToken = 0;
    Gdiplus::GdiplusStartup(&gdiToken, &gsi, nullptr);

    // 開発用：--sample <結果.tsv> で、点検せずに見本の結果を表示する（画面の確認用）
    std::wstring sample;
    bool portable = false;
    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (argv) {
        for (int i = 1; i < argc; ++i) {
            const bool hasNext = i + 1 < argc;
            if (wcscmp(argv[i], L"--portable") == 0) portable = true;  // 開発用：Program Files に入れない
            else if (hasNext && wcscmp(argv[i], L"--sample") == 0) sample = argv[++i];
            else if (hasNext && wcscmp(argv[i], L"--parent-pid") == 0) g_parentPid = static_cast<DWORD>(_wtoi(argv[++i]));
            else if (hasNext && wcscmp(argv[i], L"--cleanup-folder") == 0) g_cleanupFolder = argv[++i];
            else if (hasNext && wcscmp(argv[i], L"--cleanup-zip") == 0) g_cleanupZip = argv[++i];
            else if (hasNext && wcscmp(argv[i], L"--cleanup-files") == 0) g_cleanupFiles = argv[++i];
        }
        LocalFree(argv);
    }
    if (sample.empty() && !portable && SelfInstallIfNeeded(instanceMutex)) {
        Gdiplus::GdiplusShutdown(gdiToken);
        CoUninitialize();
        if (instanceMutex) CloseHandle(instanceMutex);
        return 0;
    }

    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = WndProc;
    wc.hInstance = inst;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
    wc.lpszClassName = kClassName;
    RegisterClassExW(&wc);

    g.dpi = SystemDpi();
    RECT work{};
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);
    int ww = std::min(S(1080), static_cast<int>(work.right - work.left));
    int wh = std::min(S(720), static_cast<int>(work.bottom - work.top));
    int wx = work.left + ((work.right - work.left) - ww) / 2;
    int wy = work.top + ((work.bottom - work.top) - wh) / 2;

    std::wstring title = std::wstring(kAppName) + L"（v" + kVersion + L"）";
    HWND hwnd = CreateWindowExW(0, kClassName, title.c_str(), WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN, wx, wy, ww, wh,
                                nullptr, nullptr, inst, nullptr);
    if (!hwnd) return 1;

    if (!sample.empty()) {
        if (LoadResults(sample, g.rows)) {
            g.lastFromSample = true;
            ApplyToggleJudgement();
            FillList();
            BuildSummary(0, true);
        }
    }

    ShowWindow(hwnd, show);
    UpdateWindow(hwnd);
    if (sample.empty()) PostMessageW(hwnd, WM_APP_STARTUP, 0, 0);
    CleanupInstallSource();

    MSG m;
    while (GetMessageW(&m, nullptr, 0, 0) > 0) {
        if (m.message == WM_KEYDOWN && m.wParam == VK_F5 && !g.running) {
            StartInspection();
            continue;
        }
        if (IsDialogMessageW(hwnd, &m)) continue;
        TranslateMessage(&m);
        DispatchMessageW(&m);
    }

    for (HFONT f : {g.font, g.fontBold, g.fontTitle, g.fontSmall})
        if (f) DeleteObject(f);
    DeleteObject(g.whiteBrush);
    Gdiplus::GdiplusShutdown(gdiToken);
    CoUninitialize();
    if (instanceMutex) CloseHandle(instanceMutex);
    return 0;
}
