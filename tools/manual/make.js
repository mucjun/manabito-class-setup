const fs = require('fs');
const { ImageRun } = require('docx');
const { imageSize } = (() => { const zlib = null; return { imageSize: (buf) => ({ w: buf.readUInt32BE(16), h: buf.readUInt32BE(20) }) }; })();
const { Document, Packer, Paragraph, TextRun, HeadingLevel, AlignmentType, Table, TableRow, TableCell, WidthType, ShadingType,
        BorderStyle, LevelFormat, Footer, PageNumber, TableOfContents, PageBreak } = require('docx');

const FONT = { ascii: 'Meiryo UI', hAnsi: 'Meiryo UI', eastAsia: 'Meiryo UI', cs: 'Meiryo UI' };
const W = 9638; // A4 本文の幅（DXA）
const NAVY = '1F3A5F', KAKI = 'C4521E';

function runs(text, opts = {}) {
  // **太字** を太字に
  const parts = text.split(/(\*\*[^*]+\*\*)/);
  return parts.filter(p => p).map(p => p.startsWith('**') ? new TextRun({ text: p.slice(2, -2), bold: true, font: FONT, ...opts })
                                                     : new TextRun({ text: p, font: FONT, ...opts }));
}
const P = (t, o = {}) => new Paragraph({ children: runs(t, o.run || {}), spacing: { after: 120, line: 320 }, ...o.para });
const H1 = t => new Paragraph({ heading: HeadingLevel.HEADING_1, children: [new TextRun({ text: t, font: FONT })] });
const H2 = t => new Paragraph({ heading: HeadingLevel.HEADING_2, children: [new TextRun({ text: t, font: FONT })] });
const H3 = t => new Paragraph({ heading: HeadingLevel.HEADING_3, children: [new TextRun({ text: t, font: FONT })] });
const B = t => new Paragraph({ numbering: { reference: 'bullets', level: 0 }, children: runs(t), spacing: { after: 80, line: 300 } });
let stepRef = 0;
function Steps(items) { const ref = 'steps' + (++stepRef); stepRefs.push(ref);
  return items.map(t => new Paragraph({ numbering: { reference: ref, level: 0 }, children: runs(t), spacing: { after: 100, line: 300 } })); }
const stepRefs = [];
const PB = () => new Paragraph({ children: [new PageBreak()] });

// 枠（注意・ポイント）：1 マスの表で背景色つき
function Box(kind, lines) {
  const color = kind === '注意' ? 'FBEDEA' : kind === 'ポイント' ? 'E8F0F8' : 'F3F1EC';
  const edge = kind === '注意' ? KAKI : NAVY;
  const border = { style: BorderStyle.SINGLE, size: 6, color: edge };
  const children = [new Paragraph({ children: [new TextRun({ text: kind === '注意' ? '注意' : kind, bold: true, font: FONT, color: edge })], spacing: { after: 80 } })]
    .concat(lines.map(l => new Paragraph({ children: runs(l), spacing: { after: 60, line: 300 } })));
  return new Table({ width: { size: W, type: WidthType.DXA }, columnWidths: [W],
    rows: [new TableRow({ cantSplit: true, children: [new TableCell({ width: { size: W, type: WidthType.DXA }, children,
      shading: { fill: color, type: ShadingType.CLEAR, color: 'auto' }, margins: { top: 120, bottom: 120, left: 180, right: 180 },
      borders: { top: border, bottom: border, left: { style: BorderStyle.SINGLE, size: 24, color: edge }, right: border } })] })] });
}
const gap = () => new Paragraph({ children: [], spacing: { after: 60 } });
// 図（イメージ）：幅を本文に合わせ、下に説明を付ける
function Img(name, caption, maxW = 600) {
  const data = fs.readFileSync('fig/' + name + '.png');
  const { w, h } = imageSize(data);
  const width = Math.min(maxW, Math.round(w / 2));
  const height = Math.round(width * h / w);
  return [
    new Paragraph({ alignment: AlignmentType.CENTER, keepNext: true, spacing: { before: 120, after: 40 },
      children: [new ImageRun({ type: 'png', data, transformation: { width, height }, altText: { title: caption, description: caption, name } })] }),
    new Paragraph({ alignment: AlignmentType.CENTER, spacing: { after: 200 }, children: [new TextRun({ text: '図　' + caption, font: FONT, size: 18, color: '6B645C' })] }),
  ];
}

// 2 列以上の表
function Tbl(head, rows, widths) {
  const total = widths.reduce((a, b) => a + b, 0);
  const border = { style: BorderStyle.SINGLE, size: 4, color: 'BFB8AC' };
  const borders = { top: border, bottom: border, left: border, right: border };
  const cell = (t, i, isHead) => new TableCell({ width: { size: widths[i], type: WidthType.DXA }, borders,
    shading: isHead ? { fill: 'E6E1D6', type: ShadingType.CLEAR, color: 'auto' } : undefined, margins: { top: 80, bottom: 80, left: 120, right: 120 },
    children: t.split('\n').map(x => new Paragraph({ children: runs(x, isHead ? { bold: true } : {}), spacing: { after: 40, line: 280 } })) });
  return new Table({ width: { size: total, type: WidthType.DXA }, columnWidths: widths,
    rows: [new TableRow({ tableHeader: true, children: head.map((h, i) => cell(h, i, true)) })]
      .concat(rows.map(r => new TableRow({ cantSplit: true, children: r.map((c, i) => cell(c, i, false)) }))) });
}

const body = [];
// ---------------- 表紙
body.push(new Paragraph({ spacing: { before: 2400 }, alignment: AlignmentType.CENTER, children: [new TextRun({ text: 'まなびと講習準備', font: FONT, size: 64, bold: true, color: NAVY })] }));
body.push(new Paragraph({ alignment: AlignmentType.CENTER, spacing: { before: 240 }, children: [new TextRun({ text: '使い方マニュアル', font: FONT, size: 40, color: NAVY })] }));
body.push(new Paragraph({ alignment: AlignmentType.CENTER, spacing: { before: 600 }, children: [new TextRun({ text: '教室・研修の PC を講習用に整え、前の受講生の履歴を消すためのツール', font: FONT, size: 24 })] }));
body.push(new Paragraph({ alignment: AlignmentType.CENTER, spacing: { before: 1800 }, children: [new TextRun({ text: '対象の版：v0.20.9 以降（図入り版）', font: FONT, size: 22 })] }));
body.push(new Paragraph({ alignment: AlignmentType.CENTER, children: [new TextRun({ text: 'NPO 法人まなびとステーション', font: FONT, size: 22 })] }));
body.push(new Paragraph({ alignment: AlignmentType.CENTER, children: [new TextRun({ text: '2026 年 11 月', font: FONT, size: 22 })] }));
body.push(PB());
body.push(new Paragraph({ children: [new TextRun({ text: '目次', font: FONT, size: 32, bold: true, color: NAVY })], spacing: { after: 200 } }));
body.push(new TableOfContents('目次', { hyperlink: true, headingStyleRange: '1-2' }));
body.push(P('※ 目次のページ番号が出ないときは、目次の上で右クリックして［フィールドの更新］を押してください。', { run: { size: 18, color: '6B645C' } }));
body.push(PB());

// ---------------- 1
body.push(H1('1. このツールでできること'));
body.push(P('「まなびと講習準備」は、教室や研修で使う Windows 11 の PC を、講習に向いた状態にそろえるためのツールです。ボタンを押すだけで、次のことができます。'));
[
  '**点検**：PC の設定が講習向きになっているかを一覧で確かめます（変更はしません）。',
  '**設定をそろえる**：スリープしない、通知を出さない、背景の色、Windows Update の自動更新を止める、などを講習向けにそろえます。',
  '**アプリを入れる**：Office、Adobe Creative Cloud、Adobe Acrobat Reader、Paint.NET、Visual Studio Code などを、講座に合わせて入れます。',
  '**履歴を消す**：前の受講生が使った履歴（最近使ったファイル、ブラウザーの履歴、日本語入力の学習、サインインなど）を消します。',
  '**先生用**：先生の PC から、教室の PC の様子をまとめて見たり、授業のファイルやプリンターの設定を配ったりします。',
].forEach(t => body.push(B(t)));
body.push(gap());
body.push(Box('ポイント', ['ふだんは［**まとめて実行**］ボタン 1 つで、「設定をそろえる → アプリを入れる → 履歴を消す」までを順に行います。']));
body.push(gap());
body.push(Box('ポイント', ['このマニュアルの図は、画面のイメージです。文字の大きさや並びなど、細かいところが実際の画面と少し違うことがあります。押す場所は、**赤い枠と番号**で示しています。']));
body.push(gap());
body.push(H2('1-1. 用意するもの'));
[
  'Windows 11 の PC（Pro を想定しています。Home でも使えますが、一部の設定が効かないことがあります）',
  'インターネットへの接続（アプリを入れるとき・ツールを更新するとき）',
  'その PC の管理者の権限（設定を変えるときに「管理者の確認」が出ます）',
].forEach(t => body.push(B(t)));
body.push(PB());

// ---------------- 2
body.push(H1('2. はじめに 1 回だけ行う準備'));
body.push(P('新しい PC で初めて使うときは、次の順番で準備します。2 台目以降も、1 台ごとに同じ準備をします。'));
body.push(H2('2-1. スマート アプリ コントロールをオフにする'));
body.push(P('「スマート アプリ コントロール」は、Windows 11 の機能で、評判の分からないアプリを止めます。このツールは、まなびとステーションで作ったアプリなので、この機能が「オン」だと起動できません。最初にオフにします。'));
body.push(...Steps([
  'スタートボタンを押し、「**Windows セキュリティ**」を開きます。',
  '「**アプリとブラウザーの制御**」を押します。',
  '「**スマート アプリ コントロールの設定**」を押します。',
  '「**オフ**」を選びます。確認が出たら「はい」を押します。',
]));
body.push(...Img('sac', 'スマート アプリ コントロールをオフにする', 520));
body.push(Box('ポイント', ['もともと「オフ」になっている PC では、何もしなくてかまいません。', 'オフにしても、ウイルス対策（Microsoft Defender）は今までどおり働きます。']));
body.push(gap());
body.push(H2('2-2. ツールをダウンロードする'));
body.push(...Steps([
  'インターネットの画面（Edge など）で、次の場所を開きます：https://github.com/mucjun/manabito-class-setup/releases',
  'いちばん上（Latest と書かれた版）の「**ManabitoClassSetup_v（版の数字）.zip**」を押して、ダウンロードします。',
  'ダウンロードしたファイルを右クリックし、［**すべて展開**］→［**展開**］を押します。',
]));
body.push(...Img('download', 'ダウンロードするファイル（いちばん上の版の .zip）', 520));
body.push(...Img('extract', 'ZIP を展開する', 560));
body.push(H2('2-3. ツールを PC に入れる'));
body.push(...Steps([
  '展開したフォルダーの中の「**ManabitoClassSetup.exe**」をダブルクリックします。',
  '「Program Files に入れます」という案内が出たら、［**Program Files に入れて起動する**］を押します。',
  '「このアプリがデバイスに変更を加えることを許可しますか？」（管理者の確認）が出たら、［**はい**］を押します。',
  'ツールが起動します。これで準備は完了です。',
]));
body.push(...Img('install', 'Program Files に入れる案内（左）と管理者の確認（右）', 580));
body.push(Box('ポイント', [
  'ツールは「C:\\Program Files\\Org_Apps\\ManabitoClassSetup」に入ります。',
  '次からは、スタートボタン →［**すべて**］→「**まなびとツール**」→「**まなびと講習準備**」で起動します。受講生が気軽に触らないよう、デスクトップにはショートカットを置きません。',
  'ダウンロード・デスクトップ・ドキュメントの中で展開したフォルダーとダウンロードした ZIP は、入れたあと自動で片付けます。',
]));
body.push(...Img('start', '次からの起動のしかた（スタート →［すべて］→「まなびとツール」）', 420));
body.push(H2('2-4. Windows セキュリティの「除外」に加える（おすすめ）'));
body.push(P('このツールは、まだ広く知られていないアプリで、しかも PC の設定を変えたり、教室の PC どうしで通信したりします。そのため、Windows のウイルス対策が「**トロイの木馬**」などと**誤って判定し、消してしまう**ことがあります。これを防ぐため、このツールだけを、ウイルス対策が調べない「除外」に加えておきます。'));
body.push(...Steps([
  'エクスプローラー（黄色いフォルダーのアイコン）を開きます。',
  '上のアドレス欄に「**C:\\Program Files\\Org_Apps\\ManabitoClassSetup**」と入力して、Enter キーを押します。',
  '「**defender_exclude_add.cmd**」をダブルクリックします。',
  '管理者の確認で［**はい**］を押します。',
  '黒い画面に説明が出ます。内容を読み、「加えますか？」に「**Y**」と入力して Enter キーを押します。',
  '「除外に加えました。」と出たら、Enter キーを押して閉じます。',
]));
body.push(...Img('exclude_explorer', 'アドレス欄にツールの場所を入れ、defender_exclude_add.cmd をダブルクリック', 520));
body.push(...Img('exclude_console', '黒い画面で「Y」を入力して Enter', 560));
body.push(Box('ポイント', [
  '除外するのは、このツールの本体のファイル（ManabitoClassSetup.exe）1 つだけです。ほかのファイルは、今までどおり調べられます。',
  'ツールを更新しても、除外はそのまま効きます。1 台につき 1 回で済みます。',
  '元に戻すときは、同じ場所の「defender_exclude_remove.cmd」をダブルクリックします。',
]));
body.push(gap());
body.push(Box('注意', [
  'このファイル（defender_exclude_add.cmd）自体が、ウイルス対策に止められることがあります。そのときは、「9-3. 手で除外に加える」の手順で加えてください。',
  'これは、まなびとステーションの PC だけで行ってください。よその PC で、知らないアプリを除外に加えてはいけません。',
]));
body.push(PB());

// ---------------- 3
body.push(H1('3. 画面の見かた'));
body.push(P('ツールの画面は、上に 2 つのタブがあります。'));
body.push(Tbl(['場所', '内容'], [
  ['［講習の準備］タブ', 'ふだん使う画面です。点検・まとめて実行・設定をそろえる・履歴を消すのボタンと、点検の結果の一覧があります。'],
  ['［独自アプリ］タブ', 'まなびとステーションで作ったアプリ（まなびとPDFツールなど）を入れたり、更新したりします。'],
  ['右上［先生用になる］', '先生の PC で使います（6 章）。受講生の PC では押しません。'],
], [2600, 7038]));
body.push(gap());
body.push(H2('3-1. ［講習の準備］タブのボタン'));
body.push(...Img('main', 'ツールの画面（［講習の準備］タブ）', 620));
body.push(Tbl(['番号', 'ボタン', 'すること'], [
  ['1', '講習プロファイル', '講座の種類（基準・事務系・WEB系）を選びます。選んだ講座に合わせて、入れるアプリや設定が決まります。'],
  ['2', '［設定…］', '講座の中身（入れるアプリなど）を変えます（7 章）。'],
  ['3', '［まとめて実行］', '「設定をそろえる → アプリを入れる → 履歴を消す」を順に行います。ふだんはこれを押します。'],
  ['4', '［点検する］', '今の PC の状態を調べて、一覧に出します。変更はしません。'],
  ['5', '［設定をそろえる］', '設定だけを講習向きにそろえます。'],
  ['6', '［履歴を消す］', '前の受講生の履歴だけを消します。'],
  ['7', '［記録のフォルダー］', '作業の記録（ログ）が入っているフォルダーを開きます。困ったときに、記録を担当者に送ります。'],
  ['8', '時間のかかる準備', 'Office・Adobe CC・独自アプリのスイッチと、［今すぐ実行］［時間を指定して実行］のボタンです。夜のうちに入れておく、などができます。'],
  ['9', '［先生用になる］', '先生の PC で使います（6 章）。受講生の PC では押しません。'],
  ['10', '［独自アプリ］タブ', 'まなびとステーションで作ったアプリを入れたり、更新したりします。'],
  ['11', '点検の結果の一覧', '項目ごとの「今の状態」と「講習用」、判定が出ます。行を選ぶと、下の「説明」に詳しいことが出ます。'],
], [800, 2400, 6438]));
body.push(gap());
body.push(H2('3-2. 点検の結果の見かた'));
body.push(Tbl(['表示', '意味'], [
  ['そろっている（緑）', '講習向きになっています。'],
  ['そろっていない（赤）', '講習向きになっていません。［まとめて実行］か［設定をそろえる］でそろいます。'],
  ['対象外・参考（灰色）', 'その PC では関係が無い、または確かめるだけの項目です。'],
], [2600, 7038]));
body.push(P('一覧の行を選ぶと、下の「説明」の欄に、詳しいことや手で直す方法が出ます。'));
body.push(PB());

// ---------------- 4
body.push(H1('4. 講習の前（受講生の PC）'));
body.push(H2('4-1. いつもの手順'));
body.push(...Steps([
  'スタートボタン →［すべて］→「まなびとツール」→「**まなびと講習準備**」で起動します。',
  '新しい版があるときは「新しい版があります」と出ます。［**今すぐ更新**］を押します（8 章）。',
  '「講習プロファイル」で、講座の種類を選びます（例：事務系）。',
  '［**まとめて実行**］を押します。確認が出たら［はい］、管理者の確認が出たら［はい］を押します。',
  '終わるまで待ちます。Office などを入れるときは、30 分以上かかることがあります。',
  '終わったら、**PC を再起動**します。再起動のあとに効く設定（PDF を開くアプリ・スタートメニューなど）があるためです。',
  'もう一度起動して［**点検する**］を押し、「そろっていない」が無いかを確かめます。',
]));
body.push(Box('ポイント', [
  'Windows Update の自動更新は止めますが、手で更新はできます。設定 →「Windows Update」→［更新プログラムのチェック］。',
  'まだ入っていない Windows の更新があるときは、［まとめて実行］の前に「先に更新する」を案内します。講習の準備の日に「更新 → 再起動 → まとめて実行」の順にすると、講習中に勝手に再起動しません。',
]));
body.push(gap());
body.push(H2('4-2. 授業のファイルを受け取る'));
body.push(P('先生の PC が授業のファイルを配っていると、受講生の PC のツールに「授業ファイルを取得しますか」と出ます。'));
body.push(...Steps([
  '［**取得する**］を押します。',
  '先生の画面に出ている「**合言葉**」（4 桁の数字）を入れます。',
  'ファイルは「ドキュメント」の「**授業ファイル**」フォルダーに入ります。',
]));
body.push(...Img('receive', '授業ファイルを取得する（左）と合言葉を入れる（右）', 560));
body.push(P('先生が「登録済みの PC に自動で配る」を使っている場合は、何もしなくても 30 秒ほどで届き、「先生からファイルが届きました」と出ます（6 章）。'));
body.push(H2('4-3. 先生の PC を登録する（プリンターの設定などを受け取るため）'));
body.push(P('先生の PC がプリンターの設定を配っていると、「先生の PC を登録しますか」と出ることがあります。登録は 1 台につき 1 回です。'));
body.push(...Steps([
  '［はい］を押します。',
  '画面に出る「**印**」（例：1A2B-3C4D）が、先生の画面の「この PC の印」と同じかを確かめます。同じなら［はい］を押します。',
  '先生の画面の合言葉を入れます。',
]));
body.push(...Img('pair', '受講生の画面の印（左）が、先生の画面の印（右）と同じか確かめる', 560));
body.push(Box('注意', ['印が先生の画面と違うときは、［いいえ］を押して先生に知らせてください。別の PC が先生のふりをしている可能性があります。']));
body.push(PB());

// ---------------- 5
body.push(H1('5. 講習の後（受講生の PC）'));
body.push(P('次の受講生のために、前の受講生の履歴を消します。'));
body.push(H2('5-1. 閉じるときに消す'));
body.push(...Steps([
  'ツールの右上の［×］で閉じます。',
  '「**履歴を削除して終了する（講習 PC 用）**」を押します。',
  '管理者の確認で［はい］を押します。消し終わると、ツールが閉じます。',
]));
body.push(...Img('close', '閉じるときの選択（受講生の PC は「履歴を削除して終了する」）', 480));
body.push(H2('5-2. 消すもの'));
[
  '最近使ったファイル、エクスプローラーの「よく使うフォルダー」（ピン留めは残します）',
  'ダウンロード・デスクトップのファイル（ショートカットは残します）、ごみ箱、受け取った授業ファイル',
  'Edge・Chrome の履歴・お気に入り・保存したパスワード（初めの状態に戻します）',
  '日本語入力（IME）の学習と、ユーザー辞書',
  'Microsoft アカウント・Office・Adobe などのサインイン、保存されたログイン情報（NAS などネットワークの情報は残します）',
  'クリップボードの履歴など',
  'Local（WordPress）で作ったサイト（「Local Sites」フォルダーの中身）。持ち帰るサイトは、講習の最後に Local の「Export」で書き出してもらいます',
].forEach(t => body.push(B(t)));
body.push(gap());
body.push(Box('注意', [
  '消したものは**元に戻せません**。受講生が自分のファイルを持ち帰ったかを、先に確かめてください。',
  '**先生の PC や、事務作業に使う PC では、「履歴を削除して終了する」を押さないでください**。その場合は「履歴を削除せずに終了する（開発 PC 用）」を押します。先生の PC は 6-5 の「講習後の片付け」を使います。',
]));
body.push(PB());

// ---------------- 6
body.push(H1('6. 先生用（先生の PC）'));
body.push(P('先生の PC でツールを起動し、右上の［**先生用になる**］を押すと、先生用の窓が開きます。上に「**合言葉**」（4 桁）と「**この PC の印**」が出ます。受講生に伝えるときは、この画面を見せます。'));
body.push(Box('注意', ['先生の PC と受講生の PC は、同じネットワーク（同じ Wi-Fi・同じ有線 LAN）につないでください。ネットワークが「パブリック」になっていると、受講生の PC から見えないことがあります。案内が出たら［プライベートに切り替えて始める］を押します。']));
body.push(gap());
body.push(H2('6-1. ［教室の PC］タブ'));
body.push(P('受講生の PC（ツールを開いているもの）の様子が、30 秒ごとに一覧に出ます。「準備できた」「要対応」「応答なし」の数と、PC ごとの「そろっていない項目」「空き容量」「時刻のずれ」「電池」がわかります。行を選ぶと、下に詳しい様子が出ます。'));
body.push(P('［**いますぐ点検してもらう**］を押すと、次の報告のときに、受講生の PC が点検をし直します。'));
body.push(...Img('teacher_pcs', '先生用の窓の［教室の PC］タブ（1 集計、2 いますぐ点検してもらう、3 PC ごとの一覧）', 600));
body.push(H2('6-2. ［配る］タブ（授業のファイル）'));
body.push(...Steps([
  '配りたいファイルやフォルダーを、「授業ファイル」の欄に**ドラッグ**します。',
  '受講生の PC で［取得する］を押し、合言葉を入れてもらいます（4-2）。',
]));
body.push(P('先生の PC を登録した受講生の PC には、［**登録済みの PC に自動で配る**］で、受講生の操作なしに配れます。届いた台数は［配る］タブに出ます。'));
body.push(...Img('teacher_share', '［配る］タブ（1 授業ファイルの欄、2 自動で配る、3 インストール元）', 600));
body.push(Box('注意', ['「自動で配る」は、合言葉なしで受け取れる配り方です（中身が本物かどうかは確かめます）。**人に見られて困るファイルは、合言葉で受け取る配り方**（上の 1・2）で配ってください。']));
body.push(gap());
body.push(P('同じタブの「インストール元」で、Office や Creative Cloud のインストール用のファイルを先生の PC に用意すると、受講生の PC はインターネットではなく先生の PC から受け取るので、早く入ります。'));
body.push(H2('6-3. ［プリンター］タブ'));
body.push(P('教室のプリンターの設定を、受講生の PC に配ります。'));
body.push(...Steps([
  '「**IP アドレスで加える**」の欄に、プリンターの名前（例：教室-複合機）と IP アドレス（例：192.168.1.50）を入れます。ポート（631）とパス（/ipp/print）は、ふつうはそのままです。',
  '［**つながるか確かめる**］を押し、「つながりました」と出ることを確かめます。',
  '［**一覧に加える**］を押します。',
  '「既定にする」で、ふだん使うプリンターを選びます。',
  '［**チェックしたプリンターを配る**］を押します。',
  '受講生の PC で、先生の PC を登録（4-3）したあと、［設定をそろえる］か［まとめて実行］を押すと、プリンターが入ります。',
]));
body.push(...Img('teacher_printer', '［プリンター］タブ（番号は手順の順）', 600));
body.push(Box('ポイント', ['IP アドレスで加えたプリンターは、Windows に入っている標準のドライバーを使うので、メーカーのドライバーが無い PC にも入ります。', '入らなかった PC は、ツールを閉じるときに、その設定を片付けます（何度も入れようとしないように）。']));
body.push(gap());
body.push(H2('6-4. 先生用をやめる'));
body.push(P('右下の［**先生用をやめて閉じる**］を押します。配るのも止まります。'));
body.push(H2('6-5. 講習後の片付け（先生の PC）'));
body.push(P('複数の先生が同じ PC を使う場合に、次の講習のために先生の PC を片付けます。先生用の窓の左下の［**講習後の片付け（この PC）**］を押します。'));
body.push(Tbl(['すること', '内容'], [
  ['ファイルを移す', 'デスクトップ（ショートカット以外）・ダウンロード・ドキュメント・スクリーンショット・画面の録画・Local のサイトを、「ドキュメント\\講習の保管\\日時」に移します。**消しません**。'],
  ['サインアウトと履歴', '受講生の PC と同じように消します。この PC のサインインと、NAS などネットワークの情報は残します。'],
  ['元に戻す', '画面を「複製」に、音の出口を PC のスピーカーに、既定のプリンターを［プリンター］で選んだものに戻します。'],
  ['講師の持ち物の名残', 'クリップボードの履歴を消し、教室の基準に無い Wi-Fi の接続先と Bluetooth 機器（講師のスマホなど）を外します。'],
  ['先生用の窓', '授業ファイルの欄と、自動で配るものを空にします（プリンターの設定は残します）。'],
], [2600, 7038]));
body.push(gap());
body.push(Box('注意', [
  'Word・Excel・PowerPoint・Outlook・Paint.NET が開いていると、止まります。保存してから閉じて、もう一度押してください。',
  '**初めての片付けでは**、Wi-Fi と Bluetooth は、そのときの状態を「教室の基準」として覚えるだけで、何も消しません。講師の持ち物をつなぐ前（教室の機器だけの状態）で、1 回目の片付けをしてください。',
  '教室の Wi-Fi や Bluetooth 機器を入れ替えたときは、［**教室の基準を覚え直す**］を押します。',
  'Office を講師の Microsoft アカウントで使っていた場合、次の講師がサインインし直します。',
]));
body.push(PB());

// ---------------- 7
body.push(H1('7. プロファイル（講座ごとの設定）'));
body.push(P('「講習プロファイル」は、講座ごとに「入れるアプリ」や「設定」をまとめたものです。初めから次の 3 つがあります。'));
body.push(Tbl(['プロファイル', '入れるもの', 'PDF を開くアプリ'], [
  ['基準', 'Office', '変えない'],
  ['事務系', 'Office・Adobe Acrobat Reader・Paint.NET', 'Adobe Acrobat Reader'],
  ['WEB系', 'Office・Adobe Creative Cloud・Adobe Acrobat Reader・Visual Studio Code・Figma・Local', 'Adobe Acrobat Reader'],
], [1800, 5238, 2600]));
body.push(gap());
body.push(Box('ポイント', [
  '**Figma** は、使うときに Figma のアカウントでサインインします（無料のアカウントで使えます）。PC 全体に入れる版なので、自動では更新されません。講座の前に「Figma を新しくしてください」と出たら、担当者に知らせてください。',
  '**Local**（WordPress を PC の中で動かすアプリ）は、サイトを作るときに管理者の確認が出ます。［はい］を押してください。',
  '［履歴を消す］で、Figma と Local のサインインと設定も消します。',
]));
body.push(gap());
body.push(H2('7-1. プロファイルを変える・作る'));
body.push(...Steps([
  '「講習プロファイル」の横の［**設定…**］を押します。',
  '左の一覧から変えたいものを選ぶか、［**基準から新しく作る**］を押します。',
  '名前・入れるアプリ（チェック）・PDF を開くアプリ・背景色などを選びます。',
  '［**保存する**］を押し、管理者の確認で［はい］を押します。',
]));
body.push(...Img('profile', 'プロファイルの設定の窓', 560));
body.push(P('初めからあるプロファイルを変えた場合は、［**元に戻す**］で元の内容に戻せます。保存したプロファイルは、ツールを更新しても消えません。'));
body.push(PB());

// ---------------- 8
body.push(H1('8. ツールの更新'));
body.push(P('ツールを起動したとき、インターネットに新しい版があると「**新しい版があります**」と出ます。30 秒たつと自動で更新が始まります。'));
body.push(...Steps([
  '［**今すぐ更新**］を押します（または 30 秒待ちます）。',
  '管理者の確認で［はい］を押します。',
  'ツールが一度閉じて、新しい版で起動し直します。',
]));
body.push(...Img('update', '新しい版の案内', 440));
body.push(Box('ポイント', [
  '更新しただけでは、設定は変わりません。新しい版で増えた設定（スタートメニューなど）を使うときは、更新のあとに［**設定をそろえる**］か［**まとめて実行**］を押し、PC を再起動してください。',
  '講習の途中など、今は更新したくないときは［今回は更新しない］を押します。次に起動したときに、また案内が出ます。',
]));
body.push(PB());

// ---------------- 9
body.push(H1('9. Windows セキュリティの対策'));
body.push(H2('9-1. なぜウイルスと間違えられるのか'));
body.push(P('このツールは、まなびとステーションで作ったもので、次のような特徴があります。'));
[
  '広く使われているアプリではないので、Windows が「評判」を知らない。',
  'アプリに「電子署名」（作った会社を証明する印）が付いていない。',
  'PC の設定を変える、教室の PC どうしで通信する、インターネットからファイルを入手して入れる、などの動きをする。',
].forEach(t => body.push(B(t)));
body.push(P('これらの組み合わせが、悪いプログラム（遠隔操作をするウイルスなど）の動きと似ているため、ウイルス対策（Microsoft Defender）が「**トロイの木馬**」などと**誤って判定する**ことがあります。ツールが悪いことをしているわけではありません。'));
body.push(H2('9-2. 事前の準備（おすすめ）'));
body.push(Tbl(['準備', '理由', '手順'], [
  ['スマート アプリ コントロールをオフ', 'オンのままだと、ツールが起動できない', '2-1'],
  ['Windows セキュリティの除外に加える', '誤って判定されて、消されるのを防ぐ', '2-4（できないときは 9-3）'],
], [2800, 3838, 3000]));
body.push(gap());
body.push(H2('9-3. 手で除外に加える'));
body.push(P('defender_exclude_add.cmd が使えないときは、Windows セキュリティの画面から加えます。'));
body.push(...Steps([
  'スタートボタン →「**Windows セキュリティ**」を開きます。',
  '「**ウイルスと脅威の防止**」を押します。',
  '「ウイルスと脅威の防止の設定」の「**設定の管理**」を押します。',
  '下のほうの「除外」の「**除外の追加または削除**」を押します。管理者の確認で［はい］を押します。',
  '［**除外の追加**］→「**ファイル**」を押します。',
  '「C:\\Program Files\\Org_Apps\\ManabitoClassSetup」を開き、「**ManabitoClassSetup.exe**」を選んで［開く］を押します。',
]));
body.push(...Img('exclusion_manual', '［除外の追加］→「ファイル」を選ぶ。加えると下に表示される', 560));
body.push(Box('注意', ['「フォルダー」ではなく、「ファイル」を選んでください。フォルダーごと除外すると、調べない範囲が広くなりすぎます。']));
body.push(gap());
body.push(H2('9-4. ツールが消されてしまったとき'));
body.push(P('次のようなことが起きたら、ウイルス対策に消された可能性があります。'));
[
  '右下に「脅威が見つかりました」「トロイの木馬」などの知らせが出た。',
  'スタートメニューから起動しようとしても、何も起きない。「ファイルが見つかりません」と出る。',
].forEach(t => body.push(B(t)));
body.push(H3('手順 1：消されたものを元に戻す（復元）'));
body.push(...Steps([
  'スタートボタン →「**Windows セキュリティ**」→「**ウイルスと脅威の防止**」を開きます。',
  '「**保護の履歴**」を押します。',
  'いちばん上の項目（「脅威は検疫済み」「トロイの木馬」など）を押して開きます。',
  '「影響を受けた項目」に「**C:\\Program Files\\Org_Apps\\ManabitoClassSetup\\ManabitoClassSetup.exe**」があることを確かめます。',
  '［**操作**］→［**復元**］を押します。管理者の確認で［はい］を押します。',
]));
body.push(...Img('history', '保護の履歴から復元する（3 の場所が、このツールであることを必ず確かめる）', 560));
body.push(Box('注意', [
  '「影響を受けた項目」が、このツールの場所（C:\\Program Files\\Org_Apps\\ManabitoClassSetup）以外のときは、**復元しないでください**。本当のウイルスの可能性があります。担当者に知らせてください。',
  '「影響を受けた項目」に、スタートメニューの「まなびと講習準備.lnk」（ショートカット）が一緒に出ることがあります。これはツールを指しているだけなので、一緒に復元してかまいません。',
]));
body.push(gap());
body.push(H3('手順 2：もう消されないように、除外に加える'));
body.push(P('復元したら、すぐに 2-4（または 9-3）の手順で除外に加えます。加えないと、また消されることがあります。'));
body.push(H3('手順 3：担当者に知らせる'));
body.push(P('「保護の履歴」の画面の写真（検出された名前・影響を受けた項目が見えるもの）を撮って、担当者に送ってください。担当者は、Microsoft に「誤った判定です」と届け出ます。届け出が認められると、ほかの PC でも判定されなくなります。'));
body.push(H2('9-5. やってはいけないこと'));
[
  'ウイルス対策（Microsoft Defender）そのものを、オフにしないでください。',
  'フォルダーやドライブごと、除外に加えないでください。',
  'このツール以外の、知らないアプリを除外に加えないでください。',
  '「影響を受けた項目」をよく確かめずに、復元しないでください。',
].forEach(t => body.push(B(t)));
body.push(PB());

// ---------------- 10
body.push(H1('10. 困ったときは'));
body.push(Tbl(['こんなとき', 'すること'], [
  ['ツールが起動しない', 'スマート アプリ コントロールがオフか確かめる（2-1）。消されていないか確かめる（9-4）。'],
  ['「トロイの木馬」と出た', '9-4 の手順で、復元 → 除外に加える → 担当者に知らせる。'],
  ['管理者の確認が出ない・押せない', 'その PC の管理者のアカウントでサインインしているか、担当者に確かめる。'],
  ['［まとめて実行］で「うまくいかなかった項目」が出た', '一覧のその行を選び、下の「説明」を読む。手で直す方法が書いてある。わからないときは［記録のフォルダー］の中身を担当者に送る。'],
  ['点検で「再起動のあとに効きます」の項目がそろわない', 'PC を再起動してから、もう一度［点検する］。'],
  ['Office のインストールが終わらない', 'インターネットの速さによっては 1 時間ほどかかる。画面の進み具合が動いていれば待つ。'],
  ['授業ファイルが届かない', '先生の PC と同じネットワークか確かめる。先生の PC のネットワークが「プライベート」か確かめる。受講生の PC でツールを開いているか確かめる。'],
  ['プリンターが入らない', '先生の［プリンター］タブで［つながるか確かめる］。受講生の PC が先生の PC を登録しているか確かめる（4-3）。'],
  ['先生の PC で「履歴を削除して終了」を押してしまった', '消したものは戻せない。ブラウザーやアプリにサインインし直す。'],
  ['更新の案内が出ない', 'ツールを Program Files に入れて使っているか確かめる（2-3）。インターネットにつながっているか確かめる。'],
], [3600, 6038]));
body.push(gap());
body.push(H2('10-1. 担当者に送るもの'));
body.push(...Steps([
  'ツールの［**記録のフォルダー**］を押します。',
  '日付と時刻が新しいファイル（「まとめて実行_」「点検_」などで始まるもの）を、メールなどで送ります。',
  '画面にエラーが出ていたら、その画面の写真も送ります。',
]));
body.push(PB());

// ---------------- 11
body.push(H1('11. 付録'));
body.push(H2('11-1. 記録のフォルダー'));
body.push(P('作業の記録は「C:\\Users\\（利用者名）\\AppData\\Local\\ManabitoClassSetup\\logs」に入ります。ファイルが 50 個を超えると、ツールを起動したときに、古いものから整理して新しい 30 個を残します。'));
body.push(H2('11-2. 言葉の説明'));
body.push(Tbl(['言葉', '意味'], [
  ['管理者の確認（UAC）', '「このアプリがデバイスに変更を加えることを許可しますか？」という確認の画面。設定を変えるときに出ます。'],
  ['プロファイル', '講座ごとの「入れるアプリ」と「設定」のまとまり。'],
  ['スマート アプリ コントロール', '評判の分からないアプリを止める、Windows 11 の機能。'],
  ['Microsoft Defender', 'Windows に入っているウイルス対策。「Windows セキュリティ」の画面で設定します。'],
  ['除外', 'ウイルス対策が調べない場所やファイル。'],
  ['合言葉', '先生用の窓に出る 4 桁の数字。授業ファイルの受け取りや、先生の PC の登録に使います。'],
  ['この PC の印', '先生の PC を見分けるための記号（例：1A2B-3C4D）。登録のときに、受講生の画面の印と同じか確かめます。'],
  ['IP アドレス', 'ネットワークの中の住所のような番号（例：192.168.1.50）。プリンターの設定画面などで確かめられます。'],
  ['Program Files', 'アプリを入れておく、Windows の決まった場所（C:\\Program Files）。'],
], [3000, 6638]));

const numbering = { config: [{ reference: 'bullets', levels: [{ level: 0, format: LevelFormat.BULLET, text: '・', alignment: AlignmentType.LEFT,
    style: { paragraph: { indent: { left: 480, hanging: 300 } }, run: { font: FONT } } }] }]
  .concat(stepRefs.map(ref => ({ reference: ref, levels: [{ level: 0, format: LevelFormat.DECIMAL, text: '%1.', alignment: AlignmentType.LEFT,
    style: { paragraph: { indent: { left: 520, hanging: 360 } }, run: { font: FONT, bold: true, color: NAVY } } }] }))) };

const doc = new Document({
  features: { updateFields: true },  // 開いたときに目次を更新する
  creator: 'NPO 法人まなびとステーション', title: 'まなびと講習準備 使い方マニュアル',
  styles: {
    default: { document: { run: { font: FONT, size: 21 } } },
    paragraphStyles: [
      { id: 'Heading1', name: 'Heading 1', basedOn: 'Normal', next: 'Normal', quickFormat: true,
        run: { font: FONT, size: 34, bold: true, color: NAVY }, paragraph: { spacing: { before: 120, after: 200 }, keepNext: true, outlineLevel: 0,
          border: { bottom: { style: BorderStyle.SINGLE, size: 12, color: NAVY, space: 4 } } } },
      { id: 'Heading2', name: 'Heading 2', basedOn: 'Normal', next: 'Normal', quickFormat: true,
        run: { font: FONT, size: 26, bold: true, color: NAVY }, paragraph: { spacing: { before: 280, after: 120 }, keepNext: true, outlineLevel: 1 } },
      { id: 'Heading3', name: 'Heading 3', basedOn: 'Normal', next: 'Normal', quickFormat: true,
        run: { font: FONT, size: 23, bold: true, color: KAKI }, paragraph: { spacing: { before: 200, after: 100 }, keepNext: true, outlineLevel: 2 } },
    ],
  },
  numbering,
  sections: [{
    properties: { page: { size: { width: 11906, height: 16838 }, margin: { top: 1134, bottom: 1134, left: 1134, right: 1134 } } },
    footers: { default: new Footer({ children: [new Paragraph({ alignment: AlignmentType.CENTER,
      children: [new TextRun({ text: 'まなびと講習準備 使い方マニュアル　―　', font: FONT, size: 16, color: '6B645C' }),
                 new TextRun({ children: [PageNumber.CURRENT], font: FONT, size: 16, color: '6B645C' })] })] }) },
    children: body,
  }],
});
Packer.toBuffer(doc).then(buf => { fs.writeFileSync('manual.docx', buf); console.log('ok'); });
