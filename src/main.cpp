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
const wchar_t kVersion[] = L"0.19.5";
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
    IDC_PROFSET,      // ［設定…］（プロファイルの設定）
    IDC_TAB0,         // タブ：講習の準備
    IDC_TAB1,         // タブ：独自アプリ
    IDC_APPS_ALL,     // チェックしたものをまとめてセットアップ
    IDC_APPS_RELOAD,  // 一覧を読み直す
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
const UINT WM_APP_APPSTATUS = WM_APP + 6;  // 独自アプリの一覧を読み終えた
const UINT WM_APP_WUCHECK = WM_APP + 7;    // Windows Update の確認が終わった
const int kIdAppCheck = 6000;  // 独自アプリの行のチェックボックス（+ 行の番号）
const int kIdAppSetup = 6100;  // 独自アプリの行の［セットアップ］（+ 行の番号）
const int kIconId = 101;       // res\app.rc のアイコン
const UINT WM_APP_SHARE_FOUND = WM_APP + 5;  // LAN 内に配布サーバーを見つけた（lParam = share::Offer*）
const UINT_PTR kTimerUpdate = 2;
const UINT_PTR kTimerReport = 4;  // 先生用への報告（30 秒ごと）
const UINT WM_APP_REPORTDONE = WM_APP + 8;
const UINT WM_APP_PUSHDONE = WM_APP + 9;  // 自動で配られたファイルを受け取り終えた

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
    std::wstring file;      // ファイル名（standard.json など）
    bool builtin = false;   // アプリの profiles にあるもの
    bool custom = false;    // %ProgramData%\ManabitoClassSetup\profiles にあるもの（設定画面で保存したもの）
};

// 講座のプロファイルで選べる追加のアプリ（scripts\extra_apps.txt）
struct ExtraApp {
    std::wstring id, name;
    bool pdf = false;
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
    HWND profSet = nullptr;     // ［設定…］
    int extrasTop = 0;          // 「この講座で入れるアプリ」の行
    std::wstring extrasText;    // その文（プロファイルを選んだときに作る）
    // タブ（0 = 講習の準備、1 = 独自アプリ）
    int tab = 0;
    HWND tabBtn[2] = {nullptr, nullptr};
    // 独自アプリのタブ
    struct AppItem {
        std::wstring name, exe, installed, latest, state, url, sha;
        bool self = false;
        HWND check = nullptr, setup = nullptr;
    };
    std::vector<AppItem> apps;
    HWND appsAll = nullptr, appsReload = nullptr;
    std::wstring appsOut, appsError;
    bool appsLoading = false, appsLoaded = false, appsDeploy = false;
    int appsTop = 0, appsButtonsTop = 0;
    struct PendingSelf {
        bool on = false;
        std::wstring ver, url, sha;
    } pendingSelf;  // 独自アプリを入れ終えたあとで、このアプリ自身を更新する
    // 先生用への報告
    bool reporting = false, pairAsked = false;
    // 起動直後は、更新の判断が済むまで先生からの案内などを後回しにする（更新の案内を先に出すため。v0.19.0 の実機で順番が逆だった）
    bool updateDecided = false;
    DWORD startTick = 0;
    DWORD remoteInspectTick = 0;  // 先生用から頼まれて点検した時刻（5 分に 1 回まで）
    // 自動で配られたファイル
    bool pushing = false;
    std::string pushTriedId;  // 最後に受け取りを試した番号
    int pushTries = 0;        // その番号を試した回数（3 回まで）
    std::wstring pushState, pushCurrent;  // 先生用への報告に入れる（OK・NG と、番号・個数）
    std::string printersFailVer;
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

// 設定画面で保存したプロファイルの置き場所（アプリの更新で消えない）
std::wstring CustomProfileDir() {
    wchar_t buf[MAX_PATH] = L"";
    DWORD n = GetEnvironmentVariableW(L"ProgramData", buf, MAX_PATH);
    std::wstring pd = (n > 0 && n < MAX_PATH) ? std::wstring(buf, n) : L"C:\\ProgramData";
    return pd + L"\\ManabitoClassSetup\\profiles";
}

void LoadProfiles(const std::wstring& keepFile = L"") {
    std::wstring keep = keepFile;
    if (keep.empty()) {
        int cur = static_cast<int>(SendMessageW(g.combo, CB_GETCURSEL, 0, 0));
        if (cur >= 0 && cur < static_cast<int>(g.profiles.size())) keep = g.profiles[static_cast<size_t>(cur)].file;
    }
    g.profiles.clear();
    // アプリの profiles と、保存の場所。同じファイル名なら、保存の場所のもの（設定画面で変えたもの）を使う
    const std::wstring dirs[2] = {g.exeDir + L"\\profiles\\", CustomProfileDir() + L"\\"};
    for (int d = 0; d < 2; ++d) {
        WIN32_FIND_DATAW fd{};
        HANDLE f = FindFirstFileW((dirs[d] + L"*.json").c_str(), &fd);
        if (f == INVALID_HANDLE_VALUE) continue;
        do {
            if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
            ProfileEntry p;
            p.file = fd.cFileName;
            p.path = dirs[d] + fd.cFileName;
            p.name = ReadProfileName(p.path);
            if (p.name.empty()) p.name = fd.cFileName;
            auto it = std::find_if(g.profiles.begin(), g.profiles.end(), [&](const ProfileEntry& e) { return _wcsicmp(e.file.c_str(), p.file.c_str()) == 0; });
            if (it != g.profiles.end()) {
                it->path = p.path;
                it->name = p.name;
                it->custom = true;
            } else {
                p.builtin = (d == 0);
                p.custom = (d == 1);
                g.profiles.push_back(p);
            }
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
    int sel = 0;
    for (size_t i = 0; i < g.profiles.size(); ++i) {
        SendMessageW(g.combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(g.profiles[i].name.c_str()));
        if (!keep.empty() && _wcsicmp(g.profiles[i].file.c_str(), keep.c_str()) == 0) sel = static_cast<int>(i);
    }
    if (g.profiles.empty()) {
        SendMessageW(g.combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"（プロファイルなし）"));
    }
    SendMessageW(g.combo, CB_SETCURSEL, sel, 0);
}

std::vector<ExtraApp> LoadExtraCatalog() {
    std::vector<ExtraApp> list;
    std::string bytes;
    if (!ReadAllBytes(g.exeDir + L"\\scripts\\extra_apps.txt", bytes)) return list;
    std::wstring t = DecodeText(bytes);
    size_t pos = 0;
    while (pos < t.size()) {
        size_t nl = t.find(L'\n', pos);
        std::wstring line = t.substr(pos, nl == std::wstring::npos ? std::wstring::npos : nl - pos);
        pos = nl == std::wstring::npos ? t.size() : nl + 1;
        while (!line.empty() && (line.back() == L'\r' || line.back() == L' ')) line.pop_back();
        if (line.empty() || line[0] == L'#') continue;
        std::vector<std::wstring> f;
        size_t a0 = 0;
        for (;;) {
            size_t b0 = line.find(L'|', a0);
            f.push_back(line.substr(a0, b0 == std::wstring::npos ? std::wstring::npos : b0 - a0));
            if (b0 == std::wstring::npos) break;
            a0 = b0 + 1;
        }
        if (f.size() < 6) continue;
        list.push_back({f[0], f[1], f[4] == L"1"});
    }
    return list;
}

std::vector<std::wstring> SplitComma(const std::wstring& s) {
    std::vector<std::wstring> out;
    size_t a = 0;
    while (a <= s.size()) {
        size_t b = s.find(L',', a);
        std::wstring t = s.substr(a, b == std::wstring::npos ? std::wstring::npos : b - a);
        while (!t.empty() && t.front() == L' ') t.erase(0, 1);
        while (!t.empty() && t.back() == L' ') t.pop_back();
        if (!t.empty()) out.push_back(t);
        if (b == std::wstring::npos) break;
        a = b + 1;
    }
    return out;
}

// 選んでいるプロファイルの「この講座で入れるアプリ」の名前（・区切り。無ければ空）
std::wstring ProfileExtraNames(const std::wstring& profilePath) {
    if (profilePath.empty()) return std::wstring();
    auto ids = SplitComma(ReadJsonString(profilePath, L"extraApps"));
    if (ids.empty()) return std::wstring();
    auto cat = LoadExtraCatalog();
    std::wstring out;
    for (const auto& id : ids) {
        std::wstring name = id;
        for (const auto& a : cat)
            if (a.id == id) name = a.name;
        out += (out.empty() ? L"" : L"・") + name;
    }
    return out;
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
    for (HWND h : {g.run, g.oneclick, g.apply, g.clean, g.combo, g.profSet, g.tog[0], g.tog[1], g.tog[2], g.runNow, g.timePick}) {
        if (!h) continue;
        EnableWindow(h, !busy);
        InvalidateRect(h, nullptr, FALSE);
    }
    for (HWND h : {g.appsAll, g.appsReload}) {
        if (!h) continue;
        EnableWindow(h, !busy);
        InvalidateRect(h, nullptr, FALSE);
    }
    for (auto& a : g.apps) {
        const bool selfLatest = a.self && a.state != L"old";
        EnableWindow(a.check, !busy && !selfLatest);
        EnableWindow(a.setup, !busy && !selfLatest);
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

bool ConfirmWindowsUpdate();  // 後で定義

void StartApply() {
    if (g.running) return;
    if (!ConfirmWindowsUpdate()) return;
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

void NotePrintersResult(const std::wstring& tsv);  // 後で定義

void FinishApply(JobResult* res) {
    static const ElevatedText t = {
        L"そろえるのを取りやめました（管理者の確認で「いいえ」が押されました）。設定は変えていません。",
        L"そろえる作業を始められませんでした（エラー %lu）。",
        L"そろえた結果を受け取れませんでした。",
        L"そろえました",
        L"変えた",
        L"そろえられなかった項目があります。",
    };
    NotePrintersResult(g.applyTsv);  // プリンターが入らなかったら、その設定の版を覚える
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
        L"・Edge・Chrome を初めて開いたときの状態に（サインイン・お気に入り・履歴・Cookie・パスワード・拡張機能。開いているタブは失われます）\n"
        L"・前の受講生のサインイン：Windows に追加されたアカウント、Office、Adobe Creative Cloud、保存されたログイン情報\n"
        L"・Zoom・LINE・ChatGPT・Claude・Firefox など、ほかのアプリやブラウザーのデータ（ログインと設定が消えます）\n"
        L"・受け取った授業ファイル\n\n"
        L"残すもの：このアプリが入っているフォルダー、OneDrive と同期しているフォルダー、クイック アクセスのピン留め、"
        L"ネットワーク共有・プリンター用の資格情報\n\n"
        L"Word・Excel・PowerPoint・メモ帳・Adobe のアプリは、閉じてから進めてください。\n"
        L"※ 開発用の PC では実行しないでください（git や開発ツールのログインも消えます）。\n"
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

void SendReportNow();  // 先生用への報告（後で定義）

// 更新の判断が済んだか（起動から 60 秒たったら、判断を待たない：確認がいつまでも終わらないときに、案内が出なくならないように）
// 記録のフォルダーの整理：ファイルが 50 個を超えたら、古いものから消して新しい 30 個を残す（淳さんの提案）
// 10 分以内に書かれたものは消さない（ほかの作業が書いている途中かもしれないため）。フォルダーの中のファイルだけを消す。
void TidyLogs() {
    EnsureLogDir();  // 起動直後は、記録のフォルダーの場所がまだ決まっていないことがある
    if (g.logDir.empty()) return;
    struct F {
        std::wstring path;
        ULONGLONG time;
    };
    std::vector<F> files;
    WIN32_FIND_DATAW fd{};
    HANDLE h = FindFirstFileW((g.logDir + L"\\*").c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return;
    do {
        if (fd.dwFileAttributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)) continue;
        ULARGE_INTEGER t{};
        t.LowPart = fd.ftLastWriteTime.dwLowDateTime;
        t.HighPart = fd.ftLastWriteTime.dwHighDateTime;
        files.push_back({g.logDir + L"\\" + fd.cFileName, t.QuadPart});
    } while (FindNextFileW(h, &fd));
    FindClose(h);
    if (files.size() <= 50) return;
    std::sort(files.begin(), files.end(), [](const F& a, const F& b) { return a.time < b.time; });  // 古い順
    FILETIME nowFt{};
    GetSystemTimeAsFileTime(&nowFt);
    ULARGE_INTEGER now{};
    now.LowPart = nowFt.dwLowDateTime;
    now.HighPart = nowFt.dwHighDateTime;
    const ULONGLONG tenMinutes = 10ULL * 60 * 10000000ULL;
    size_t remove = files.size() - 30;
    for (size_t i = 0; i < files.size() && remove > 0; ++i) {
        if (now.QuadPart > files[i].time && now.QuadPart - files[i].time < tenMinutes) continue;
        if (DeleteFileW(files[i].path.c_str())) --remove;
    }
}

bool StartupSettled() { return g.updateDecided || (g.startTick != 0 && GetTickCount() - g.startTick > 60000); }

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
        SendReportNow();  // 先生用に、点検の結果を知らせる
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


// ---------------------------------------------------------------- 先生用への報告（受講生の PC。30 秒ごと・点検のあと）

std::string Utf8Of(const std::wstring& w) {
    if (w.empty()) return std::string();
    int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), static_cast<int>(w.size()), nullptr, 0, nullptr, nullptr);
    std::string s(static_cast<size_t>(n), '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.c_str(), static_cast<int>(w.size()), &s[0], n, nullptr, nullptr);
    return s;
}

std::string ReportField(const std::wstring& w, size_t maxLen) {
    std::wstring t = w.substr(0, maxLen);
    for (auto& c : t)
        if (c == L'|' || c == L'\r' || c == L'\n' || c == L'\t') c = L' ';
    return Utf8Of(t);
}

// 点検の結果（画面に出している g.rows）から、報告の本文を作る
std::string BuildReportPayload() {
    wchar_t pc[MAX_COMPUTERNAME_LENGTH + 2];
    DWORD n = MAX_COMPUTERNAME_LENGTH + 1;
    std::wstring pcName = GetComputerNameW(pc, &n) ? std::wstring(pc, n) : L"PC";
    int ok = 0, ng = 0, na = 0;
    std::vector<const Row*> items;
    static const wchar_t* keep[] = {L"office", L"health-disk", L"health-clock", L"health-battery", L"printers", L"wupending", L"defender"};
    for (const auto& r : g.rows) {
        if (r.state == L"OK") ++ok;
        else if (r.state == L"NG" || r.state == L"ERR") ++ng;
        else ++na;
        bool always = false;
        for (const wchar_t* k : keep) always = always || (r.id == k);
        if ((always || r.state == L"NG" || r.state == L"ERR") && items.size() < 120) items.push_back(&r);
    }
    const bool hasPush = !g.pushCurrent.empty();
    std::string s = "REPORT|" + ReportField(pcName, 64) + "|" + Utf8Of(kVersion) + "|" + std::to_string(ok) + "|" + std::to_string(ng) + "|" +
                    std::to_string(na) + "|" + std::to_string(items.size() + (hasPush ? 1 : 0)) + "\n";
    for (const Row* r : items)
        s += "I|" + ReportField(r->id, 64) + "|" + ReportField(r->state, 8) + "|" + ReportField(r->name, 120) + "|" + ReportField(r->current, 200) + "\n";
    if (hasPush) s += "I|push|" + ReportField(g.pushState, 8) + "|自動で配られたファイル|" + ReportField(g.pushCurrent, 200) + "\n";
    return s;
}

// 数字だけの版を比べる（a が新しければ true）
bool VersionNewer(const std::string& a, const std::string& b) {
    if (a.empty() || a.find_first_not_of("0123456789") != std::string::npos) return false;
    if (b.empty()) return true;
    if (a.size() != b.size()) return a.size() > b.size();
    return a > b;
}

struct ReportJob {
    HWND hwnd = nullptr;
    share::TeacherInfo teacher;
    std::string payload;
    // 結果
    bool sent = false, inspect = false, needPair = false, fetched = false, fetchFailed = false;
    std::string printersVersion, pushId;  // pushId：受け取るべき、自動で配られたものの番号
    std::wstring message;
};

// 入らなかったプリンターの設定の版（閉じるときに覚える。この版はもう受け取らない。先生が配り直すと版が変わり、また受け取る）
std::wstring DeclinedPrintersPath() { return AppDataDir() + L"\\printers_declined.txt"; }

std::string DeclinedPrintersVersion() {
    std::string b;
    if (!ReadAllBytes(DeclinedPrintersPath(), b)) return std::string();
    while (!b.empty() && (b.back() == '\r' || b.back() == '\n' || b.back() == ' ')) b.pop_back();
    return b.find_first_not_of("0123456789") == std::string::npos ? b : std::string();
}

// プリンターを入れられなかった設定の版（［設定をそろえる］［まとめて実行］の結果から覚える）
std::wstring FailedPrintersPath() { return AppDataDir() + L"\\printers_failed.txt"; }

void WriteSmallFile(const std::wstring& path, const std::string& text) {
    HANDLE h = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return;
    DWORD w = 0;
    WriteFile(h, text.data(), static_cast<DWORD>(text.size()), &w, nullptr);
    CloseHandle(h);
}

// ［設定をそろえる］［まとめて実行］の結果のファイルを見て、プリンターが入らなかったら、その設定の版を覚える（入ったら忘れる）
// （画面の一覧は、閉じる前の［履歴を消す］などで入れ替わるため、結果のファイルで判断する。v0.19.4 の実機で、閉じても片付かなかった）
void NotePrintersResult(const std::wstring& tsv) {
    std::vector<Row> rows;
    if (!FileExists(tsv) || !LoadResults(tsv, rows)) return;
    for (const auto& r : rows) {
        if (r.id != L"printers") continue;
        if (r.state == L"FAIL") {
            const std::string ver = share::LocalPrintersVersion();
            if (!ver.empty()) WriteSmallFile(FailedPrintersPath(), ver);
        } else if (r.state == L"DONE" || r.state == L"SAME") {
            DeleteFileW(FailedPrintersPath().c_str());
        }
    }
}

// 閉じるとき：入らなかった設定がまだ残っていれば消し、その版を「受け取らない版」として覚える（淳さんの指定）
void ForgetFailedPrinters() {
    std::string failed;
    if (!ReadAllBytes(FailedPrintersPath(), failed)) return;
    while (!failed.empty() && (failed.back() == '\r' || failed.back() == '\n' || failed.back() == ' ')) failed.pop_back();
    const std::string ver = share::LocalPrintersVersion();
    if (failed.empty() || ver != failed) {  // 失敗のあとに新しい設定を受け取っている：その設定はまだ入れようとしていない
        if (ver != failed) DeleteFileW(FailedPrintersPath().c_str());
        return;
    }
    const std::wstring cfg = AppDataDir() + L"\\printers.txt";
    if (!DeleteFileW(cfg.c_str()) && GetLastError() != ERROR_FILE_NOT_FOUND) return;
    WriteSmallFile(DeclinedPrintersPath(), ver);
    DeleteFileW(FailedPrintersPath().c_str());
}

DWORD WINAPI ReportThread(LPVOID param) {
    ReportJob* job = static_cast<ReportJob*>(param);
    share::ReportReply reply;
    job->sent = share::SendReport(job->teacher, job->payload, reply);
    if (job->sent) {
        job->inspect = reply.inspect;
        job->printersVersion = reply.printersVersion;
        // 自動で配られているもの：登録した先生の PC で、まだ受け取っていない新しい番号なら
        if (!reply.pushId.empty() && job->teacher.paired && reply.fingerprint == job->teacher.fingerprint &&
            VersionNewer(reply.pushId, share::LastPushId(job->teacher.fingerprint)))
            job->pushId = reply.pushId;
        if (!reply.printersVersion.empty()) {
            if (!job->teacher.paired || reply.fingerprint != job->teacher.fingerprint) {
                job->needPair = true;  // 配られているが、この先生の PC を登録していない
            } else if (VersionNewer(reply.printersVersion, share::LocalPrintersVersion()) &&
                       reply.printersVersion != DeclinedPrintersVersion()) {  // 入らなかった版は、もう受け取らない
                job->fetched = share::FetchPrinters(job->teacher, job->message);
                job->fetchFailed = !job->fetched;
            }
        }
    }
    if (!PostMessageW(job->hwnd, WM_APP_REPORTDONE, 0, reinterpret_cast<LPARAM>(job))) delete job;
    return 0;
}

void SendReportNow() {
    if (g.reporting || share::IsServerRunning()) return;  // 先生用の窓を開いている PC（先生の PC）は報告しない
    share::TeacherInfo t = share::GetTeacher();
    if (!t.valid) return;
    ReportJob* job = new ReportJob();
    job->hwnd = g.hwnd;
    job->teacher = t;
    job->payload = BuildReportPayload();
    HANDLE th = CreateThread(nullptr, 0, ReportThread, job, 0, nullptr);
    if (!th) {
        delete job;
        return;
    }
    CloseHandle(th);
    g.reporting = true;
}

// ---- 自動で配られたファイル（裏で受け取る。報告は止めない）
struct PushJob {
    HWND hwnd = nullptr;
    share::TeacherInfo teacher;
    std::string pushId;
    share::PushResult result;
    bool ok = false;
    std::wstring message;
};

DWORD WINAPI PushThread(LPVOID param) {
    PushJob* job = static_cast<PushJob*>(param);
    job->ok = share::FetchPush(job->teacher, job->pushId, job->result, job->message);
    if (!PostMessageW(job->hwnd, WM_APP_PUSHDONE, 0, reinterpret_cast<LPARAM>(job))) delete job;
    return 0;
}

void StartPushFetch(const share::TeacherInfo& t, const std::string& pushId) {
    if (g.pushing) return;
    if (pushId == g.pushTriedId && g.pushTries >= 3) return;  // 同じ番号は 3 回まで（開いているファイルなどで、いつまでも繰り返さない）
    if (pushId != g.pushTriedId) {
        g.pushTriedId = pushId;
        g.pushTries = 0;
    }
    ++g.pushTries;
    PushJob* job = new PushJob();
    job->hwnd = g.hwnd;
    job->teacher = t;
    job->pushId = pushId;
    HANDLE th = CreateThread(nullptr, 0, PushThread, job, 0, nullptr);
    if (!th) {
        delete job;
        return;
    }
    CloseHandle(th);
    g.pushing = true;
    g.pushState = L"NA";
    g.pushCurrent = std::wstring(pushId.begin(), pushId.end()) + L"：受け取り中";
    SetSummaryText(L"先生からファイルを受け取っています…", kAccent);
}

void FinishPush(PushJob* job) {
    g.pushing = false;
    const std::wstring id(job->pushId.begin(), job->pushId.end());
    const std::wstring folder = job->result.folder;
    const bool ok = job->ok;
    const std::wstring message = job->message;
    const int received = job->result.received;
    g.pushState = ok ? L"OK" : L"NG";
    g.pushCurrent = id + (ok ? L"：受け取り済み（" + std::to_wstring(received) + L" 個）" : L"：" + message);
    delete job;
    SendReportNow();  // 先生用に、受け取った結果を知らせる
    if (!ok) {
        SetSummaryText(L"先生からのファイル：" + message + (g.pushTries < 3 ? L"（あとでもう一度受け取ります）" : L""), kNg);
        return;
    }
    SetSummaryText(message + L"　受け取った場所：" + folder, kOk);
    // 作業中・ほかの窓を開いているときは、知らせの窓を出さない（下の行の知らせだけ）
    if (g.running || !IsWindowEnabled(g.hwnd)) return;
    std::wstring ask = message + L"\n\n受け取った場所：" + folder + L"\n\nフォルダーを開きますか？";
    if (MessageBoxW(g.hwnd, ask.c_str(), kAppName, MB_YESNO | MB_ICONINFORMATION) == IDYES)
        ShellExecuteW(g.hwnd, L"open", folder.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
}

void FinishReport(ReportJob* job) {
    g.reporting = false;
    const share::TeacherInfo t = job->teacher;
    const bool inspect = job->inspect, needPair = job->needPair;
    const std::string pushId = job->pushId;
    if (job->fetched) {
        SetSummaryText(job->message, kOk);
    } else if (job->fetchFailed && job->printersVersion != g.printersFailVer) {
        g.printersFailVer = job->printersVersion;  // 同じ版の失敗は 1 回だけ知らせる
        SetSummaryText(L"プリンターの設定：" + job->message, kNg);
    }
    delete job;
    if (!pushId.empty() && StartupSettled() && !g.updating) StartPushFetch(t, pushId);  // 更新の判断の前は受け取らない（次の報告でまた知らされる）
    // 先生用から頼まれた点検：5 分に 1 回まで（偽の「先生用」に繰り返し頼まれても重くならないように）
    if (inspect && !g.running && (g.remoteInspectTick == 0 || GetTickCount() - g.remoteInspectTick > 300000)) {
        g.remoteInspectTick = GetTickCount();
        StartInspection();
    }
    // プリンターの設定が配られているのに、この先生の PC を登録していない：1 回だけ案内する（作業中・ほかの窓を開いているときは出さない）
    if (needPair && !g.pairAsked && !g.running && !g.updating && StartupSettled() && IsWindowEnabled(g.hwnd)) {
        g.pairAsked = true;
        std::wstring ask = L"先生の PC（" + t.name +
                           L"）が、プリンターの設定を配っています。\n\n受け取るには、この PC に先生の PC を登録します（1 回だけ。先生の画面の印を確かめ、合言葉を入れます）。\n\n今、登録しますか？";
        if (MessageBoxW(g.hwnd, ask.c_str(), kAppName, MB_YESNO | MB_ICONQUESTION) == IDYES) {
            std::wstring msg;
            const bool ok = share::PairInteractive(g.hwnd, t, msg);
            if (!msg.empty()) MessageBoxW(g.hwnd, msg.c_str(), kAppName, MB_OK | (ok ? MB_ICONINFORMATION : MB_ICONWARNING));
            if (ok) SendReportNow();  // すぐに受け取る
        }
    }
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
    g.toggleOn[2] = false;  // 独自アプリは初めはオフ（独自アプリのタブから入れるのが基本。淳さんの指定）
    {
        const std::wstring names = ProfileExtraNames(p);
        const std::wstring pdf = ReadJsonString(p, L"pdfDefault");
        g.extrasText = names.empty() ? L"なし（プロファイルの横の［設定…］で、講座ごとに選べます）"
                                     : names + L"　―［今すぐ実行］［まとめて実行］で、入っていなければ入れます" + (pdf.empty() ? L"" : L"。PDF は Adobe Acrobat Reader で開きます");
    }
    for (HWND h : g.tog)
        if (h) InvalidateRect(h, nullptr, FALSE);
}


// ---------------------------------------------------------------- プロファイルの設定（v0.19.0）
//   保存・削除は scripts\save_profile.ps1（管理者）。置き場所は %ProgramData%\ManabitoClassSetup\profiles。
//   アプリに入っているプロファイルを変えたときは、同じファイル名で保存の場所に置く（こちらが優先。［元に戻す］で消す）。

// 管理者として動かし、終わるまで待つ（待つ間も窓を描き直す）。戻り値：起動できたか
bool RunElevatedWait(HWND owner, const std::wstring& params, DWORD timeoutMs) {
    SHELLEXECUTEINFOW sei{};
    sei.cbSize = sizeof(sei);
    sei.fMask = SEE_MASK_NOCLOSEPROCESS | SEE_MASK_NOASYNC;
    sei.hwnd = owner;
    sei.lpVerb = L"runas";
    const std::wstring exe = PowerShellPath();
    sei.lpFile = exe.c_str();
    sei.lpParameters = params.c_str();
    sei.nShow = SW_HIDE;
    if (!ShellExecuteExW(&sei) || !sei.hProcess) return false;
    const DWORD t0 = GetTickCount();
    HCURSOR old = SetCursor(LoadCursorW(nullptr, IDC_WAIT));
    for (;;) {
        DWORD left = timeoutMs > GetTickCount() - t0 ? timeoutMs - (GetTickCount() - t0) : 0;
        // 描き直しだけを行う（タイマーなどは保存のあとに回す：待つ間に、ほかの確認の窓が出ないように）
        DWORD r = MsgWaitForMultipleObjects(1, &sei.hProcess, FALSE, left, QS_PAINT);
        if (r != WAIT_OBJECT_0 + 1) break;  // 終わった・時間切れ
        MSG msg;
        while (PeekMessageW(&msg, nullptr, WM_PAINT, WM_PAINT, PM_REMOVE)) DispatchMessageW(&msg);
    }
    SetCursor(old);
    CloseHandle(sei.hProcess);
    return true;
}

enum : int {
    IDP_LIST = 7300,
    IDP_NEW,
    IDP_DELETE,
    IDP_NAME,
    IDP_DESC,
    IDP_OFFICE,
    IDP_ADOBE,
    IDP_PDF,
    IDP_COLOR,
    IDP_WU,
    IDP_SAVE,
    IDP_CLOSE,
    IDP_NOTE,
    IDP_APP0 = 7400,  // + 目録の番号
};

struct ProfDlg {
    HWND hwnd = nullptr;
    std::vector<ProfileEntry> items;
    std::vector<ExtraApp> cat;
    std::vector<HWND> checks;
    std::vector<int> pdfIndex;  // PDF の選択肢 → 目録の番号（0 番目は「変えない」）
    int cur = -1;
    bool dirty = false, loading = false;
    bool pendingNew = false;  // まだ保存していない新しいプロファイル（一覧の最後）
    HFONT font = nullptr;
} g_pd;

std::wstring StandardProfilePath() {
    for (const auto& p : g.profiles)
        if (_wcsicmp(p.file.c_str(), L"standard.json") == 0) return p.path;
    return g.profiles.empty() ? std::wstring() : g.profiles[0].path;
}

void ProfDlgFillList() {
    HWND lb = GetDlgItem(g_pd.hwnd, IDP_LIST);
    SendMessageW(lb, LB_RESETCONTENT, 0, 0);
    for (size_t i = 0; i < g_pd.items.size(); ++i) {
        const auto& p = g_pd.items[i];
        std::wstring t = p.name;
        if (g_pd.pendingNew && i + 1 == g_pd.items.size()) t += L"（新しい・まだ保存していません）";
        else if (p.builtin && p.custom) t += L"（変更あり）";
        else if (p.custom) t += L"（追加）";
        SendMessageW(lb, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(t.c_str()));
    }
    SendMessageW(lb, LB_SETCURSEL, g_pd.cur, 0);
}

void ProfDlgLoadFields(int i) {
    g_pd.loading = true;
    g_pd.cur = i;
    const ProfileEntry& p = g_pd.items[static_cast<size_t>(i)];
    const std::wstring path = p.path;  // 新しいものは、基準の中身を読む
    SetDlgItemTextW(g_pd.hwnd, IDP_NAME, p.name.c_str());
    SetDlgItemTextW(g_pd.hwnd, IDP_DESC, ReadJsonString(path, L"description").c_str());
    SendDlgItemMessageW(g_pd.hwnd, IDP_OFFICE, CB_SETCURSEL, ReadJsonString(path, L"office") == L"install" ? 0 : 1, 0);
    SendDlgItemMessageW(g_pd.hwnd, IDP_ADOBE, CB_SETCURSEL, ReadJsonString(path, L"adobe") == L"install" ? 0 : 1, 0);
    auto ids = SplitComma(ReadJsonString(path, L"extraApps"));
    for (size_t k = 0; k < g_pd.cat.size(); ++k) {
        bool on = std::find(ids.begin(), ids.end(), g_pd.cat[k].id) != ids.end();
        SendMessageW(g_pd.checks[k], BM_SETCHECK, on ? BST_CHECKED : BST_UNCHECKED, 0);
    }
    const std::wstring pdf = ReadJsonString(path, L"pdfDefault");
    int pdfSel = 0;
    for (size_t k = 1; k < g_pd.pdfIndex.size(); ++k)
        if (g_pd.cat[static_cast<size_t>(g_pd.pdfIndex[k])].id == pdf) pdfSel = static_cast<int>(k);
    SendDlgItemMessageW(g_pd.hwnd, IDP_PDF, CB_SETCURSEL, pdfSel, 0);
    std::wstring color = ReadJsonString(path, L"wallpaperColor");
    SetDlgItemTextW(g_pd.hwnd, IDP_COLOR, color.empty() ? L"#1F3A5F" : color.c_str());
    SendDlgItemMessageW(g_pd.hwnd, IDP_WU, CB_SETCURSEL, ReadJsonString(path, L"windowsUpdate") == L"stop" ? 0 : 1, 0);
    // 削除・元に戻す
    const bool isNew = g_pd.pendingNew && i + 1 == static_cast<int>(g_pd.items.size());
    HWND del = GetDlgItem(g_pd.hwnd, IDP_DELETE);
    SetWindowTextW(del, (p.builtin && p.custom) ? L"元に戻す" : isNew ? L"作るのをやめる" : L"削除する");
    EnableWindow(del, isNew || p.custom);
    g_pd.dirty = isNew;
    g_pd.loading = false;
}

// 変えたまま離れてよいか
bool ProfDlgCanLeave() {
    if (!g_pd.dirty) return true;
    return MessageBoxW(g_pd.hwnd, L"変えたところを保存していません。保存せずに進みますか？", kAppName, MB_YESNO | MB_ICONQUESTION | MB_DEFBUTTON2) == IDYES;
}

std::wstring GetText(HWND dlg, int id) {
    HWND h = GetDlgItem(dlg, id);
    std::wstring t(static_cast<size_t>(GetWindowTextLengthW(h)) + 1, L'\0');
    GetWindowTextW(h, &t[0], static_cast<int>(t.size()));
    t.resize(wcslen(t.c_str()));
    return t;
}

// 保存の部品を動かし、結果（1 行目 OK/NG、2 行目 説明）を読む
bool ProfDlgRunSave(const std::wstring& args, std::wstring& message) {
    EnsureLogDir();
    const std::wstring out = g.logDir + L"\\プロファイルの保存_" + Timestamp() + L".txt";
    const std::wstring script = g.exeDir + L"\\scripts\\save_profile.ps1";
    if (!FileExists(script)) {
        message = L"保存の部品が見つかりません：" + script;
        return false;
    }
    std::wstring params = L"-NoProfile -NonInteractive -ExecutionPolicy Bypass -WindowStyle Hidden -File \"" + script + L"\" -OutFile \"" + out + L"\" " + args;
    EnableWindow(g_pd.hwnd, FALSE);  // 待つ間は操作させない（二度押しを防ぐ）
    const bool started = RunElevatedWait(g_pd.hwnd, params, 60000);
    EnableWindow(g_pd.hwnd, TRUE);
    SetForegroundWindow(g_pd.hwnd);
    if (!started) {
        message = L"管理者の確認で「はい」が押されなかったため、保存しませんでした。";
        return false;
    }
    std::string bytes;
    if (!ReadAllBytes(out, bytes)) {
        message = L"保存の結果を読めませんでした。";
        return false;
    }
    std::wstring t = DecodeText(bytes);
    size_t nl = t.find(L'\n');
    std::wstring first = t.substr(0, nl);
    std::wstring second = nl == std::wstring::npos ? L"" : t.substr(nl + 1);
    while (!first.empty() && (first.back() == L'\r' || first.back() == L' ')) first.pop_back();
    while (!second.empty() && (second.back() == L'\r' || second.back() == L'\n')) second.pop_back();
    message = second;
    return first == L"OK";
}

// 引数に入れる文字（「"」は使えない。最後の「\」は引数の区切りを壊すので外す）
bool ArgSafe(const std::wstring& v) { return v.find(L'"') == std::wstring::npos && (v.empty() || v.back() != L'\\'); }

void ProfDlgReloadAfterSave(const std::wstring& file) {
    LoadProfiles(file);  // もとの窓の一覧も読み直す（保存したものを選ぶ）
    ApplyProfileToggles();
    InvalidateRect(g.hwnd, nullptr, FALSE);
    g_pd.items = g.profiles;
    g_pd.pendingNew = false;
    int sel = 0;
    for (size_t i = 0; i < g_pd.items.size(); ++i)
        if (_wcsicmp(g_pd.items[i].file.c_str(), file.c_str()) == 0) sel = static_cast<int>(i);
    g_pd.cur = sel;
    ProfDlgFillList();
    if (!g_pd.items.empty()) ProfDlgLoadFields(sel);
}

void ProfDlgSave() {
    if (g_pd.cur < 0) return;
    const ProfileEntry& p = g_pd.items[static_cast<size_t>(g_pd.cur)];
    const std::wstring name = GetText(g_pd.hwnd, IDP_NAME), desc = GetText(g_pd.hwnd, IDP_DESC), color = GetText(g_pd.hwnd, IDP_COLOR);
    if (name.empty()) {
        MessageBoxW(g_pd.hwnd, L"名前を入れてください。", kAppName, MB_OK | MB_ICONINFORMATION);
        return;
    }
    if (!ArgSafe(name) || !ArgSafe(desc) || !ArgSafe(color)) {
        MessageBoxW(g_pd.hwnd, L"名前・説明・背景色には、「\"」と、最後の「\\」は使えません。", kAppName, MB_OK | MB_ICONINFORMATION);
        return;
    }
    std::wstring ids;
    for (size_t k = 0; k < g_pd.cat.size(); ++k)
        if (SendMessageW(g_pd.checks[k], BM_GETCHECK, 0, 0) == BST_CHECKED) ids += (ids.empty() ? L"" : L",") + g_pd.cat[k].id;
    int pdfSel = static_cast<int>(SendDlgItemMessageW(g_pd.hwnd, IDP_PDF, CB_GETCURSEL, 0, 0));
    std::wstring pdf = (pdfSel > 0 && pdfSel < static_cast<int>(g_pd.pdfIndex.size())) ? g_pd.cat[static_cast<size_t>(g_pd.pdfIndex[static_cast<size_t>(pdfSel)])].id : L"";
    const bool officeOn = SendDlgItemMessageW(g_pd.hwnd, IDP_OFFICE, CB_GETCURSEL, 0, 0) == 0;
    const bool adobeOn = SendDlgItemMessageW(g_pd.hwnd, IDP_ADOBE, CB_GETCURSEL, 0, 0) == 0;
    const bool wuStop = SendDlgItemMessageW(g_pd.hwnd, IDP_WU, CB_GETCURSEL, 0, 0) == 0;
    std::wstring args = L"-Base \"" + p.path + L"\" -FileName \"" + p.file + L"\" -Name \"" + name + L"\" -Description \"" + desc + L"\" -Office " +
                        (officeOn ? L"install" : L"skip") + L" -Adobe " + (adobeOn ? L"install" : L"skip") + L" -ExtraApps \"" + ids + L"\" -PdfDefault \"" + pdf +
                        L"\" -WallpaperColor \"" + color + L"\" -WindowsUpdate " + (wuStop ? L"stop" : L"check-only");
    std::wstring msg;
    const bool ok = ProfDlgRunSave(args, msg);
    MessageBoxW(g_pd.hwnd, msg.c_str(), kAppName, MB_OK | (ok ? MB_ICONINFORMATION : MB_ICONWARNING));
    if (ok) ProfDlgReloadAfterSave(p.file);
}

void ProfDlgDelete() {
    if (g_pd.cur < 0) return;
    const ProfileEntry p = g_pd.items[static_cast<size_t>(g_pd.cur)];
    if (g_pd.pendingNew && g_pd.cur + 1 == static_cast<int>(g_pd.items.size())) {  // まだ保存していない
        g_pd.items.pop_back();
        g_pd.pendingNew = false;
        g_pd.cur = 0;
        ProfDlgFillList();
        if (!g_pd.items.empty()) ProfDlgLoadFields(0);
        return;
    }
    if (!p.custom) return;
    std::wstring ask = (p.builtin ? L"「" + p.name + L"」を、アプリに入っている元の内容に戻しますか？" : L"「" + p.name + L"」を削除しますか？（元に戻せません）");
    if (MessageBoxW(g_pd.hwnd, ask.c_str(), kAppName, MB_YESNO | MB_ICONQUESTION | MB_DEFBUTTON2) != IDYES) return;
    std::wstring msg;
    const bool ok = ProfDlgRunSave(L"-Delete -FileName \"" + p.file + L"\"", msg);
    MessageBoxW(g_pd.hwnd, msg.c_str(), kAppName, MB_OK | (ok ? MB_ICONINFORMATION : MB_ICONWARNING));
    if (ok) ProfDlgReloadAfterSave(p.builtin ? p.file : L"standard.json");
}

void ProfDlgNew() {
    if (g_pd.pendingNew) return;
    if (!ProfDlgCanLeave()) return;
    SYSTEMTIME st{};
    GetLocalTime(&st);
    wchar_t file[64];
    swprintf(file, 64, L"custom_%04d%02d%02d_%02d%02d%02d.json", st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
    ProfileEntry p;
    p.file = file;
    p.path = StandardProfilePath();  // 基準の中身から始める
    p.name = L"新しいプロファイル";
    g_pd.items.push_back(p);
    g_pd.pendingNew = true;
    g_pd.cur = static_cast<int>(g_pd.items.size()) - 1;
    ProfDlgFillList();
    ProfDlgLoadFields(g_pd.cur);
    SetFocus(GetDlgItem(g_pd.hwnd, IDP_NAME));
    SendDlgItemMessageW(g_pd.hwnd, IDP_NAME, EM_SETSEL, 0, -1);
}

LRESULT CALLBACK ProfDlgProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_CTLCOLORSTATIC: {
            static HBRUSH paper = CreateSolidBrush(kPaper);
            HDC dc = reinterpret_cast<HDC>(wp);
            SetBkColor(dc, kPaper);
            SetTextColor(dc, GetDlgCtrlID(reinterpret_cast<HWND>(lp)) == IDP_NOTE ? kMuted : kInk);
            return reinterpret_cast<LRESULT>(paper);
        }
        case WM_ERASEBKGND: {
            RECT rc;
            GetClientRect(hwnd, &rc);
            HBRUSH b = CreateSolidBrush(kPaper);
            FillRect(reinterpret_cast<HDC>(wp), &rc, b);
            DeleteObject(b);
            return 1;
        }
        case WM_COMMAND: {
            const int id = LOWORD(wp), code = HIWORD(wp);
            if (id == IDP_LIST && code == LBN_SELCHANGE) {
                int sel = static_cast<int>(SendDlgItemMessageW(hwnd, IDP_LIST, LB_GETCURSEL, 0, 0));
                if (sel == g_pd.cur || sel < 0) return 0;
                if (!ProfDlgCanLeave()) {
                    SendDlgItemMessageW(hwnd, IDP_LIST, LB_SETCURSEL, g_pd.cur, 0);
                    return 0;
                }
                if (g_pd.pendingNew && g_pd.cur + 1 == static_cast<int>(g_pd.items.size())) {  // 保存しなかった新しいものは捨てる
                    g_pd.items.pop_back();
                    g_pd.pendingNew = false;
                    if (sel >= static_cast<int>(g_pd.items.size())) sel = 0;
                    g_pd.cur = sel;
                    ProfDlgFillList();
                }
                ProfDlgLoadFields(sel);
                return 0;
            }
            if (!g_pd.loading && ((code == EN_CHANGE && (id == IDP_NAME || id == IDP_DESC || id == IDP_COLOR)) ||
                                  (code == CBN_SELCHANGE && (id == IDP_OFFICE || id == IDP_ADOBE || id == IDP_PDF || id == IDP_WU)) ||
                                  (code == BN_CLICKED && id >= IDP_APP0 && id < IDP_APP0 + 100))) {
                g_pd.dirty = true;
                return 0;
            }
            if (id == IDP_NEW) ProfDlgNew();
            else if (id == IDP_DELETE) ProfDlgDelete();
            else if (id == IDP_SAVE) ProfDlgSave();
            else if (id == IDP_CLOSE || id == IDCANCEL) SendMessageW(hwnd, WM_CLOSE, 0, 0);
            return 0;
        }
        case WM_CLOSE:
            if (!ProfDlgCanLeave()) return 0;
            EnableWindow(g.hwnd, TRUE);
            DestroyWindow(hwnd);
            SetForegroundWindow(g.hwnd);
            return 0;
        case WM_DESTROY:
            if (g_pd.font) DeleteObject(g_pd.font);
            g_pd = ProfDlg();
            return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

void OpenProfileSettings() {
    if (g_pd.hwnd) {
        SetForegroundWindow(g_pd.hwnd);
        return;
    }
    if (g.running) {
        MessageBoxW(g.hwnd, L"作業の最中は、プロファイルの設定を開けません。", kAppName, MB_OK | MB_ICONINFORMATION);
        return;
    }
    static bool registered = false;
    if (!registered) {
        WNDCLASSEXW wc{};
        wc.cbSize = sizeof(wc);
        wc.lpfnWndProc = ProfDlgProc;
        wc.hInstance = g.inst;
        wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        wc.hIcon = LoadIconW(g.inst, MAKEINTRESOURCEW(kIconId));
        wc.lpszClassName = L"ManabitoProfileSettings";
        RegisterClassExW(&wc);
        registered = true;
    }
    g_pd = ProfDlg();
    g_pd.items = g.profiles;
    g_pd.cat = LoadExtraCatalog();
    const int dpi = static_cast<int>(GetDpiForWindow(g.hwnd));
    auto S = [dpi](int v) { return MulDiv(v, dpi, 96); };
    const int appsH = static_cast<int>(g_pd.cat.size()) * S(28);
    const int W = S(880), H = S(560) + appsH;
    RECT owner;
    GetWindowRect(g.hwnd, &owner);
    HWND hwnd = CreateWindowExW(WS_EX_DLGMODALFRAME, L"ManabitoProfileSettings", L"まなびと講習準備 ― プロファイルの設定",
                                WS_POPUP | WS_CAPTION | WS_SYSMENU, owner.left + S(60), owner.top + S(60), W, H, g.hwnd, nullptr, g.inst, nullptr);
    if (!hwnd) return;
    g_pd.hwnd = hwnd;
    g_pd.font = CreateFontW(-MulDiv(10, dpi, 72), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                            CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Yu Gothic UI");
    auto mk = [&](const wchar_t* cls, const wchar_t* text, DWORD style, int x, int y, int w, int h, int id, DWORD ex = 0) {
        HWND c = CreateWindowExW(ex, cls, text, WS_CHILD | WS_VISIBLE | style, x, y, w, h, hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), g.inst, nullptr);
        SendMessageW(c, WM_SETFONT, reinterpret_cast<WPARAM>(g_pd.font), FALSE);
        return c;
    };
    const int m = S(16), lx = m + S(300), fx = lx + S(170), fw = W - fx - S(40);
    mk(WC_STATICW, L"プロファイル", 0, m, S(12), S(280), S(22), -1);
    mk(WC_LISTBOXW, L"", WS_BORDER | WS_VSCROLL | WS_TABSTOP | LBS_NOTIFY | LBS_NOINTEGRALHEIGHT, m, S(36), S(280), H - S(190), IDP_LIST);
    mk(WC_BUTTONW, L"基準から新しく作る", WS_TABSTOP, m, H - S(146), S(280), S(32), IDP_NEW);
    mk(WC_BUTTONW, L"削除する", WS_TABSTOP, m, H - S(108), S(280), S(32), IDP_DELETE);
    int y = S(36);
    auto row = [&](const wchar_t* label) {
        mk(WC_STATICW, label, 0, lx, y + S(5), S(165), S(22), -1);
    };
    row(L"名前");
    mk(WC_EDITW, L"", WS_BORDER | WS_TABSTOP | ES_AUTOHSCROLL, fx, y, fw, S(28), IDP_NAME);
    y += S(40);
    row(L"説明");
    mk(WC_EDITW, L"", WS_BORDER | WS_TABSTOP | ES_AUTOHSCROLL, fx, y, fw, S(28), IDP_DESC);
    y += S(40);
    row(L"Office");
    HWND co = mk(WC_COMBOBOXW, L"", WS_TABSTOP | CBS_DROPDOWNLIST, fx, y, S(220), S(200), IDP_OFFICE);
    SendMessageW(co, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"入れる"));
    SendMessageW(co, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"入れない"));
    y += S(40);
    row(L"Adobe Creative Cloud");
    HWND ca = mk(WC_COMBOBOXW, L"", WS_TABSTOP | CBS_DROPDOWNLIST, fx, y, S(220), S(200), IDP_ADOBE);
    SendMessageW(ca, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"入れる"));
    SendMessageW(ca, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"入れない"));
    y += S(40);
    row(L"この講座で入れるアプリ");
    for (size_t k = 0; k < g_pd.cat.size(); ++k) {
        g_pd.checks.push_back(mk(WC_BUTTONW, g_pd.cat[k].name.c_str(), WS_TABSTOP | BS_AUTOCHECKBOX, fx, y + static_cast<int>(k) * S(28), fw, S(24),
                                 IDP_APP0 + static_cast<int>(k)));
    }
    if (g_pd.cat.empty()) mk(WC_STATICW, L"（目録 scripts\\extra_apps.txt がありません）", 0, fx, y + S(5), fw, S(22), -1);
    y += std::max(appsH, S(28)) + S(12);
    row(L"PDF を開くアプリ");
    HWND cp = mk(WC_COMBOBOXW, L"", WS_TABSTOP | CBS_DROPDOWNLIST, fx, y, S(320), S(200), IDP_PDF);
    SendMessageW(cp, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"変えない（Windows の既定のまま）"));
    g_pd.pdfIndex.push_back(-1);
    for (size_t k = 0; k < g_pd.cat.size(); ++k) {
        if (!g_pd.cat[k].pdf) continue;
        SendMessageW(cp, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(g_pd.cat[k].name.c_str()));
        g_pd.pdfIndex.push_back(static_cast<int>(k));
    }
    y += S(40);
    row(L"背景色");
    mk(WC_EDITW, L"", WS_BORDER | WS_TABSTOP | ES_AUTOHSCROLL, fx, y, S(120), S(28), IDP_COLOR);
    mk(WC_STATICW, L"#RRGGBB の形（例 #1F3A5F）", 0, fx + S(132), y + S(5), S(260), S(22), IDP_NOTE);
    y += S(40);
    row(L"Windows Update");
    HWND cw = mk(WC_COMBOBOXW, L"", WS_TABSTOP | CBS_DROPDOWNLIST, fx, y, S(320), S(200), IDP_WU);
    SendMessageW(cw, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"自動の更新を止める"));
    SendMessageW(cw, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"変えない（確認だけ）"));
    y += S(48);
    mk(WC_STATICW,
       L"保存すると、管理者の確認が出ます。保存の場所は %ProgramData%\\ManabitoClassSetup\\profiles で、アプリを更新しても消えません。"
       L"ここに無い設定（スリープ・通知・履歴の消し方など）は、元のプロファイルのものを受け継ぎます。",
       0, lx, y, W - lx - S(40), S(66), IDP_NOTE);
    mk(WC_BUTTONW, L"保存する", WS_TABSTOP | BS_DEFPUSHBUTTON, W - S(320), H - S(96), S(130), S(36), IDP_SAVE);
    mk(WC_BUTTONW, L"閉じる", WS_TABSTOP, W - S(180), H - S(96), S(130), S(36), IDP_CLOSE);
    int sel = static_cast<int>(SendMessageW(g.combo, CB_GETCURSEL, 0, 0));
    if (sel < 0 || sel >= static_cast<int>(g_pd.items.size())) sel = 0;
    g_pd.cur = sel;
    ProfDlgFillList();
    if (!g_pd.items.empty()) ProfDlgLoadFields(sel);
    EnableWindow(g.hwnd, FALSE);  // 開いている間は、もとの窓を操作しない
    ShowWindow(hwnd, SW_SHOW);
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

// onlyApps：独自アプリのタブから、選んだアプリだけを入れるとき（Office・Adobe は触らない）
void StartDeploy(bool scheduled, const std::vector<std::wstring>* onlyApps = nullptr) {
    if (g.running) return;
    RefreshInstalled();
    bool office = g.toggleOn[0], adobe = g.toggleOn[1], apps = g.toggleOn[2] && g.appsAvailable;
    bool removeOffice = WillRemove(0), removeAdobe = WillRemove(1);
    const std::wstring extraNames = onlyApps ? std::wstring() : ProfileExtraNames(SelectedProfilePath());  // プロファイルで決めた追加のアプリ
    const bool extras = !extraNames.empty();
    if (onlyApps) {
        office = adobe = removeOffice = removeAdobe = false;
        apps = true;
    }
    if (!office && !adobe && !apps && !removeOffice && !removeAdobe && !extras) {
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
    if (extras) items += L"・この講座で入れるアプリ（" + extraNames + L"）：入っていなければインストール\n";
    if (apps && onlyApps) {
        items += L"・次の独自アプリの最新版の入手と展開（同じ版なら何もしません）\n";
        for (const auto& n : *onlyApps) items += L"　　" + n + L"\n";
    } else if (apps) {
        items += L"・独自アプリの最新版の入手と展開（同じ版なら何もしません）\n";
    }
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
              (adobe ? L"1" : removeAdobe ? L"2" : L"0") + L" -OwnApps " + (apps ? L"1" : L"0") + L" -ExtraApps " + (extras ? L"1" : L"0");
    if (!startArg.empty()) params += L" -StartAt \"" + startArg + L"\"";
    if (onlyApps) {
        std::wstring list;
        for (const auto& n : *onlyApps) list += (list.empty() ? L"" : L"|") + n;
        params += L" -OnlyApps \"" + list + L"\"";
    }
    g.appsDeploy = (onlyApps != nullptr);

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
    if (!ConfirmWindowsUpdate()) return;
    std::wstring script = g.exeDir + L"\\scripts\\oneclick.ps1";
    if (!FileExists(script)) {
        MessageBoxW(g.hwnd, (L"まとめて実行の部品が見つかりません。\n\n" + script).c_str(), kAppName, MB_OK | MB_ICONWARNING);
        return;
    }
    RefreshInstalled();
    bool office = g.toggleOn[0], adobe = g.toggleOn[1], apps = g.toggleOn[2] && g.appsAvailable;
    bool removeOffice = WillRemove(0), removeAdobe = WillRemove(1);
    const std::wstring extraNames = ProfileExtraNames(SelectedProfilePath());  // プロファイルで決めた追加のアプリ
    const bool extras = !extraNames.empty();
    bool needNet = (office && !g.installed[0]) || (adobe && !g.installed[1]) || apps || removeOffice || removeAdobe || extras;
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
    if (extras) { ask += L"　・この講座で入れるアプリ（" + extraNames + L"）：入っていなければインストール\n"; anyApp = true; }
    if (apps) { ask += L"　・独自アプリ：最新版を入手して展開\n"; anyApp = true; }
    if (removeOffice) { ask += L"　・Office：削除（元に戻せません）\n"; anyApp = true; }
    if (removeAdobe) { ask += L"　・Adobe CC：削除（元に戻せません）\n"; anyApp = true; }
    if (!anyApp) ask += L"　・（なし）\n";
    ask += L"3. 前の受講生の履歴とサインインを消す（最近使ったファイル・ダウンロード・IME・ブラウザー・Office や Adobe のサインインなど。元に戻せません）\n\n";
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
              (adobe ? L"1" : removeAdobe ? L"2" : L"0") + L" -OwnApps " + (apps ? L"1" : L"0") + L" -ExtraApps " + (extras ? L"1" : L"0");

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

void AfterDeployForApps();  // 独自アプリのタブ（後で定義）

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
    if (wasOneClick) NotePrintersResult(g.deployTsv);  // まとめて実行（設定をそろえるを含む）の結果
    g.oneClick = false;
    FinishElevated(res, g.deployTsv, g.deployLog, wasOneClick ? t1 : t);
    AfterDeployForApps();
}

// ---------------------------------------------------------------- このアプリ自身の更新（目録 apps.json に載せた新しい版）

std::wstring InstallTarget();
bool IsSameOrUnder(const std::wstring& path, const std::wstring& base);

// 起動時：Program Files に入っているときだけ、目録に新しい版があるかを裏で確かめる
void StartUpdateCheck() {
    if (g.running) return;
    if (!IsSameOrUnder(g.exeDir, InstallTarget())) {  // 入れていない（開発・試験）ときは確かめない
        SetSummaryText(L"この場所（" + g.exeDir + L"）で動いているため、新しい版は確かめていません（Program Files に入れたアプリだけが確かめます）。", kMuted);
        return;
    }
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
    g.updateDecided = true;  // 新しい版なし・確かめられない・更新する・しない、のどれでも判断は済む（更新の案内の間は、窓が無効なので先生の案内は出ない）
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

// ---------------------------------------------------------------- Windows Update の確認（起動時に裏で）

std::wstring WuStatusPath() { return AppDataDir() + L"\\wu_status.txt"; }

void StartWuCheck() {
    std::wstring script = g.exeDir + L"\\scripts\\wu_check.ps1";
    if (!FileExists(script)) return;
    SHCreateDirectoryExW(nullptr, AppDataDir().c_str(), nullptr);
    Job* job = new Job();
    job->cmd = L"\"" + PowerShellPath() + L"\" -NoProfile -NonInteractive -ExecutionPolicy Bypass -File \"" + script + L"\" -OutFile \"" +
               WuStatusPath() + L"\"";
    job->outLog = AppDataDir() + L"\\wu_check.log";
    job->hwnd = g.hwnd;
    job->doneMsg = WM_APP_WUCHECK;
    HANDLE th = CreateThread(nullptr, 0, JobThread, job, 0, nullptr);
    if (th) CloseHandle(th);
    else delete job;
}

std::vector<std::wstring> SplitBarW(const std::wstring& line) {
    std::vector<std::wstring> f;
    size_t a0 = 0;
    for (;;) {
        size_t b0 = line.find(L'|', a0);
        f.push_back(line.substr(a0, b0 == std::wstring::npos ? std::wstring::npos : b0 - a0));
        if (b0 == std::wstring::npos) break;
        a0 = b0 + 1;
    }
    return f;
}

// ［設定をそろえる］［まとめて実行］の前：今日確かめた結果で、まだ入っていない更新があれば知らせる。続けるなら true
bool ConfirmWindowsUpdate() {
    std::string bytes;
    if (!ReadAllBytes(WuStatusPath(), bytes)) return true;
    std::wstring t = DecodeText(bytes);
    std::wstring first = t.substr(0, t.find(L'\n'));
    while (!first.empty() && (first.back() == L'\r' || first.back() == L' ')) first.pop_back();
    std::vector<std::wstring> f = SplitBarW(first);
    if (f.size() < 4 || f[0] != L"done") return true;
    SYSTEMTIME now{};
    GetLocalTime(&now);
    wchar_t today[16];
    wsprintfW(today, L"%04d/%02d/%02d", now.wYear, now.wMonth, now.wDay);
    if (f[1].rfind(today, 0) != 0) return true;  // 今日の結果でなければ聞かない
    int total = _wtoi(f[2].c_str()), sec = _wtoi(f[3].c_str());
    if (total <= 0) return true;
    std::wstring content = L"Windows Update に、まだ入っていない更新が " + std::to_wstring(total) + L" 件あります（うちセキュリティ " +
                           std::to_wstring(sec) + L" 件。" + f[1] + L" に確認）。\n最新の状態にしてからそろえることをおすすめします。";
    TASKDIALOG_BUTTON buttons[] = {
        {7101, L"先に更新する\nWindows Update の画面を開きます。更新して再起動したあと、もう一度実行してください。"},
        {7102, L"このまま続ける"},
    };
    TASKDIALOGCONFIG c{};
    c.cbSize = sizeof(c);
    c.hwndParent = g.hwnd;
    c.hInstance = g.inst;
    c.dwFlags = TDF_USE_COMMAND_LINKS | TDF_ALLOW_DIALOG_CANCELLATION | TDF_POSITION_RELATIVE_TO_WINDOW;
    c.pszWindowTitle = kAppName;
    c.pszMainIcon = TD_WARNING_ICON;
    c.pszMainInstruction = L"Windows が最新の状態ではありません";
    c.pszContent = content.c_str();
    c.cButtons = 2;
    c.pButtons = buttons;
    c.nDefaultButton = 7101;
    int button = 0;
    if (FAILED(TaskDialogIndirect(&c, &button, nullptr, nullptr)))
        button = (MessageBoxW(g.hwnd, (content + L"\n\nこのまま続けますか？").c_str(), kAppName, MB_YESNO | MB_ICONWARNING) == IDYES) ? 7102 : 7101;
    if (button == 7102) return true;
    if (button == 7101) ShellExecuteW(g.hwnd, L"open", L"ms-settings:windowsupdate", nullptr, nullptr, SW_SHOWNORMAL);
    return false;
}

// ---------------------------------------------------------------- 独自アプリのタブ

void ShowTab(int t);

void StartAppsStatus() {
    if (g.appsLoading) return;
    std::wstring script = g.exeDir + L"\\scripts\\apps_status.ps1";
    std::wstring profile = SelectedProfilePath();
    if (!FileExists(script) || profile.empty()) {
        g.appsError = L"一覧を読む部品か、プロファイルが見つかりません。";
        InvalidateRect(g.hwnd, nullptr, FALSE);
        return;
    }
    EnsureLogDir();
    g.appsOut = g.logDir + L"\\独自アプリの一覧_" + Timestamp() + L".txt";
    Job* job = new Job();
    job->cmd = L"\"" + PowerShellPath() + L"\" -NoProfile -NonInteractive -ExecutionPolicy Bypass -File \"" + script + L"\" -ProfilePath \"" +
               profile + L"\" -CurrentVersion " + kVersion + L" -OutFile \"" + g.appsOut + L"\"";
    job->outLog = g.appsOut + L".log";
    job->hwnd = g.hwnd;
    job->doneMsg = WM_APP_APPSTATUS;
    HANDLE th = CreateThread(nullptr, 0, JobThread, job, 0, nullptr);
    if (!th) {
        delete job;
        return;
    }
    CloseHandle(th);
    g.appsLoading = true;
    g.appsError.clear();
    InvalidateRect(g.hwnd, nullptr, FALSE);
}

void FinishAppsStatus() {
    g.appsLoading = false;
    g.appsLoaded = true;
    for (auto& a : g.apps) {
        if (a.check) DestroyWindow(a.check);
        if (a.setup) DestroyWindow(a.setup);
    }
    g.apps.clear();
    std::string bytes;
    if (!ReadAllBytes(g.appsOut, bytes)) {
        g.appsError = L"一覧を読めませんでした。";
    } else {
        std::wstring t = DecodeText(bytes);
        size_t pos = 0;
        while (pos < t.size()) {
            size_t nl = t.find(L'\n', pos);
            std::wstring line = t.substr(pos, nl == std::wstring::npos ? std::wstring::npos : nl - pos);
            pos = nl == std::wstring::npos ? t.size() : nl + 1;
            while (!line.empty() && (line.back() == L'\r' || line.back() == L' ')) line.pop_back();
            std::vector<std::wstring> f = SplitBarW(line);
            if (f.size() >= 2 && f[0] == L"error") g.appsError = f[1];
            if (f.size() < 8 || f[0] != L"app") continue;
            App::AppItem it;
            it.name = f[1];
            it.exe = f[2];
            it.installed = f[3];
            it.latest = f[4];
            it.state = f[5];
            it.url = f[6];
            it.sha = f[7];
            it.self = (_wcsicmp(it.exe.c_str(), L"ManabitoClassSetup.exe") == 0);
            g.apps.push_back(it);
        }
    }
    for (size_t i = 0; i < g.apps.size() && i < 90; ++i) {
        auto& a = g.apps[i];
        a.check = CreateWindowExW(0, WC_BUTTONW, L"", WS_CHILD | WS_TABSTOP | BS_AUTOCHECKBOX, 0, 0, 10, 10, g.hwnd,
                                  reinterpret_cast<HMENU>(static_cast<INT_PTR>(kIdAppCheck + static_cast<int>(i))), g.inst, nullptr);
        a.setup = CreateWindowExW(0, WC_BUTTONW, a.state == L"same" ? L"入れ直す" : L"セットアップ", WS_CHILD | WS_TABSTOP | BS_OWNERDRAW,
                                  0, 0, 10, 10, g.hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kIdAppSetup + static_cast<int>(i))),
                                  g.inst, nullptr);
        SendMessageW(a.setup, WM_SETFONT, reinterpret_cast<WPARAM>(g.font), FALSE);
        if (a.state == L"old" || a.state == L"none") SendMessageW(a.check, BM_SETCHECK, BST_CHECKED, 0);  // 入れるべきものに初めから印
        const bool selfLatest = a.self && a.state != L"old";
        EnableWindow(a.check, !g.running && !selfLatest);
        EnableWindow(a.setup, !g.running && !selfLatest);  // このアプリは、最新なら入れ直さない（更新は起動時にも確かめる）
    }
    ShowTab(g.tab);
}

// 選んだ行をセットアップする（ほかのアプリを先に、このアプリ自身は最後に更新する）
void SetupApps(const std::vector<size_t>& idx) {
    if (g.running) return;
    std::vector<std::wstring> names;
    const App::AppItem* self = nullptr;
    for (size_t i : idx) {
        if (i >= g.apps.size()) continue;
        if (g.apps[i].self) self = &g.apps[i];
        else names.push_back(g.apps[i].name);
    }
    if (names.empty() && !self) {
        MessageBoxW(g.hwnd, L"セットアップするアプリにチェックを付けてください。", kAppName, MB_OK | MB_ICONINFORMATION);
        return;
    }
    g.pendingSelf = App::PendingSelf();
    const bool selfNeeded = self && (self->state == L"old");
    if (self && !selfNeeded && names.empty()) {
        MessageBoxW(g.hwnd, L"まなびと講習準備は、最新の版です。", kAppName, MB_OK | MB_ICONINFORMATION);
        return;
    }
    if (selfNeeded) {
        g.pendingSelf.on = true;
        g.pendingSelf.ver = self->latest;
        g.pendingSelf.url = self->url;
        g.pendingSelf.sha = self->sha;
    }
    if (!names.empty()) {
        StartDeploy(false, &names);
        if (!g.running) g.pendingSelf = App::PendingSelf();  // 取りやめた
        return;
    }
    App::PendingSelf p = g.pendingSelf;
    g.pendingSelf = App::PendingSelf();
    StartSelfUpdate(p.ver, p.url, p.sha);
}

// 準備が終わったとき：独自アプリのタブから始めたものなら一覧を読み直し、このアプリ自身の更新が控えていれば始める
void AfterDeployForApps() {
    if (!g.appsDeploy) return;
    g.appsDeploy = false;
    if (g.pendingSelf.on) {
        App::PendingSelf p = g.pendingSelf;
        g.pendingSelf = App::PendingSelf();
        StartSelfUpdate(p.ver, p.url, p.sha);
        return;
    }
    StartAppsStatus();
}

void ShowTab(int t) {
    g.tab = t;
    const int prep = (t == 0) ? SW_SHOW : SW_HIDE, apps = (t == 1) ? SW_SHOW : SW_HIDE;
    for (HWND h : {g.combo, g.profSet, g.oneclick, g.run, g.apply, g.clean, g.logs, g.tog[0], g.tog[1], g.tog[2], g.runNow, g.runAt, g.timePick, g.list, g.detail})
        if (h) ShowWindow(h, prep);
    for (HWND h : {g.appsAll, g.appsReload})
        if (h) ShowWindow(h, apps);
    for (auto& a : g.apps) {
        ShowWindow(a.check, apps);
        ShowWindow(a.setup, apps);
    }
    for (HWND h : g.tabBtn)
        if (h) InvalidateRect(h, nullptr, FALSE);
    if (t == 1 && !g.appsLoaded && !g.appsLoading) StartAppsStatus();
    Layout();
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
    g.ctrlHeight = S(38);
    MoveWindow(g.share, W - m - S(190), S(14), S(190), S(34), TRUE);  // 右上：配布サーバーになる
    // タブ（タイトルの下）
    MoveWindow(g.tabBtn[0], m, S(56), S(150), S(36), TRUE);
    MoveWindow(g.tabBtn[1], m + S(154), S(56), S(150), S(36), TRUE);
    g.ctrlTop = S(108);
    if (g.tab == 1) {
        // 独自アプリ：見出しの下に 1 アプリ 1 行（チェック・名前と版・［セットアップ］）
        g.appsTop = g.ctrlTop + S(34);
        const int rowH = S(60);
        for (size_t i = 0; i < g.apps.size(); ++i) {
            const int y = g.appsTop + static_cast<int>(i) * rowH;
            MoveWindow(g.apps[i].check, m + S(6), y + S(18), S(24), S(24), TRUE);
            MoveWindow(g.apps[i].setup, W - m - S(150), y + S(11), S(150), S(38), TRUE);
        }
        g.appsButtonsTop = g.appsTop + static_cast<int>(std::max<size_t>(g.apps.size(), 1)) * rowH + S(14);
        MoveWindow(g.appsAll, m, g.appsButtonsTop, S(320), g.ctrlHeight, TRUE);
        MoveWindow(g.appsReload, m + S(332), g.appsButtonsTop, S(160), g.ctrlHeight, TRUE);
        g.summaryTop = g.appsButtonsTop + g.ctrlHeight + S(16);
        g.progressTop = g.summaryTop + S(30);
        g.progressHeight = g.progress.empty() ? 0 : static_cast<int>(g.progress.size()) * S(46) + S(10);
        InvalidateRect(g.hwnd, nullptr, FALSE);
        return;
    }
    int x = m + S(132);
    RECT cr;
    MoveWindow(g.combo, x, g.ctrlTop, S(170), S(300), TRUE);
    GetWindowRect(g.combo, &cr);
    int comboH = cr.bottom - cr.top;
    MoveWindow(g.combo, x, g.ctrlTop + (g.ctrlHeight - comboH) / 2, S(170), S(300), TRUE);
    x += S(170) + S(6);
    MoveWindow(g.profSet, x, g.ctrlTop, S(76), g.ctrlHeight, TRUE);
    x += S(76) + S(16);
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

    // 3 段目：この講座で入れるアプリ（プロファイルで決まる。トグルは置かない）
    g.extrasTop = g.row2Top + g.ctrlHeight + S(8);
    g.summaryTop = g.extrasTop + S(28);
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

// 独自アプリのタブの文字（見出し・各行の名前と版）
void PaintApps(HDC dc, int width) {
    const int m = S(20);
    std::wstring head = L"独自アプリ　―　一覧は GitHub の目録から読んでいます。チェックしたものを、まとめてセットアップできます。";
    if (g.appsLoading) head = L"独自アプリ　―　一覧を読んでいます…";
    DrawTextAt(dc, head, m, g.ctrlTop + S(4), g.fontBold, kInk);
    if (!g.appsError.empty() && g.apps.empty()) {
        DrawTextAt(dc, L"一覧を読めませんでした：" + g.appsError, m, g.appsTop + S(10), g.font, kNg);
        return;
    }
    const int rowH = S(60);
    for (size_t i = 0; i < g.apps.size(); ++i) {
        const auto& a = g.apps[i];
        const int y = g.appsTop + static_cast<int>(i) * rowH;
        RECT sep{m, y + rowH - 1, width - m, y + rowH};
        HBRUSH lb = CreateSolidBrush(kLine);
        FillRect(dc, &sep, lb);
        DeleteObject(lb);
        std::wstring name = a.name + (a.self ? L"（このアプリ）" : L"");
        if (a.self && !IsSameOrUnder(g.exeDir, InstallTarget())) name += L"　※ Program Files 以外で動いているため、自動の更新はしません";
        DrawTextAt(dc, name, m + S(42), y + S(8), g.fontBold, kInk);
        std::wstring info = L"入っている版：" + (a.installed.empty() ? std::wstring(L"入っていない") : L"v" + a.installed) + L"　　最新：v" + a.latest + L"　　";
        COLORREF stateColor = kMuted;
        std::wstring stateText;
        if (a.state == L"old") { stateText = L"新しい版があります"; stateColor = kAccent; }
        else if (a.state == L"none") { stateText = L"入っていません"; stateColor = kAccent; }
        else if (a.state == L"same") { stateText = L"最新です"; stateColor = kOk; }
        else { stateText = L"入っている版のほうが新しい"; }
        int iw = 0;
        DrawTextAt(dc, info, m + S(42), y + S(34), g.fontSmall, kMuted, &iw);
        DrawTextAt(dc, stateText, m + S(42) + iw, y + S(34), g.fontSmall, stateColor);
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
    {
        // タブの下の線
        RECT line{m, S(92), rc.right - m, S(93)};
        HBRUSH lb = CreateSolidBrush(kLine);
        FillRect(dc, &line, lb);
        DeleteObject(lb);
    }
    if (g.tab == 0) {
        DrawTextAt(dc, L"講習プロファイル", m, g.ctrlTop + (g.ctrlHeight - lh) / 2, g.font, kInk);
        DrawTextAt(dc, L"時間のかかる準備", m, g.row2Top + (g.ctrlHeight - lh) / 2, g.font, kInk);
        DrawTextAt(dc, L"この講座のアプリ", m, g.extrasTop + S(2), g.font, kInk);
        DrawTextAt(dc, g.extrasText, m + S(132), g.extrasTop + S(2), g.font, g.extrasText.rfind(L"なし", 0) == 0 ? kMuted : kAccent);
    } else {
        PaintApps(dc, rc.right);
    }

    int x = m;
    for (const auto& seg : g.summary) {
        int sw = 0;
        DrawTextAt(dc, seg.text, x, g.summaryTop, g.fontBold, seg.color, &sw);
        x += sw;
    }
    if (g.tab == 0) DrawTextAt(dc, L"説明", m, g.detailLabelTop, g.font, kMuted);
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
    if (d->CtlID == IDC_TAB0 || d->CtlID == IDC_TAB1) {
        const bool sel = (static_cast<int>(d->CtlID - IDC_TAB0) == g.tab);
        HDC tdc = d->hDC;
        RECT r = d->rcItem;
        HBRUSH paper = CreateSolidBrush(kPaper);
        FillRect(tdc, &r, paper);
        DeleteObject(paper);
        if (sel) {
            RECT bar{r.left, r.bottom - S(3), r.right, r.bottom};
            HBRUSH ab = CreateSolidBrush(kAccent);
            FillRect(tdc, &bar, ab);
            DeleteObject(ab);
        }
        wchar_t label[64];
        GetWindowTextW(d->hwndItem, label, 64);
        SetBkMode(tdc, TRANSPARENT);
        SetTextColor(tdc, sel ? kAccent : kMuted);
        HGDIOBJ old = SelectObject(tdc, sel ? g.fontBold : g.font);
        DrawTextW(tdc, label, -1, &r, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        SelectObject(tdc, old);
        return;
    }
    if (d->CtlID >= IDC_TOG_OFFICE && d->CtlID <= IDC_TOG_APPS) {
        DrawToggle(d, static_cast<int>(d->CtlID - IDC_TOG_OFFICE));
        return;
    }
    const bool primary = (d->CtlID == IDC_ONECLICK || d->CtlID == IDC_APPS_ALL);
    const bool outline = (d->CtlID == IDC_APPS_RELOAD || (d->CtlID >= kIdAppSetup && d->CtlID < kIdAppSetup + 100) ||
                          d->CtlID == IDC_RUN || d->CtlID == IDC_SHARE || d->CtlID == IDC_PROFSET || d->CtlID == IDC_APPLY || d->CtlID == IDC_CLEAN || d->CtlID == IDC_RUN_NOW ||
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
    g.tabBtn[0] = CreateWindowExW(0, WC_BUTTONW, L"講習の準備", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW, 0, 0, 10, 10, hwnd,
                                  reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_TAB0)), g.inst, nullptr);
    g.tabBtn[1] = CreateWindowExW(0, WC_BUTTONW, L"独自アプリ", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW, 0, 0, 10, 10, hwnd,
                                  reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_TAB1)), g.inst, nullptr);
    g.appsAll = CreateWindowExW(0, WC_BUTTONW, L"チェックしたものをまとめてセットアップ", WS_CHILD | WS_TABSTOP | BS_OWNERDRAW, 0, 0, 10, 10,
                                hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_APPS_ALL)), g.inst, nullptr);
    g.appsReload = CreateWindowExW(0, WC_BUTTONW, L"一覧を読み直す", WS_CHILD | WS_TABSTOP | BS_OWNERDRAW, 0, 0, 10, 10, hwnd,
                                   reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_APPS_RELOAD)), g.inst, nullptr);
    g.combo = CreateWindowExW(0, WC_COMBOBOXW, L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_VSCROLL | CBS_DROPDOWNLIST,
                              0, 0, 10, 10, hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_PROFILE)), g.inst,
                              nullptr);
    g.run = CreateWindowExW(0, WC_BUTTONW, L"点検する", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW, 0, 0, 10, 10,
                            hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_RUN)), g.inst, nullptr);
    g.profSet = CreateWindowExW(0, WC_BUTTONW, L"設定…", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW, 0, 0, 10, 10, hwnd,
                                reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_PROFSET)), g.inst, nullptr);
    g.share = CreateWindowExW(0, WC_BUTTONW, L"先生用になる", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW, 0, 0, 10,
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
        L"最近使ったファイル、ダウンロード、Office・メモ帳・IME の履歴、ブラウザー、Office・Adobe などのサインインを消してから閉じます。"
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
            mm->ptMinTrackSize.x = S(1180);  // 1 段目（プロファイル・［設定…］・ボタン 5 つ）が収まる幅
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
            for (const auto& a : g.apps) {
                if (reinterpret_cast<HWND>(lp) == a.check) {
                    static HBRUSH paperBrush = CreateSolidBrush(kPaper);
                    SetBkColor(reinterpret_cast<HDC>(wp), kPaper);
                    return reinterpret_cast<LRESULT>(paperBrush);
                }
            }
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
            if (LOWORD(wp) >= kIdAppSetup && LOWORD(wp) < kIdAppSetup + 100) {
                SetupApps({static_cast<size_t>(LOWORD(wp) - kIdAppSetup)});
                return 0;
            }
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
                case IDC_TAB0:
                case IDC_TAB1:
                    ShowTab(LOWORD(wp) - IDC_TAB0);
                    return 0;
                case IDC_APPS_RELOAD:
                    StartAppsStatus();
                    return 0;
                case IDC_APPS_ALL: {
                    std::vector<size_t> idx;
                    for (size_t i = 0; i < g.apps.size(); ++i)
                        if (SendMessageW(g.apps[i].check, BM_GETCHECK, 0, 0) == BST_CHECKED) idx.push_back(i);
                    SetupApps(idx);
                    return 0;
                }
                case IDC_PROFSET:
                    OpenProfileSettings();
                    return 0;
                case IDC_SHARE:
                    share::SetProfilePath(SelectedProfilePath());
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
            if (wp == kTimerReport) {
                SendReportNow();
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
            g.startTick = GetTickCount();
            TidyLogs();  // 記録のフォルダーが 50 個を超えていたら、新しい 30 個を残して整理する
            SetTimer(hwnd, kTimerReport, 30000, nullptr);  // 先生用への報告（先生用が見つかったときだけ送る）
            ShowUpdateResult();
            CheckSacOnStartup();
            if (!g.running &&
                EnsureInternet(L"アプリの入手やインストールには、インターネットが必要です。Wi-Fi や LAN ケーブルをつないでください。", true) &&
                IsInternetConnected()) {
                StartUpdateCheck();
                StartWuCheck();  // Windows Update に、まだ入っていない更新があるか（裏で 1〜数分）
            }
            if (g.updCheckOut.empty()) g.updateDecided = true;  // 更新の確認をしなかった（Program Files 以外・インターネットなし など）
            return 0;
        case WM_APP_SHARE_FOUND: {
            share::Offer* offer = reinterpret_cast<share::Offer*>(lp);
            if (g.running || g.updating || !StartupSettled() || share::IsServerRunning() || !IsWindowEnabled(hwnd)) {
                share::Release(offer->session);  // 作業中・更新の判断の前などは、あとでもう一度知らせる
                delete offer;
            } else {
                share::HandleOffer(hwnd, offer);
            }
            return 0;
        }
        case WM_APP_REPORTDONE:
            FinishReport(reinterpret_cast<ReportJob*>(lp));
            return 0;
        case WM_APP_PUSHDONE:
            FinishPush(reinterpret_cast<PushJob*>(lp));
            return 0;
        case WM_APP_APPSTATUS: {
            delete reinterpret_cast<JobResult*>(lp);
            FinishAppsStatus();
            return 0;
        }
        case WM_APP_WUCHECK: {
            delete reinterpret_cast<JobResult*>(lp);
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
            if (!g.applying && !g.deploying) ForgetFailedPrinters();  // プリンターを入れる作業の途中だけは消さない（履歴を消している途中は消してよい）
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
    wc.hIcon = LoadIconW(inst, MAKEINTRESOURCEW(kIconId));
    wc.hIconSm = static_cast<HICON>(LoadImageW(inst, MAKEINTRESOURCEW(kIconId), IMAGE_ICON, GetSystemMetrics(SM_CXSMICON),
                                               GetSystemMetrics(SM_CYSMICON), LR_DEFAULTCOLOR));
    wc.lpszClassName = kClassName;
    RegisterClassExW(&wc);
    g.inst = inst;

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
    if (hwnd) {  // タスクバー・Alt+Tab のアイコン（クラスのアイコンに加えて、窓にも直接設定する）
        SendMessageW(hwnd, WM_SETICON, ICON_BIG, reinterpret_cast<LPARAM>(LoadImageW(inst, MAKEINTRESOURCEW(kIconId), IMAGE_ICON, GetSystemMetrics(SM_CXICON), GetSystemMetrics(SM_CYICON), LR_DEFAULTCOLOR)));
        SendMessageW(hwnd, WM_SETICON, ICON_SMALL, reinterpret_cast<LPARAM>(LoadImageW(inst, MAKEINTRESOURCEW(kIconId), IMAGE_ICON, GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON), LR_DEFAULTCOLOR)));
    }
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
        if (g_pd.hwnd && IsDialogMessageW(g_pd.hwnd, &m)) continue;  // プロファイルの設定（Tab で項目を移る）
        if (m.message == WM_KEYDOWN && m.wParam == VK_F5 && !g.running && IsWindowEnabled(hwnd)) {
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
