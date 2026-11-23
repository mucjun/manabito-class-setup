// まなびと講習準備 ― LAN 内の授業ファイル配布
//
// 配布する側：［配布サーバーになる］→ 配布用の窓にファイル・フォルダー・ドライブをドロップ。4 桁の合言葉を出す。
// 受け取る側：アプリが開いている間、LAN に配布サーバーがないかを探す。見つかったら知らせる（WM を送る）。
//
// 通信：UDP 47811（見つける）・TCP 47812（一覧と中身）。文字は UTF-8、1 行ずつ。
//   受→配（UDP）MNBSHARE?1
//   配→受（UDP）MNBSHARE!1|PC名|TCPの番号|配布の番号|ファイル数|合計バイト
//   受→配（TCP）HELLO|合言葉|PC名     ← OK|配布の番号 ／ NG|理由
//   受→配（TCP）LIST                  ← COUNT|件数|合計  のあと  F|相対パス|大きさ ／ D|相対パス|0  を件数分
//   受→配（TCP）GET|番号              ← DATA|大きさ  のあと中身 ／ ERR|理由
//   受→配（TCP）DONE|できた数|できなかった数
//   インストール元（合言葉なし。時間のかかる準備から使う。中身の署名は受け取る側で確かめる）:
//   配→受（UDP）の答えの続き  |office=版|バイト|adobe=版|バイト   （無ければ 版 は空、バイトは 0）
//   受→配（TCP）OFFICE|PC名 ／ ADOBE|PC名   ← COUNT|件数|合計  のあと  F|Office\…（Adobe\…）|大きさ ／ D|…|0
//   受→配（TCP）OGET|番号 ／ AGET|番号     ← DATA|大きさ  のあと中身

#pragma once
// windows.h より前に winsock2.h が要る share.cpp は、自分で先に読み込む。ここでは windows.h だけ使う。
#include <windows.h>
#include <string>

namespace share {

// 見つけた配布サーバー（受け取る側の窓に、WM で渡す。受け取ったら delete する）
struct Offer {
    std::wstring server;  // PC 名
    std::wstring ip;
    int port = 0;
    unsigned session = 0;
    int files = 0;
    unsigned long long bytes = 0;
};

// アプリの起動時：Winsock を用意し、配布サーバーを探し始める。見つけたら owner に foundMsg（lParam = Offer*）を送る
void Init(HINSTANCE inst, HWND owner, UINT foundMsg);
// アプリの終了時：探すのと配布をやめる
void Shutdown();

// ［配布サーバーになる］。profilePath は Office のインストール元を用意するときの構成に使う（選んでいるプロファイル）
void SetProfilePath(const std::wstring& profilePath);
void OpenServerWindow(HWND owner);
bool IsServerRunning();

// 見つけた配布を知らせ、取得するなら合言葉・受け取るフォルダーを聞いて受け取る（受け取りは裏で進み、窓に進み具合を出す）
void HandleOffer(HWND owner, Offer* offer);
// 今は案内できない（作業中など）：あとでもう一度知らせてよい
void Release(unsigned session);

// 受け取るフォルダー（前回選んだもの。初期値はドキュメント\授業ファイル）
std::wstring DestFolder();

// ---- 先生用（v0.17.0）：受講生の PC からの報告・先生の PC の登録・プリンターの設定
//   受→配（TCP）REPORT|PC名|版|OK数|NG数|対象外数|行数 のあと I|Id|判定|項目|今の状態  ← OK|点検を頼むか|プリンターの版|先生の印|配る番号
//   受→配（TCP）PAIR|合言葉|PC名  ← KEY|公開の鍵（16 進）|先生の PC 名（合言葉が要る。失敗の回数は授業ファイルと同じく制限）
//   受→配（TCP）PRINTERS  ← PRN|版|長さ|署名（16 進）のあと本文（登録した鍵で署名を確かめる）
//   配→受（UDP）の答えの続き  |printers=版|0|teacher=先生の印（16 文字）|0

struct TeacherInfo {
    bool valid = false;   // 30 秒以内に見えている
    bool paired = false;  // この先生の PC を登録している
    std::wstring name, ip;
    int port = 0;
    std::string printersVersion, fingerprint;
};
struct ReportReply {
    bool ok = false, inspect = false;
    std::string printersVersion, fingerprint, pushId;  // pushId：自動で配っているものの番号（無ければ空）
};
struct PushResult {
    int received = 0, failed = 0;
    unsigned long long bytes = 0;
    std::wstring folder;
};

TeacherInfo GetTeacher();
// 報告を送る（ネットワークを使うので、画面のスレッド以外から）
bool SendReport(const TeacherInfo& t, const std::string& payload, ReportReply& reply);
// 先生の PC を登録する（画面のスレッドから。印の確かめと合言葉を聞く）
bool PairInteractive(HWND owner, const TeacherInfo& t, std::wstring& message);
// プリンターの設定を受け取る（画面のスレッド以外から）。登録した鍵で署名を確かめてから printers.txt に書く
bool FetchPrinters(const TeacherInfo& t, std::wstring& message);
// この PC にあるプリンターの設定の版（無ければ空）
std::string LocalPrintersVersion();
// 自動で配られたファイルを受け取る（画面のスレッド以外から）。署名した一覧と SHA-256 が合ったものだけを置く
//   受→配（TCP）PUSHMAN  ← PSH|番号|長さ|署名 のあと一覧（m|番号|個数|合計 のあと f|相対パス|大きさ|SHA-256）
//   受→配（TCP）PUSH|番号|PC名  ← OK|番号、そのあと PGET|i ← DATA|大きさ のあと中身、最後に DONE|できた数|できなかった数
bool FetchPush(const TeacherInfo& t, const std::string& pushId, PushResult& result, std::wstring& message);
// この先生の PC から受け取り終えた、自動で配られたものの番号（無ければ空）
std::string LastPushId(const std::string& fingerprint);

// ---- 先生の PC の講習後の片付け（v0.20.0）
// 先生用の窓の［講習後の片付け］を押すと、本体の窓（Init の owner）にこの知らせを送る。本体が片付けを行う
const UINT kTeacherCleanupMsg = WM_APP + 20;
// 先生用の窓の状態を空にする（授業ファイルの欄・自動で配るもの・受け取った PC の一覧。プリンターの設定と先生の鍵は残す）。画面のスレッドから
void ResetTeacherWork();

}  // namespace share
