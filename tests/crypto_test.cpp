// 実行のしかた（x64 Native Tools Command Prompt for VS）:
//   cd /d C:\dev\ManabitoClassSetup
//   cl /nologo /std:c++20 /EHsc /utf-8 /DUNICODE /D_UNICODE /DNOMINMAX /Fe:build\crypto_test.exe tests\crypto_test.cpp /link comctl32.lib shell32.lib ole32.lib oleaut32.lib gdi32.lib user32.lib advapi32.lib ws2_32.lib iphlpapi.lib bcrypt.lib crypt32.lib
//   build\crypto_test.exe
// 本物の鍵・登録には触れない：試験のあいだ LOCALAPPDATA を一時フォルダー（%TEMP%\manabito_crypto_test）に替える。

// share.cpp の署名まわりを、本物の Windows の関数（CNG・DPAPI）で試す
#include "../src/share.cpp"
#include <cstdio>

using namespace share;
static int g_fail = 0;
static void Check(bool ok, const char* what) {
    printf("%s %s\n", ok ? "OK  " : "NG  ", what);
    if (!ok) ++g_fail;
}

int wmain() {
    InitializeCriticalSection(&g_cs);
    // 本物の鍵（teacher_key.bin）と登録（teacher_pin.txt）に触れないよう、一時フォルダーで試す
    wchar_t tmp[MAX_PATH];
    GetTempPathW(MAX_PATH, tmp);
    std::wstring sandbox = std::wstring(tmp) + L"manabito_crypto_test";
    CreateDirectoryW(sandbox.c_str(), nullptr);
    SetEnvironmentVariableW(L"LOCALAPPDATA", sandbox.c_str());
    if (AppDataDir().find(L"manabito_crypto_test") == std::wstring::npos) { printf("一時フォルダーに替えられないため、試験をやめます。\n"); return 2; }
    DeleteFileW((AppDataDir() + L"\\teacher_key.bin").c_str());
    DeleteFileW((AppDataDir() + L"\\teacher_pin.txt").c_str());
    std::wstring err;
    // 1. 鍵を作る・保存する・読み直す
    bool made = LoadOrCreateTeacherKey(err);
    Check(made, "鍵を作って保存できる");
    if (!made) { wprintf(L"  理由: %ls\n", err.c_str()); return 1; }
    std::vector<BYTE> pub1 = g_tkey.pub;
    std::string fp1 = g_tkey.fp;
    Check(ValidPublicBlob(pub1) && fp1.size() == 16, "公開の鍵の形と、印（16 文字）");
    std::string body = "v|13400000000\r\np|教室-複合機|192.168.1.50|raw|9100|Microsoft IPP Class Driver|1\r\n";
    std::vector<BYTE> sig;
    Check(SignWithTeacherKey(body, sig) && sig.size() == 64, "署名できる（64 バイト）");
    Check(VerifySignature(pub1, body, sig), "正しい本文と署名は、確かめに通る");
    std::string tampered = body; tampered[20] ^= 1;
    Check(!VerifySignature(pub1, tampered, sig), "本文を 1 文字変えると、確かめに失敗する");
    std::vector<BYTE> badsig = sig; badsig[10] ^= 1;
    Check(!VerifySignature(pub1, body, badsig), "署名を 1 バイト変えると、確かめに失敗する");
    std::vector<BYTE> shortsig(sig.begin(), sig.begin() + 63);
    Check(!VerifySignature(pub1, body, shortsig), "短い署名は受け付けない");
    // 読み直し（DPAPI で戻す）
    CloseTeacherKey();
    Check(LoadOrCreateTeacherKey(err) && g_tkey.fp == fp1, "保存した鍵を読み直すと、同じ印になる");
    std::vector<BYTE> sig2;
    Check(SignWithTeacherKey(body, sig2) && VerifySignature(pub1, body, sig2), "読み直した鍵の署名も、前の公開の鍵で確かめられる");
    // 3. 別の鍵
    CloseTeacherKey();
    DeleteFileW((AppDataDir() + L"\\teacher_key.bin").c_str());
    Check(LoadOrCreateTeacherKey(err) && g_tkey.fp != fp1, "鍵を作り直すと、印が変わる");
    std::vector<BYTE> sigOther;
    SignWithTeacherKey(body, sigOther);
    Check(!VerifySignature(pub1, body, sigOther), "別の鍵の署名は、前の公開の鍵では確かめに失敗する");
    // 壊れた鍵のファイル：作り直さずに止める
    CloseTeacherKey();
    const char junk[] = "this is not a key";
    WriteFileAtomic(AppDataDir() + L"\\teacher_key.bin", junk, sizeof(junk));
    Check(!LoadOrCreateTeacherKey(err), "壊れた鍵のファイルは読まずに止める（黙って作り直さない）");
    // 4. 登録の保存と読み直し
    Check(SavePin(L"TEACHER|PC", pub1), "登録を保存できる");
    Pin pin;
    Check(LoadPin(pin) && pin.fp == fp1 && pin.pub == pub1 && pin.name == L"TEACHER_PC", "登録を読み直せる（名前の | は _ に）");
    std::string bad = "TEACHER|0123456789abcdef|" + Hex(pub1) + "\r\n";  // 印が鍵と合わない
    WriteFileAtomic(AppDataDir() + L"\\teacher_pin.txt", bad.data(), bad.size());
    Check(!LoadPin(pin), "印と鍵が合わない登録は受け付けない");
    // 5. 16 進数
    std::vector<BYTE> o;
    Check(Unhex("00ff10", o) && o.size() == 3 && o[1] == 0xff, "16 進数を読める");
    Check(!Unhex("0g", o) && !Unhex("abc", o), "16 進数でない・奇数の長さは受け付けない");
    std::vector<BYTE> notpub(72, 0);
    Check(!ValidPublicBlob(notpub), "形の違う公開の鍵は受け付けない");
    // 6. ファイルの SHA-256（自動で配るときの確かめ）。"abc" の SHA-256 はよく知られた値
    std::wstring f = AppDataDir() + L"\\sha_test.txt";
    WriteFileAtomic(f, "abc", 3);
    std::vector<BYTE> h;
    Check(FileSha256(f, h, 3) && Hex(h) == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad", "ファイルの SHA-256 が正しい");
    Check(!FileSha256(f, h, 4), "大きさが違う（途中で変わった）ファイルは確かめに失敗する");
    std::string man = "m|13400000001|1|3\r\nf|abc.txt|3|ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad\r\n";
    CloseTeacherKey();
    DeleteFileW((AppDataDir() + L"\\teacher_key.bin").c_str());
    std::vector<BYTE> msig;
    Check(LoadOrCreateTeacherKey(err) && SignWithTeacherKey(man, msig) && VerifySignature(g_tkey.pub, man, msig), "配るファイルの一覧に署名して確かめられる");
    std::string man2 = man; man2[man2.size() - 3] = '0';
    Check(!VerifySignature(g_tkey.pub, man2, msig), "一覧の SHA-256 を 1 文字変えると、確かめに失敗する");
    printf("\n%d 件の失敗\n", g_fail);
    return g_fail;
}
