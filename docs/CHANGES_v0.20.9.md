# v0.20.9（2026-11-24）［履歴を消す］で Local のサイトも消す

淳さんの指定：Local で作った WordPress のサイト（C:\Users\<利用者>\Local Sites の中身）も、［履歴を消す］で消してよい。

- 点検「Local のサイト（Local Sites）」：中身の数（desktop.ini は数えない）。フォルダーが無ければ対象外。
- ［履歴を消す］「Local のサイト」（ほかのアプリのログインより前）：消す前に、Local の場所（AppData\Roaming\Local・Program Files\Local・
  AppData\Local\Programs\Local・Local Sites）から動いている部品（Local・MySQL・nginx・PHP など）を止める。
  名前だけで止めると XAMPP など別のアプリの同じ名前の部品まで止めるため、動いている場所で見分ける。
- 先生の PC の講習後の片付けでは、消さずに「講習の保管」へ移す（先生の片付けは消さない約束のため）。
- マニュアルの「消すもの」と先生の片付けの表に書き足した。持ち帰るサイトは、講習の最後に Local の「Export」で書き出してもらう。
- 確かめたこと（この検証環境）：点検（前 1 個→後 0 個）、desktop.ini は残す、2 回目は何もしない。
