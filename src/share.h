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

}  // namespace share
