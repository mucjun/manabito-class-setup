from playwright.sync_api import sync_playwright
import os
CSS = """
*{box-sizing:border-box} body{margin:0;font-family:'Noto Sans CJK JP',sans-serif;color:#332E29;background:#fff}
.win{border:1px solid #9a9a9a;border-radius:8px;overflow:hidden;box-shadow:0 2px 10px rgba(0,0,0,.18);background:#F7F3EA}
.titlebar{height:30px;background:#fff;border-bottom:1px solid #ddd;display:flex;align-items:center;padding:0 10px;font-size:12px;color:#444;justify-content:space-between}
.tb-btn{letter-spacing:14px;color:#666}
.btn{display:inline-block;font-size:13px;font-weight:700;border-radius:7px;padding:7px 14px;border:1.5px solid #2D5B8A;color:#2D5B8A;background:#fff;white-space:nowrap}
.btn.pri{background:#2D5B8A;color:#fff}
.btn.red{border-color:#B3412E;color:#B3412E}
.btn.gray{border:1px solid #CFC7BA;color:#332E29;font-weight:400}
.hl{outline:3px solid #E0312B;outline-offset:3px;border-radius:6px;position:relative}
.n{position:absolute;top:-14px;right:-14px;width:24px;height:24px;border-radius:12px;background:#E0312B;color:#fff;font-size:13px;font-weight:700;display:flex;align-items:center;justify-content:center;z-index:5}
.nl{position:absolute;top:-14px;left:-14px;width:24px;height:24px;border-radius:12px;background:#E0312B;color:#fff;font-size:13px;font-weight:700;display:flex;align-items:center;justify-content:center;z-index:5}
.muted{color:#6B645C} .tab{padding:6px 18px;font-size:13px;color:#6B645C;border-bottom:3px solid transparent;display:inline-block}
.tab.on{color:#2D5B8A;font-weight:700;border-bottom-color:#2D5B8A}
table{border-collapse:collapse;width:100%;font-size:12px} th{background:#EFE9DE;text-align:left;padding:5px 8px} td{border-top:1px solid #EFE9DE;padding:5px 8px;background:#fff}
.ok{color:#2E7D32;font-weight:700} .ng{color:#B3412E;font-weight:700}
.dlg{background:#fff;border:1px solid #999;border-radius:8px;box-shadow:0 6px 24px rgba(0,0,0,.25);width:520px}
.dlg .hd{padding:10px 14px;font-size:12px;border-bottom:1px solid #eee;color:#444}
.dlg .bd{padding:16px 18px;font-size:13px;line-height:1.7}
.cmdlink{border:1px solid #ddd;border-radius:6px;padding:9px 12px;margin-top:8px;font-size:13px}
.cmdlink b{color:#1a5fb4}
.winset{background:#f3f3f3;font-family:'Noto Sans CJK JP';}
.side{width:170px;background:#eee;padding:10px;font-size:12px}
.side div{padding:6px 8px;border-radius:5px;margin-bottom:2px} .side .on{background:#dcdcdc;font-weight:700}
.card{background:#fff;border:1px solid #e3e3e3;border-radius:6px;padding:10px 14px;margin-bottom:6px;font-size:13px;display:flex;justify-content:space-between;align-items:center}
.toggle{width:38px;height:20px;border-radius:10px;background:#2D5B8A;display:inline-block;position:relative}
.toggle.off{background:#fff;border:1.5px solid #666}
"""
def page(body, w, h):
    return f"<html><head><meta charset='utf-8'><style>{CSS}</style></head><body><div id='c' style='padding:22px;width:{w+44}px;display:inline-block'>{body}</div></body></html>"

FIGS = {}
# ---- スマート アプリ コントロール
FIGS['sac'] = (page("""
<div class='win winset' style='display:flex;height:330px'>
 <div class='side'><div>ホーム</div><div>ウイルスと脅威の防止</div><div>アカウントの保護</div><div>ファイアウォール</div>
  <div class='on hl'><span class='nl'>1</span>アプリとブラウザーの制御</div><div>デバイス セキュリティ</div></div>
 <div style='flex:1;padding:16px 20px;background:#fafafa'>
  <div style='font-size:20px;font-weight:700;margin-bottom:12px'>スマート アプリ コントロール</div>
  <div style='font-size:12px;color:#555;margin-bottom:14px'>信頼できないアプリや、評判の分からないアプリをブロックします。</div>
  <div class='card'><span>オン</span><span style='width:16px;height:16px;border:1.5px solid #666;border-radius:8px'></span></div>
  <div class='card'><span>評価</span><span style='width:16px;height:16px;border:1.5px solid #666;border-radius:8px'></span></div>
  <div class='card hl'><span class='n'>2</span><b>オフ</b><span style='width:16px;height:16px;border:5px solid #2D5B8A;border-radius:8px'></span></div>
 </div></div>""",720,360), 720, 370)
# ---- ダウンロード（Releases）
FIGS['download'] = (page("""
<div class='win' style='background:#fff;height:300px'>
 <div class='titlebar'><span>Releases · mucjun/manabito-class-setup</span><span class='tb-btn'>—▢✕</span></div>
 <div style='padding:8px 14px;font-size:12px;border-bottom:1px solid #eee;color:#555'>https://github.com/mucjun/manabito-class-setup/releases</div>
 <div style='padding:16px 22px'>
  <div style='font-size:22px;font-weight:700'>まなびと講習準備 v0.20.7 <span style='font-size:12px;border:1px solid #2E7D32;color:#2E7D32;border-radius:10px;padding:2px 8px'>Latest</span></div>
  <div style='font-size:14px;font-weight:700;margin:14px 0 8px'>▼ Assets</div>
  <div style='border:1px solid #ddd;border-radius:6px'>
   <div style='padding:9px 12px;border-bottom:1px solid #eee;font-size:13px'><span class='hl' style='padding:2px 4px;color:#1a5fb4;font-weight:700'><span class='n'>1</span>📦 ManabitoClassSetup_v0.20.7.zip</span></div>
   <div style='padding:9px 12px;border-bottom:1px solid #eee;font-size:13px;color:#555'>Source code (zip)</div>
   <div style='padding:9px 12px;font-size:13px;color:#555'>Source code (tar.gz)</div>
  </div></div></div>""",720,320), 720, 330)
# ---- すべて展開
FIGS['extract'] = (page("""
<div style='display:flex;gap:26px;align-items:flex-start'>
 <div style='width:150px;text-align:center;font-size:12px'><div style='font-size:52px'>🗜️</div>ManabitoClassSetup_<br>v0.20.7.zip</div>
 <div style='background:#fff;border:1px solid #ccc;border-radius:8px;box-shadow:0 4px 16px rgba(0,0,0,.2);width:260px;padding:6px 0;font-size:13px'>
  <div style='padding:7px 14px'>開く</div><div style='padding:7px 14px'>プログラムから開く</div>
  <div style='padding:7px 14px;margin:0 6px;background:#E8F0F8' class='hl'><span class='n'>1</span><b>すべて展開...</b></div>
  <div style='padding:7px 14px'>パスのコピー</div><div style='padding:7px 14px'>プロパティ</div></div>
 <div class='dlg' style='width:300px'><div class='hd'>圧縮 (ZIP 形式) フォルダーの展開</div>
  <div class='bd' style='font-size:12px'>ファイルを下のフォルダーに展開する：<div style='border:1px solid #ccc;padding:4px;margin:6px 0'>…\\Downloads\\ManabitoClassSetup_v0.20.7</div>
  <div style='text-align:right;margin-top:10px'><span class='btn pri hl'><span class='n'>2</span>展開</span></div></div></div>
</div>""",780,250), 780, 260)
# ---- 入れる
FIGS['install'] = (page("""
<div style='display:flex;gap:24px;align-items:flex-start'>
 <div class='dlg' style='width:410px'><div class='hd'>まなびと講習準備</div><div class='bd'>
  <b>まなびと講習準備を Program Files に入れます</b><br><span style='font-size:12px;color:#555'>入れると、スタートメニューの「まなびとツール」から起動できます。</span>
  <div class='cmdlink hl'><span class='n'>1</span><b>➜ Program Files に入れて起動する（おすすめ）</b></div>
  <div class='cmdlink'><b>➜ この場所のまま使う</b></div></div></div>
 <div class='dlg' style='width:300px;border-top:6px solid #1a5fb4'><div class='bd'>
  <b>ユーザー アカウント制御</b><br>このアプリがデバイスに変更を加えることを許可しますか？<br><span style='font-size:12px;color:#555'>Windows PowerShell</span>
  <div style='display:flex;gap:10px;margin-top:14px'><span class='btn pri hl' style='flex:1;text-align:center'><span class='n'>2</span>はい</span><span class='btn gray' style='flex:1;text-align:center'>いいえ</span></div></div></div>
</div>""",780,250), 780, 260)
# ---- スタートメニュー
FIGS['start'] = (page("""
<div style='width:520px;background:#f2f2f2;border:1px solid #bbb;border-radius:10px;padding:16px 20px;box-shadow:0 6px 24px rgba(0,0,0,.2)'>
 <div style='background:#fff;border-radius:16px;padding:7px 14px;font-size:12px;color:#888;margin-bottom:12px'>🔍 アプリ、設定、ドキュメントの検索</div>
 <div style='font-size:13px;font-weight:700;margin-bottom:6px'><span class='hl' style='padding:1px 4px'><span class='n'>1</span>すべて</span>　<span class='muted' style='font-weight:400;font-size:12px'>ビュー：カテゴリ</span></div>
 <div style='display:grid;grid-template-columns:1fr 1fr;gap:8px;margin-top:8px'>
  <div style='background:#fff;border-radius:8px;padding:10px;font-size:12px'>生産性<br><span class='muted'>Word・Excel・PowerPoint</span></div>
  <div class='hl' style='background:#fff;border-radius:8px;padding:10px;font-size:12px'><span class='n'>2</span><b>まなびとツール</b><br>
   <div style='margin-top:6px;padding:4px 6px;background:#E8F0F8;border-radius:5px' class='hl'><span class='n'>3</span>🖥️ まなびと講習準備</div>
   <div style='margin-top:4px;padding:4px 6px'>✅ まなびとバックアップ</div></div>
  <div style='background:#fff;border-radius:8px;padding:10px;font-size:12px'>ユーティリティとツール</div>
  <div style='background:#fff;border-radius:8px;padding:10px;font-size:12px'>その他</div>
 </div></div>""",580,330), 580, 340)
# ---- 除外：エクスプローラー
FIGS['exclude_explorer'] = (page("""
<div class='win' style='background:#fff;height:250px'>
 <div class='titlebar'><span>ManabitoClassSetup</span><span class='tb-btn'>—▢✕</span></div>
 <div style='padding:8px 12px;border-bottom:1px solid #eee'><div class='hl' style='border:1px solid #bbb;border-radius:4px;padding:5px 8px;font-size:13px'><span class='n'>1</span>C:\\Program Files\\Org_Apps\\ManabitoClassSetup</div></div>
 <div style='padding:10px 16px;font-size:13px'>
  <div style='padding:5px 0'>📁 profiles</div><div style='padding:5px 0'>📁 scripts</div>
  <div style='padding:5px 6px;background:#E8F0F8;display:inline-block' class='hl'><span class='n'>2</span>⚙️ <b>defender_exclude_add.cmd</b></div>
  <div style='padding:5px 0'>⚙️ defender_exclude_remove.cmd</div><div style='padding:5px 0'>🖥️ ManabitoClassSetup.exe</div></div></div>""",720,270), 720, 280)
# ---- 除外：黒い画面
FIGS['exclude_console'] = (page("""
<div style='background:#0c0c0c;color:#ddd;border-radius:6px;padding:14px 16px;font-family:"Noto Sans Mono CJK JP",monospace;font-size:12.5px;line-height:1.7;height:300px'>
<span style='color:#5ed1f0'>まなびと講習準備 ― Windows セキュリティの除外</span><br><br>
次のファイル 1 つを、Windows セキュリティ（Microsoft Defender）が調べない「除外」に加えます。<br>
　 C:\\Program Files\\Org_Apps\\ManabitoClassSetup\\ManabitoClassSetup.exe<br><br>
・このアプリが「トロイの木馬」と誤って判定され、消されるのを防ぐためのものです。<br>
・除外するのはこのファイルだけです。ほかのファイルやフォルダーは、今までどおり調べます。<br><br>
加えますか？（はい＝Y／いいえ＝N）: <span class='hl' style='padding:0 6px;color:#fff;font-weight:700'><span class='n'>1</span>Y</span><br>
<span style='color:#7ee787'>除外に加えました。</span><br><br>Enter キーを押すと閉じます</div>""",760,320), 760, 330)
# ---- メイン画面
FIGS['main'] = (page("""
<div class='win' style='height:520px;border-top:4px solid #2D5B8A'>
 <div style='padding:10px 18px 0;display:flex;justify-content:space-between;align-items:center'>
  <div><span style='font-size:21px;font-weight:700'>まなびと講習準備</span> <span class='muted' style='font-size:11px'>v0.20.7</span></div>
  <span class='btn hl'><span class='n'>9</span>先生用になる</span></div>
 <div style='margin:6px 18px 0;border-bottom:1px solid #DDD5C8'><span class='tab on'>講習の準備</span><span class='tab hl' style='outline-offset:0'><span class='n'>10</span>独自アプリ</span></div>
 <div style='padding:12px 18px 0;display:flex;gap:9px;align-items:center;flex-wrap:wrap'>
  <span style='font-size:13px;width:105px'>講習プロファイル</span>
  <span class='hl' style='border:1px solid #DDD5C8;background:#fff;border-radius:5px;padding:5px 9px;font-size:13px;width:110px'><span class='n'>1</span>事務系 ▾</span>
  <span class='btn hl'><span class='n'>2</span>設定…</span>
  <span class='btn pri hl'><span class='n'>3</span>まとめて実行</span><span class='btn hl'><span class='n'>4</span>点検する</span>
  <span class='btn hl'><span class='n'>5</span>設定をそろえる</span><span class='btn red hl'><span class='n'>6</span>履歴を消す</span><span class='btn gray hl'><span class='n'>7</span>記録のフォルダー</span></div>
 <div style='padding:12px 18px 0;display:flex;gap:9px;align-items:center;font-size:13px'>
  <span style='width:105px'>時間のかかる準備</span>
  <span class='hl' style='display:flex;gap:8px;padding:2px'><span class='n'>8</span><span style='background:#fff;border:1px solid #2D5B8A;border-radius:14px;padding:4px 10px'><span class='toggle' style='width:24px;height:12px;vertical-align:middle'></span> Office</span>
  <span style='background:#fff;border:1px solid #DDD5C8;border-radius:14px;padding:4px 10px;color:#6B645C'>Adobe CC</span>
  <span style='background:#fff;border:1px solid #DDD5C8;border-radius:14px;padding:4px 10px;color:#6B645C'>独自アプリ</span></span>
  <span class='btn'>今すぐ実行</span><span class='btn'>時間を指定して実行</span></div>
 <div style='padding:9px 18px 0;font-size:13px'><span style='width:105px;display:inline-block'>この講座のアプリ</span><span style='color:#2D5B8A'>Adobe Acrobat Reader・Paint.NET ―［今すぐ実行］［まとめて実行］で入れます</span></div>
 <div style='padding:9px 18px 0;font-size:13px;font-weight:700'>点検しました：<span class='ok'>そろっている 44</span>　<span class='ng'>そろっていない 2</span>　<span class='muted'>対象外 6</span></div>
 <div class='hl' style='margin:8px 18px 0;border:1px solid #DDD5C8;border-radius:6px;outline-offset:2px'><span class='n'>11</span>
  <table><tr><th>項目</th><th>今の状態</th><th>講習用</th><th>判定</th></tr>
  <tr><td>自動スリープ</td><td>しない</td><td>しない</td><td class='ok'>そろっている</td></tr>
  <tr><td>Windows の広告・おすすめ・ヒント</td><td>出す（4 か所）</td><td>出さない</td><td class='ng'>そろっていない</td></tr>
  <tr><td>ダウンロード フォルダー</td><td>0 個</td><td>空</td><td class='ok'>そろっている</td></tr></table></div>
 <div style='margin:8px 18px 0;font-size:12px;color:#6B645C'>説明</div>
 <div style='margin:3px 18px 0;background:#fff;border:1px solid #DDD5C8;border-radius:6px;padding:8px 10px;font-size:12px'>行を選ぶと、ここに詳しいことや、手で直す方法が出ます。</div>
</div>""",1000,580), 1000, 590)
# ---- 閉じるとき
FIGS['close'] = (page("""
<div class='dlg' style='width:600px'><div class='hd'>まなびと講習準備</div><div class='bd'><b>まなびと講習準備を終了します</b>
 <div class='cmdlink hl'><span class='n'>1</span><b>➜ 履歴を削除して終了する（講習 PC 用）</b><br><span style='font-size:12px;color:#555'>※ 開発用 PC では実行しないでください</span></div>
 <div class='cmdlink'><b>➜ 履歴を削除せずに終了する（開発 PC 用）</b></div>
 <div style='text-align:right;margin-top:12px'><span class='btn gray'>キャンセル</span></div></div></div>""",660,250), 660, 260)
# ---- 授業ファイルの受け取り
FIGS['receive'] = (page("""
<div style='display:flex;gap:22px;align-items:flex-start'>
 <div class='dlg' style='width:360px'><div class='hd'>まなびと講習準備</div><div class='bd'>先生の PC（TEACHER-PC）が、授業ファイルを配っています。<br>（18 個・24.6 MB）
  <div style='display:flex;gap:10px;margin-top:12px'><span class='btn pri hl'><span class='n'>1</span>取得する</span><span class='btn gray'>今はしない</span></div></div></div>
 <div class='dlg' style='width:330px'><div class='hd'>合言葉</div><div class='bd'>先生の画面に出ている合言葉（4 桁）を入れてください。
  <div class='hl' style='border:1px solid #888;border-radius:4px;padding:6px 10px;margin-top:10px;font-size:18px;letter-spacing:6px'><span class='n'>2</span>4827</div>
  <div style='text-align:right;margin-top:12px'><span class='btn pri'>OK</span></div></div></div></div>""",760,230), 760, 240)
# ---- 登録（印）
FIGS['pair'] = (page("""
<div style='display:flex;gap:22px;align-items:flex-start'>
 <div class='dlg' style='width:380px'><div class='hd'>受講生の PC</div><div class='bd'>先生の PC（TEACHER-PC）を登録します。<br>先生の画面の「この PC の印」が<br>
  <div class='hl' style='font-size:22px;font-weight:700;text-align:center;margin:8px 0;letter-spacing:2px'><span class='n'>1</span>1A2B-3C4D</div>と同じことを確かめてください。<br>同じですか？
  <div style='display:flex;gap:10px;margin-top:10px'><span class='btn pri'>はい</span><span class='btn gray'>いいえ</span></div></div></div>
 <div class='win' style='width:320px;border-top:4px solid #2D5B8A;padding:12px 14px'><div style='font-size:13px;font-weight:700'>先生用（先生の PC の画面）</div>
  <div style='font-size:26px;font-weight:700;color:#2D5B8A;margin-top:6px'>合言葉　4827</div>
  <div class='hl' style='font-size:12px;margin-top:6px;padding:2px 4px'><span class='n'>2</span>この PC：TEACHER-PC　この PC の印：<b>1A2B-3C4D</b></div></div></div>""",760,280), 760, 290)
# ---- 先生用：教室の PC
def tabs(on):
    names=['教室の PC','配る','プリンター']
    return "<div style='margin:6px 16px 0;border-bottom:1px solid #DDD5C8'>"+''.join(f"<span class='tab{' on' if i==on else ''}'>{n}</span>" for i,n in enumerate(names))+"</div>"
HEAD = """<div style='padding:10px 16px 0;display:flex;justify-content:space-between'><div><div style='font-size:16px;font-weight:700'>先生用　―　教室の PC をまとめて見る・配る</div>
 <div style='font-size:11px;color:#6B645C'>この PC：TEACHER-PC　この PC の印：1A2B-3C4D</div></div><div style='font-size:28px;font-weight:700;color:#2D5B8A'>合言葉 4827</div></div>"""
FOOT = """<div style='position:absolute;bottom:10px;left:16px;right:16px;display:flex;justify-content:space-between'><span><span class='btn gray'>講習後の片付け（この PC）</span> <span class='btn gray'>教室の基準を覚え直す</span></span><span class='btn gray'>先生用をやめて閉じる</span></div>"""
FIGS['teacher_pcs'] = (page(f"""
<div class='win' style='height:440px;border-top:4px solid #2D5B8A;position:relative'>{HEAD}{tabs(0)}
 <div style='padding:10px 16px 0;display:flex;justify-content:space-between;font-size:13px;font-weight:700'><span class='hl' style='padding:2px 4px'><span class='n'>1</span>16 台　準備できた 13・要対応 2・応答なし 1</span><span class='btn hl'><span class='n'>2</span>いますぐ点検してもらう</span></div>
 <div class='hl' style='margin:12px 16px 0;border:1px solid #DDD5C8'><span class='nl'>3</span><table><tr><th>PC 名</th><th>準備</th><th>そろっていない項目</th><th>Office</th><th>空き容量</th><th>電池</th><th>最後の報告</th></tr>
 <tr><td>PC-01</td><td class='ok'>準備できた</td><td>なし</td><td>入っている</td><td>182 GB</td><td>92%</td><td>10:31:05</td></tr>
 <tr><td>PC-05</td><td class='ng'>要対応</td><td>Office</td><td>入っていない</td><td>205 GB</td><td>91%</td><td>10:31:02</td></tr>
 <tr><td>PC-06</td><td class='ng'>要対応</td><td>空き容量</td><td>入っている</td><td>7.8 GB</td><td>54%</td><td>10:30:58</td></tr>
 <tr><td>PC-08</td><td class='muted'>応答なし</td><td>―</td><td>―</td><td>―</td><td>―</td><td>10:02:11</td></tr></table></div>{FOOT}</div>""",900,460), 900, 470)
FIGS['teacher_share'] = (page(f"""
<div class='win' style='height:440px;border-top:4px solid #2D5B8A;position:relative'>{HEAD}{tabs(1)}
 <div style='padding:10px 16px 0;font-size:12px'>授業ファイル（ファイル・フォルダーを、ここにドロップしてください）</div>
 <div class='hl' style='margin:6px 16px 0;background:#fff;border:2px dashed #B9AE9E;border-radius:6px;padding:10px;font-size:12px;min-height:60px'><span class='n'>1</span>第3回_Excel関数（18 個・24.6 MB）</div>
 <div style='margin:10px 16px 0;display:flex;gap:8px'><span class='btn gray'>選んだものを外す</span><span class='btn gray'>すべて外す</span><span class='btn hl'><span class='n'>2</span>登録済みの PC に自動で配る</span></div>
 <div style='margin:8px 16px 0;font-size:12px;color:#2D5B8A'>自動で配っています：ファイル 18 個・24.6 MB　受け取り済み 14 台</div>
 <div style='margin:10px 16px 0;display:flex;gap:8px;align-items:center;font-size:12px'><span class='btn hl'><span class='n'>3</span>Office のインストール元を用意する</span><span class='muted'>用意すると、受講生の PC は先生の PC から受け取ります</span></div>
 {FOOT}</div>""",900,460), 900, 470)
FIGS['teacher_printer'] = (page(f"""
<div class='win' style='height:470px;border-top:4px solid #2D5B8A;position:relative'>{HEAD}{tabs(2)}
 <div style='margin:10px 16px 0;border:1px solid #DDD5C8'><table><tr><th>プリンター</th><th>つなぎ方</th><th>ドライバー</th><th>配れるか</th></tr>
 <tr><td>☑ 教室-複合機</td><td>IPP（192.168.1.50:631/ipp/print）・IP アドレスで加えたもの</td><td>Microsoft IPP Class Driver</td><td>配れます</td></tr>
 <tr><td>☐ Microsoft Print to PDF</td><td>―</td><td>―</td><td>Windows に最初から入っています</td></tr></table></div>
 <div style='margin:10px 16px 0;display:flex;gap:8px;align-items:center;font-size:12px'>既定にする：<span class='hl' style='border:1px solid #bbb;background:#fff;padding:4px 8px;border-radius:4px'><span class='n'>4</span>教室-複合機 ▾</span>
  <span class='btn pri hl'><span class='n'>5</span>チェックしたプリンターを配る</span><span class='btn gray'>一覧を読み直す</span></div>
 <div class='hl' style='margin:16px 16px 0;padding:8px;display:flex;gap:8px;align-items:center;font-size:12px;flex-wrap:wrap'><span class='nl'>1</span>IP アドレスで加える：
  <span style='border:1px solid #bbb;background:#fff;padding:4px 8px;width:130px'>教室-複合機</span><span style='border:1px solid #bbb;background:#fff;padding:4px 8px;width:120px'>192.168.1.50</span>
  <span style='border:1px solid #bbb;background:#fff;padding:4px 8px'>631</span><span style='border:1px solid #bbb;background:#fff;padding:4px 8px'>/ipp/print</span>
  <span class='btn hl'><span class='n'>2</span>つながるか確かめる</span><span class='btn hl'><span class='n'>3</span>一覧に加える</span><span class='btn gray'>選んだものを外す</span></div>
 {FOOT}</div>""",900,490), 900, 500)
# ---- プロファイルの設定
FIGS['profile'] = (page("""
<div class='win' style='background:#F7F3EA;height:400px;display:flex;gap:16px;padding:14px'>
 <div style='width:220px'><div style='font-size:12px'>プロファイル</div>
  <div style='background:#fff;border:1px solid #bbb;height:250px;font-size:13px'><div style='padding:5px 8px'>基準</div><div class='hl' style='padding:5px 8px;background:#cfe0f3'><span class='n'>1</span>事務系</div><div style='padding:5px 8px'>WEB系</div></div>
  <div class='btn gray' style='display:block;text-align:center;margin-top:8px'>基準から新しく作る</div><div class='btn gray' style='display:block;text-align:center;margin-top:6px'>削除する</div></div>
 <div style='flex:1;font-size:13px'>
  <div style='display:flex;gap:8px;margin-bottom:8px'><span style='width:150px'>名前</span><span style='border:1px solid #bbb;background:#fff;padding:3px 8px;flex:1'>事務系</span></div>
  <div style='display:flex;gap:8px;margin-bottom:8px'><span style='width:150px'>Office</span><span style='border:1px solid #bbb;background:#fff;padding:3px 8px'>入れる ▾</span></div>
  <div style='display:flex;gap:8px;margin-bottom:8px'><span style='width:150px'>Adobe Creative Cloud</span><span style='border:1px solid #bbb;background:#fff;padding:3px 8px'>入れない ▾</span></div>
  <div class='hl' style='display:flex;gap:8px;margin-bottom:8px;padding:3px'><span class='n'>2</span><span style='width:150px'>この講座で入れるアプリ</span><span>☑ Adobe Acrobat Reader<br>☑ Paint.NET<br>☐ Visual Studio Code</span></div>
  <div style='display:flex;gap:8px;margin-bottom:8px'><span style='width:150px'>PDF を開くアプリ</span><span style='border:1px solid #bbb;background:#fff;padding:3px 8px'>Adobe Acrobat Reader ▾</span></div>
  <div style='display:flex;gap:8px;margin-bottom:8px'><span style='width:150px'>背景色</span><span style='border:1px solid #bbb;background:#fff;padding:3px 8px'>#1F3A5F</span></div>
  <div style='text-align:right;margin-top:16px'><span class='btn pri hl'><span class='n'>3</span>保存する</span> <span class='btn gray'>閉じる</span></div></div></div>""",820,420), 820, 430)
# ---- 更新
FIGS['update'] = (page("""
<div class='dlg' style='width:560px'><div class='hd'>まなびと講習準備</div><div class='bd'><b>新しい版 v0.20.7 があります</b><br><span style='font-size:12px;color:#555'>30 秒後に、自動で更新して起動し直します。</span>
 <div class='cmdlink hl'><span class='n'>1</span><b>➜ 今すぐ更新する</b></div><div class='cmdlink'><b>➜ 今回は更新しない</b></div>
 <div style='font-size:11px;color:#888;margin-top:8px'>残り 24 秒</div></div></div>""",620,260), 620, 270)
# ---- 保護の履歴
FIGS['history'] = (page("""
<div class='win winset' style='display:flex;height:390px'>
 <div class='side'><div>ホーム</div><div class='on hl'><span class='nl'>1</span>ウイルスと脅威の防止</div><div>アカウントの保護</div><div>アプリとブラウザーの制御</div></div>
 <div style='flex:1;padding:14px 20px;background:#fafafa;font-size:13px'>
  <div style='font-size:19px;font-weight:700;margin-bottom:8px'>保護の履歴</div>
  <div class='card hl' style='display:block'><span class='n'>2</span><b>脅威は検疫済み</b>　<span style='color:#B3412E'>重大</span><br><span style='font-size:12px'>区分：トロイの木馬</span>
   <div style='font-size:12px;margin-top:8px'>影響を受けた項目：</div>
   <div class='hl' style='background:#f3f3f3;padding:5px 8px;font-size:12px;margin-top:4px'><span class='n'>3</span>file: C:\\Program Files\\Org_Apps\\ManabitoClassSetup\\ManabitoClassSetup.exe</div>
   <div style='margin-top:10px'><span class='btn gray hl'><span class='n'>4</span>操作 ▾</span>
    <span style='display:inline-block;margin-left:10px;background:#fff;border:1px solid #ccc;border-radius:6px;box-shadow:0 2px 8px rgba(0,0,0,.15)'><span style='display:block;padding:5px 14px'>削除</span><span class='hl' style='display:block;padding:5px 14px;background:#E8F0F8'><span class='n'>5</span><b>復元</b></span></span></div></div>
 </div></div>""",800,410), 800, 420)
# ---- 手で除外
FIGS['exclusion_manual'] = (page("""
<div class='win winset' style='display:flex;height:330px'>
 <div class='side'><div>ホーム</div><div class='on'>ウイルスと脅威の防止</div></div>
 <div style='flex:1;padding:14px 20px;background:#fafafa;font-size:13px'>
  <div style='font-size:19px;font-weight:700;margin-bottom:6px'>除外</div><div style='font-size:12px;color:#555;margin-bottom:12px'>Microsoft Defender ウイルス対策でスキャンしない項目を追加または削除します。</div>
  <span class='btn hl'><span class='n'>1</span>＋ 除外の追加 ▾</span>
  <span style='display:inline-block;vertical-align:top;margin-left:10px;background:#fff;border:1px solid #ccc;border-radius:6px;box-shadow:0 2px 8px rgba(0,0,0,.15);font-size:13px'>
   <span class='hl' style='display:block;padding:6px 16px;background:#E8F0F8'><span class='n'>2</span><b>ファイル</b></span><span style='display:block;padding:6px 16px'>フォルダー</span><span style='display:block;padding:6px 16px'>ファイルの種類</span><span style='display:block;padding:6px 16px'>プロセス</span></span>
  <div class='card' style='margin-top:16px'><span>C:\\Program Files\\Org_Apps\\ManabitoClassSetup\\ManabitoClassSetup.exe<br><span style='font-size:11px;color:#777'>ファイル</span></span><span>⌄</span></div></div></div>""",800,350), 800, 360)

with sync_playwright() as p:
    b = p.chromium.launch()
    for name, (html, w, h) in FIGS.items():
        pg = b.new_page(viewport={'width': w + 44, 'height': h + 44}, device_scale_factor=2)
        pg.set_content(html); pg.wait_for_timeout(200)
        pg.locator('#c').screenshot(path=f'fig/{name}.png')   # 中身の大きさで切り取る
        pg.close()
    b.close()
print('done', len(FIGS))
