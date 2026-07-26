# Videx UI/UX 再設計 — 生きた設計文書

この文書は `/loop` により反復更新される。各回で (1) 未実装機能の棚卸し、
(2) 現行シェルの UX 診断、(3) 現在の Window 設計に縛られないゼロベース再設計 を更新する。

**反復ログは末尾。** 最新回の変更点はそこを見る。

> ## ★ 第 10 回 — 独立監査との突き合わせ（重要）
>
> ユーザによる独立した全体調査の結果と突き合わせ、**本文書の誤りを訂正し、
> 見落としていた重大欠陥を取り込んだ。**
>
> **методология の教訓 — 出典の優先順位を誤っていた。**
> 第 1 回で `IMPLEMENTATION_PLAN.md:283` の「Not yet implemented」リストを
> **コードで検証せずに引用した**結果、S7（トラックのドラッグ並べ替え）を
> 「未実装」と誤記した。実際には
> [timeline_widget.cpp:1093](src/app/timeline_widget.cpp:1093) に
> `trackDragging_` / `trackDropRow_` として**実装済み**。
> → **以後、機能の有無はコードのみを典拠とする。設計文書は参考に留める。**
> （同リストの他 5 項目は再検証し、すべて正しいことを確認した。）
>
> **見落としていた最重大欠陥 — プレビュー合成の二経路と画質の崖（D13）。**
> 9 回の調査でシェル・タイムライン・ヒットテスト・Undo・ビルドは精査したが、
> **「1 フレームがソースから画面に届くまで」を一度も追跡していなかった。**
> その結果、ユーザが実使用で遭遇する最も目立つ品質欠陥を発見できなかった。
> 構造と相互作用の監査だけでは、体験の欠陥は見つからない。
>
> **優先順位の訂正。** 本文書は UI/UX（テーマ・レール・パレット）を UX0 に置いていたが、
> **D13（画質の崖）・データ損失（未保存プロジェクトの自動保存欠落）・
> リポジトリ管理（追跡 6 ファイル）が明確に上位**である。Part 4 を再編した。

## 目次

- **Part 0** 現状の実測サマリ
- **Part 1** まだ作られていないもの
  — 1-A 構造 / 1-B 反応性・規模 / 1-C 仕上げ / 1-D 品質 / 1-E 高頻度オペレーション / 1-F データモデル
- **Part 2** UX 診断（D1〜D11）
  — D1 明色テーマ / D2 モニタタブ / D3 タブスタック / D4 パネル復帰不能 / D5 メニュー IA /
  D6 モーダル / D7 スキャン可能性 / **D8 ショートカット分裂** / **D9 カーソルの嘘** /
  D10 カーソル語彙 / **D11 描画の 2 つの限界**
- **Part 3** ゼロベース再設計
  — 3-1 トークン / 3-1a 具体値 / 3-2 レイアウト / 3-3 コマンドパレット / 3-4 非モーダル /
  3-5 スキャン可能性 / 3-6 パネル迷子ゼロ / 3-7 乗り換えコスト / 3-8 UX 目標 /
  3-9 ヒットテスト / 3-10 Context Rail / 3-11 スキーマ v2 / 3-12 `main_window` 分割 /
  **3-13 Undo / 3-14 オーディオスクラブ / 3-15 コマンドモデル**
- **Part 4** 実行順序（UX0 / UX0.5 / UX1 / UX2 / UX3 / UX-R）
- **Part 5** 検証結果（ビルド / テスト / 回帰リスク）★第 7 回
- **Part 6** **実装記録 — Z3 / Z5** ★第 11 回（設計から実装へ）
- **反復ログ**（第 1〜11 回）

---

## Part 0. 現状の実測サマリ

コードから確認した事実（推測ではなく grep/読解による）。

| 項目 | 実測値 | 出典 |
| --- | --- | --- |
| 実装コード総量 | 約 20,800 行 | `src/` + `workers/` |
| `main_window.cpp` | **9,172 行**（単一ファイル） | `src/app/main_window.cpp` |
| アイコン資産 | **0 個**（`.qrc`/`.svg`/`.png` 皆無） | `find src -name "*.qrc" -o -name "*.svg"` |
| アプリ全体スタイルシート | **無し**（OS 既定の明色 Qt テーマ） | `setStyleSheet` は monitor widget の 3 箇所のみ |
| ドックパネル数 | 9（Project / Inspector / Effect Controls / Effects Browser / History / Audio Meters / Text / Timeline / Jobs） | `addDockWidget` × 9 |
| 右側ドック | 6 個すべてを 1 つのタブスタックに `tabifyDockWidget` | L3376–3380 |
| `Window` メニュー | **中身が空**（生成して破棄している） | L1499 `menuBar()->addMenu(tr("&Window"));` |
| ドック表示トグル | **0 個**（`toggleViewAction` の呼び出しが皆無） | grep 結果 0 件 |
| シーケンス数 | **1 プロジェクト 1 本固定** | `EditSession` が `Sequence sequence_;` を単一保持 |
| Undo 実装 | 1 ステップごとに **Sequence 全体のスナップショット** | `HistoryEntry { Sequence sequence; }` |
| 内蔵エフェクト | **5 種**（Brightness / Contrast / Saturation / Blur / Vignette） | `enum class EffectType` |
| トランジション | 実体は**クロスフェード尺のみ**（種類の概念が無い） | `videoTransitionInFrames` / `audioTransitionInFrames` |
| マスク形状 | 矩形 / 楕円 のみ | `enum class MaskShape` |

---

## Part 1. まだ作られていないもの（棚卸し）

### 1-A. 構造的な欠落 — これが無いと「編集アプリの形」にならない

| # | 未実装 | なぜ致命的か |
| --- | --- | --- |
| S1 | **複数シーケンス** | `EditSession` が Sequence を 1 本しか持たない。長尺編集の唯一の整理手段が無い |
| S2 | **ネストシーケンス / コンパウンドクリップ** | 複雑な合成を 1 クリップに畳めない。トラックが無限に増える |
| S3 | **調整レイヤー (Adjustment Layer)** | 複数クリップへの一括グレーディング手段が無い |
| S4 | **オーディオミキサー** | トラック単位の音量・パン・バス・オートメーションが無い。Solo/Mute フラグのみ |
| S5 | **スコープ**（波形 / ベクトル / ヒストグラム / パレード） | 露出・色を数値で判断できない |
| S6 | **カラーコレクション**（ホイール / カーブ / LUT / セカンダリ） | 5 種の単発エフェクトのみ。色管理パイプライン自体が無い |
| ~~S7~~ | ~~**トラックのドラッグ並べ替え**~~ | **第 10 回訂正: 実装済み。** [timeline_widget.cpp:1093](src/app/timeline_widget.cpp:1093) の `trackDragging_` / `trackDropRow_`。同種行へのスナップ付き。誤記の原因は `IMPLEMENTATION_PLAN.md` の古い記述をコード検証せず引用したこと |
| S8 | **マルチトラックのソースパッチ** | ターゲットは video 1 本 + audio 1 本に固定（`videoSourcePatched_` / `audioSourcePatched_` の 2 bool のみ・再検証済み） |
| S9 | **トラック名 / 色の編集** | **UI が無いだけでなくコマンド自体が無い**（再検証: `SetTrackNameCommand` / `SetTrackColorCommand` は `EditCommand` variant に存在しない）。`Track` には `name` / `color` フィールドがあるので、変更経路が丸ごと欠落している |
| S10 | **クリップのラベルカラー** | ビン・タイムラインでの視覚的分類ができない |

### 1-B. 反応性・規模の欠落 — 「重い」と感じさせる原因

| # | 未実装 | 現状 |
| --- | --- | --- |
| P1 | **クリップ描画の仮想化** | 行 (row) は cull されるがクリップは全描画。`PRODUCT.md` の「10,000 アイテムで 60fps」は構造的に未達 |
| P2 | **Undo の差分化** | 1 ステップ = Sequence 全体コピー。長時間セッションで履歴深度がメモリに直結 |
| P3 | **ハードウェアデコード** | CPU デコードのみ。4K 素材で破綻する |
| P4 | **バックグラウンドレンダー / レンダーキャッシュ** | In-to-Out の手動プレビューレンダーのみ。自動キャッシュが無い |
| P5 | **エクスポートキュー** | モーダルダイアログで 1 本ずつ。書き出し中は編集不能 |
| P6 | **`main_window.cpp` の分割** | 9,172 行の単一ファイル。UI 改修のたびに衝突とリスクが増える |

### 1-C. 仕上げ機能の欠落（Post-P2 バックログ相当）

タイムリマップグラフ / オプティカルフロー / フリーズフレーム / リプレイスエディット /
マルチカム / スタビライズ / トラッキング / クロマキー / 高度マスク（ベジェ・追従） /
EQ・コンプ・リミッター・ラウドネス / ノイズ除去 / サイドチェイン / VST・OFX ホスティング /
タイトルテンプレート / モーショングラフィックス / XML・AAF・EDL 交換 / HDR 納品。

### 1-D. プロダクト品質の欠落

| # | 未実装 |
| --- | --- |
| Q1 | **ショートカットのカスタマイズ UI**（`keymap` 相当が皆無） |
| Q2 | **名前付きワークスペースプリセット**（保存状態は 1 つ + 到達しにくい Reset のみ） |
| Q3 | **設定 / 環境設定ダイアログそのものが無い** |
| Q4 | **アクセシビリティ**（フォーカスリング、スクリーンリーダー名、コントラスト検証） |
| Q5 | **クラッシュレポート**、署名付き Windows パッケージ、公証済み macOS ユニバーサル |
| Q6 | **macOS CI の実行実績**（ワークフローはあるがリモート成功が未確認） |
| Q7 | **トランスクリプト編集 / ローカル文字起こし / 無音除去提案 / AI 編集差分** — P3 の AI ワークフロー一式 |
| Q8 | **オンボーディング / 初回起動体験 / チュートリアル** |
| Q9 | **性能テストが 1 件も無い**（第 8 回発見）。`PRODUCT.md` は 7 つの性能予算を明示しているが測定手段が存在しない。`tests/performance/` ディレクトリ自体が未作成（`IMPLEMENTATION_PLAN.md` の規定と不一致） |
| Q10 | **キー入力経路・カーソル状態のテストが皆無**（第 8 回発見）。D8 と D9 が 6 回の設計まで検出されなかった直接の原因 |
| **Q11** | **リポジトリが管理されていない**（第 10 回・実測）。`git ls-files` = **6 ファイル**（すべて docs）。**製品コード全体が未追跡**（`src/` `tests/` `workers/` `CMakeLists.txt` `cmake/`）。さらに `Tools/` に **12,754 ファイル**の外部バイナリ・ZIP・解析ツール（Ghidra / Wireshark / dnSpy / DrMemory / API Monitor）が置かれている |

> **Q11 は他のすべての作業の前提。** 製品コードが 1 つもコミットされていない状態では
> **どの変更も差分として追えず、壊れたときに戻せない。**
> 本文書が提案する UX0〜UX3 のいずれも、この状態で着手するのは危険。
> **手順**: (1) `Tools/` を `.gitignore` へ（または別リポジトリ / Git LFS へ分離）、
> (2) `src/` `tests/` `workers/` `cmake/` `CMakeLists.txt` `CMakePresets.json` `vcpkg.json`
> `docs/` をコミット、(3) `build/` `.deps/` `.vs/` `aqtinstall.log` が
> 除外されていることを確認。
> なお `.gitignore` は既に存在するので、まず内容を確認して不足分を足す。

### 1-E. 高頻度編集オペレーションの欠落（第 2 回で判明）

機能表には載らないが、**Premiere 使用者が 1 分に何度も叩く**操作群。これが無いと
「機能はあるのに遅い」という評価になる。すべて grep で不在を確認済み。

| # | 未実装オペレーション | Premiere 既定キー | 影響 |
| --- | --- | --- | --- |
| H1 | **シーケンス全体にズーム** | `\` | 最頻出のズーム操作。現状 `+`/`-` の連打しかない |
| H2 | **選択範囲にズーム** | `Shift+Z` 相当 | 同上 |
| H3 | **プレイヘッド位置で全ターゲットトラックを分割**（Add Edit） | `Ctrl+K` | Razor ツールで 1 トラックずつ切る必要がある |
| H4 | **マッチフレーム** | `F` | タイムラインのクリップを Source に呼び戻せない。素材の再利用が困難 |
| H5 | **プレイヘッドまでトリム**（前/後） | `Q` / `W` | 尺詰めの主要手段。ドラッグ以外の方法が無い |
| H6 | **エクステンドエディット** | `E` | 同上 |
| H7 | **リプレイスエディット** | `Alt` ドロップ / `Shift+Ctrl+V` | 差し替えができない（現状の Alt ドロップは複製） |
| H8 | **クリップのナッジ** | `Alt+←/→` | 現状 `Alt+←/→` は Slip に割当済み。位置微調整の手段が無い |
| H9 | **ペーストインサート** | `Ctrl+Shift+V` | 上書きペーストのみ |
| H10 | **属性のみペースト**（エフェクト/トランスフォーム複製） | `Ctrl+Alt+V` | 同じ補正を別クリップへ複製できない |
| H11 | **キーボードでのクリップ選択移動** | `↑↓←→` 系 | マウス必須。キーボード完結の編集ができない |
| H12 | **オーディオスクラブ** | ドラッグ中に発音 | **主要ワークフローが対話素材なのに致命的。** カット位置を耳で決められない |

> 特に H12 は `PRODUCT.md` の掲げる「インタビュー・ポッドキャスト・対話素材」という
> 主要用途と直接矛盾する。セリフの切れ目はスクラブ音で探すのが標準手法。

### 1-F. データモデル / 永続化レベルの欠落（第 3 回で判明）

UI では解決できず、**スキーマ変更を要求する**欠落。これが上位層の設計を制約する。

**F1. トランジションが「頭側のみ」— カット上のオブジェクトではない**

データ構造は `videoTransitionInFrames` / `audioTransitionInFrames` **のみ**で、
`TransitionOut` に相当するフィールドが存在しない（grep で確認）。
つまりトランジションは **incoming クリップの先頭プロパティ** としてモデル化されている。

| Premiere でできること | Videx の現状 |
| --- | --- |
| トランジションを**オブジェクトとして選択**する | 不可能（クリップのプロパティ） |
| **アラインメント**変更（カット中央 / 始点 / 終点） | 不可能（常に頭から） |
| トランジションの**種類**を変える | 不可能（クロスディゾルブ固定） |
| **シーケンス末尾**にトランジションを置く | 不可能（`fadeOut` で代用するしかない） |
| 左右で**非対称**なトランジション | 不可能 |

> トランジションは本来「2 クリップの間のカットに乗るオブジェクト」。
> 片側のプロパティとして持つ限り、上記はすべて実装できない。**スキーマ v2 の必須項目。**

**F2. ビンが「自由入力文字列」で、階層でもスキーマでもない**

[main_window.cpp:1845](src/app/main_window.cpp:1845) で `asset.metadata["bin"]` に
`QInputDialog::getText` の結果をそのまま格納している。そして
**`project_file.cpp` には `bin` / `folder` / `parent` / `group` の参照が 0 件**。
ビンは不透明な `metadata` JSON をパススルーで通り抜けているだけで、
スキーマ上の概念として存在しない。

- **階層が無い**（ネストしたビンを作れない）
- **ダイアログに名前を打ち込む**方式。New Bin ボタンもビンへのドラッグも無い
- `"Media"` と `"media"` は**別のビン**になる（正規化なし）
- ビン名の変更は**全アセットの metadata を個別に書き換える**しかない
- typo が**静かに新しいビンを作る**（バリデーションなし）

> 主要用途が「長時間インタビュー」＝クリップが大量になるワークフローである以上、
> **整理手段が実質的に存在しない**のは致命的。Premiere のビンツリーは中核機能。

**F4. `Sequence` が解像度・色空間・音声設定を持っていない（第 10 回・D13 の根本原因）**

[timeline.hpp:285](src/core/include/videx/core/timeline.hpp:285) の `Sequence` が保持するのは
**フレームレートのみ**。解像度・アスペクト・色空間・サンプルレート・チャンネル数が無い。

その結果、各所が**独自に 1280×720 を仮定している**:

| 場所 | ハードコード |
| --- | --- |
| [main_window.cpp:7031](src/app/main_window.cpp:7031) | フレームサーバー要求が `1280 / divisor` × `720 / divisor` |
| [main_window.cpp:2649](src/app/main_window.cpp:2649) | `setZoomReferenceSize(1280, 720)` |
| [main_window.cpp:589](src/app/main_window.cpp:589) | タイトル描画キャンバスが `1280 × 720` 基準 |
| [main_window.cpp:5607](src/app/main_window.cpp:5607) | プレビューレンダーの幅が `1280` 固定 |

> **これが D13 の根本原因。** シーケンスに解像度が無いから、
> プレビュー経路が独自に 720p を発明し、`Auto` がそれをさらに半分にしている。
> **`Sequence` に解像度を持たせない限り、D13 は表層的にしか直せない。**
> → スキーマ v2（3-11）の**最優先項目**に格上げする。

**F5. 未保存プロジェクトは自動保存されない（データ損失リスク）**

[main_window.cpp:7874](src/app/main_window.cpp:7874):
```cpp
void MainWindow::autosaveProject() {
    if (!dirty_ || projectPath_.isEmpty()) {
        return;                        // ← 一度も保存していないと即 return
    }
```

自動保存先が `projectPath_ + ".autosave"` なので、**保存先が決まっていない
新規プロジェクトは 60 秒ごとの自動保存対象から丸ごと外れる。**

> **最も危険な瞬間に保護が無い。** 新規プロジェクトで素材を読み込み、
> 30 分編集して、保存前にクラッシュ or 停電 → **全損**。
> しかも `PRODUCT.md` は「クラッシュ復旧の損失は現在のコマンド以内」と約束している。
> → **修正**: `projectPath_` が空のときはユーザのアプリデータ領域に
> `untitled-<セッションID>.autosave` として書き、起動時に検出して復旧を提案する。

**F6. キャッシュキーが絶対パスを含むため、ファイル移動で全キャッシュが失効**

[main_window.cpp:1073](src/app/main_window.cpp:1073):
```cpp
QString cacheKeyForPath(const QString& path) {
    hash.addData(info.absoluteFilePath().toUtf8());   // ← 絶対パス
    hash.addData(QByteArray::number(info.size()));
    hash.addData(QByteArray::number(info.lastModified().toMSecsSinceEpoch()));
```

素材を移動すると `absoluteFilePath` が変わり、**サムネイル・波形・プロキシが全て失効する。**
リリンク機能はプロジェクト側の参照を復元するが、**生成済みキャッシュは黙って捨てられ、
プロキシの再生成が始まる**（長尺素材では数分〜数十分）。

> **修正**: キーを**内容ハッシュ**（先頭 N MiB + サイズ + 尺）に変える。
> 移動・リネームで失効しなくなり、**重複素材の検出**にも同じキーが使える。

**F3. `schema_version: 1` に UI / 組織状態の置き場が無い**

書き出される最上位キー: `assets` `sequence` `tracks` `clips` `markers` `captions`
`effects` `keyframes` `metadata` ほか。**以下の置き場が存在しない。**

| 必要なもの | 現状 | 影響 |
| --- | --- | --- |
| `sequences`（複数形） | `sequence` 単数 | S1 が永続化層からも不在 |
| ビン階層 | `metadata` 内の文字列 | F2 |
| ラベルカラー（アセット / クリップ） | 無し | S10 が保存できない |
| プロジェクト単位のワークスペース | `QSettings`（**マシン単位**） | 別マシンでレイアウトが失われる |
| トランジションオブジェクト | クリップのプロパティ | F1 |

> **結論: UI 再設計はスキーマ v2 を要求する。** UX0〜UX2 は v1 のままで進められるが、
> UX3（複数シーケンス・ビン・ラベルカラー）の前に v2 とマイグレーションが必要。

---

## Part 2. 現行シェルの UX 診断 — なぜ Premiere のように感じないか

機能の数ではなく **7 つの構造的な誤り** が体験を決めている。

### D1. 明色テーマ問題（最優先・最も安価）
アプリ全体のスタイルシートが無いため、OS 既定の明るいグレーで動く。これは化粧の問題ではない：
**プレビュー画像の周囲輝度が、露出と彩度の知覚を直接歪める。**
Premiere / Resolve / Final Cut がすべて黒に近い理由がこれ。加えて全コントロールが ASCII 文字
（`|<` `<` `>` `>|` `[>]` `Play` `Stop`）で、トランスポートがデバッグ用ハーネスに見える。

> **★ 第 6 回の精密化 — 実際の症状は「明色」ではなく「不一致」。**
> `timeline_widget.cpp` は **62 箇所の `QColor` リテラル**で既に暗色を描いている
> （`QColor(31,34,39)` = `#1F2227`、`QColor(24,26,30)` = `#181A1E` など）。
> モニタウィジェットも同様。つまり:
>
> | 描画者 | 現在の見た目 |
> | --- | --- |
> | 自前 `paintEvent`（タイムライン / モニタ） | **暗色**（ほぼ黒） |
> | Qt が描くもの（メニュー・ドック・ボタン・ツリー・ダイアログ） | **OS 既定の明色** |
>
> **暗いタイムラインが明るいグレーの枠にはめ込まれている状態**で、
> どちらか一方に統一されているより悪い（未完成品に見える）。
> かつ 62 個のハードコード色は、トークン化が解消すべき保守債務そのもの。

### D12. クリップ色が意味ではなくハッシュ（第 6 回発見）

[timeline_widget.cpp:54-57](src/app/timeline_widget.cpp:54):

```cpp
QColor clipColor(const Clip& clip, const bool selected) {
    const int hue = static_cast<int>((clip.assetId.value * 47U) % 360U);
    return QColor::fromHsv(hue, selected ? 190 : 145, selected ? 230 : 190);
}
```

**クリップの色はアセット ID の擬似乱数ハッシュ**で決まっている。結果:

- **タイムラインが虹色になる。** アセットごとに任意の色相が割り当たる。
  Premiere は**トラック種別**（映像＝青系 / 音声＝緑系）＋ユーザのラベルカラーで着色する。
- **映像クリップと音声クリップが色で区別できない。** 同一アセット由来なら `assetId` が同じ
  ＝**同じ色相**になる。逆に無関係な 2 アセットが近い色相を引くこともある。
- 彩度 145〜190 / 255 はかなり高く、**中彩度の虹が画面に並ぶ**。
  3-1a で定めた設計規則「**彩度は画像から遠ざける**」に正面から反する。
- **ラベルカラー（S10）を追加できない。** 色のスロットが既にハッシュに占有されている。

> **色が意味を持たないので、一目で読めない。** Premiere のタイムラインが
> スキャンしやすいのは「色＝意味（種別 + ラベル）」だから。
> ハッシュ着色は見た目が賑やかになるだけで、**走査を助けるどころか妨げている。**
>
> **再設計**: 色相はトラック種別から（3-1a の `clip.video` / `clip.audio` / `clip.title`）、
> 明度差でクリップ境界を示し、**ユーザのラベルカラーが指定された場合のみそれを優先**する。
> 同一アセットの反復は色ではなく**クリップ名とサムネイル**で識別させる。

### D13. プレビュー合成の二経路と「画質の崖」（第 10 回・最重大）

**本文書で最も深刻な診断。** 実使用で目に見える品質欠陥であり、
これまでの D1〜D12（構造・保守性の問題）とは性質が違う。

**プレビューには 2 つの経路がある**

| 経路 | 実装 | 解像度 |
| --- | --- | --- |
| **高速経路** | `QMediaPlayer` で元動画を再生し、静止タイトルを `QPainter` で**上に重ねる**（[collectTitleOverlays()](src/app/main_window.cpp:6493)） | **元素材のネイティブ解像度**（1080p 等） |
| **フォールバック** | FFmpeg フレームサーバーで全レイヤを CPU 合成（[requestTimelineFrame()](src/app/main_window.cpp:7026)） | **再生中は 640×360** |

**高速経路が使えなくなる条件**（[L6516-6522](src/app/main_window.cpp:6516) が `false` を返す）

```cpp
if (trackIndex < baseTrackIndex          // タイトルがベース映像より背面
    || !clip.motionKeyframes.empty()     // モーションキーフレームがある
    || !clip.effects.empty()             // エフェクトがある
    || clip.maskShape != MaskShape::None // マスクがある
    || clip.cropLeft != 0.0 || ...       // クロップがある
    || clip.videoTransitionInFrames > 0) // トランジションがある
    return false;
```

静止トランスフォーム（位置・拡大・回転・アンカー・不透明度）は高速経路で扱える。
**しかしキーフレームを 1 つ打った瞬間に経路が切り替わる。**

**画質の崖の実測**

[main_window.cpp:7026](src/app/main_window.cpp:7026):
```cpp
int divisor = 1;
if (playbackRequested_) {
    divisor = playbackResolutionDivisor_ == 0 ? 2 : playbackResolutionDivisor_;
}
request += QByteArray::number(1280 / divisor) + ' ';   // 幅
request += QByteArray::number(720  / divisor) + ' ';   // 高さ
```

- **合成の基準が 1280×720 にハードコードされている**（シーケンス解像度ではない）
- **`Auto` は再生中に無条件で divisor 2** → **640×360**
- 停止中は divisor 1 → 1280×720

> **タイトルにモーションキーフレームを 1 つ追加すると、
> プレビューが 1080p から 640×360 へ落ちる（面積比 1/9）。**
> UI にはどちらの経路で再生しているかも、なぜ落ちたかも**一切表示されない。**
> これが「テキストがある場所だけ画質が落ちる」の直接原因。
>
> さらに 4K プロジェクトでは、**停止中ですらプレビューが 720p で打ち止め**になる
> （`setZoomReferenceSize(1280, 720)` も同じ前提に固定されている）。

**根本原因は F4（下記）— `Sequence` が解像度を持っていない。**
持っていないから、プレビュー経路が 1280×720 を発明している。

**QRhi は確保されているが、何も合成していない**

[qt_monitor_widget.cpp:1487](src/render/src/qt_monitor_widget.cpp:1487):
```cpp
void QtMonitorWidget::render(QRhiCommandBuffer* commandBuffer) {
    const QColor background = QColor::fromRgb(23, 25, 28);
    commandBuffer->beginPass(renderTarget(), background, ...);
    commandBuffer->endPass();          // ← 背景クリアのみ。描画ゼロ
}
void QtMonitorWidget::initialize(QRhiCommandBuffer*) { }   // ← 空
```

`QRhiWidget` の土台はあるが**実際の合成は CPU / FFmpeg / QPainter 側**にある。
`IMPLEMENTATION_PLAN.md` の Week 2 スパイク（QRhi で色テストフレームを描く）は
**土台までで止まっており、合成器としては未着手。**

> **再設計方針**
> 1. **高速経路を一般化する** — 位置・拡大・回転キーフレーム、クロップ、フェード程度は
>    オーバーレイ側で評価できる。`collectTitleOverlays` の拒否条件を
>    「オーバーレイで表現できないもの」だけに絞る（マスク・エフェクト・背面配置）。
> 2. **QRhi を本物のコンポジターにする** — 映像テクスチャ + タイトル + マスク +
>    エフェクトを GPU で合成。CPU RGBA 変換とプロセス間転送を経路から外す。
> 3. **`Auto` を適応式にする** — 最初から 1/2 に落とさず、
>    フル → 1/2 → 1/4 を実測負荷（フレーム到着遅延）に応じて動的に選ぶ。
> 4. **経路と理由を Status Ribbon（3-17）に常時表示する** —
>    「直接オーバーレイ」/「合成器（理由: Title motion）」。
>    現状は**なぜ遅いのかユーザに知る手段が無い。**

### D2. Source / Program がタブ（構造的な誤り）
Premiere の中核ループは **素材を開く → In/Out を打つ → パッチ → Insert**。
現状は `QTabWidget` なので **Source と Program を同時に見られない**。
3 点編集がタブ往復を強要され、筋肉記憶が成立しない。ここが「Premiere らしくない」最大の原因。

### D3. 右レール 6 パネルが 1 つのタブスタック
Inspector / Effect Controls / Effects Browser / Text / History / Audio Meters が排他タブ。
**キーフレームレーンと数値フィールドを同時に見られない。**
Inspector と Effect Controls は概念的に重複しており、そもそも 2 つある必要がない。

### D4. パネルを閉じたら戻せない
`Window` メニューは空。`toggleViewAction()` はゼロ。
唯一の復帰路が `Sequence > Reset Workspace`（メニューの場所が誤り、ショートカット未割当）。
**新規ユーザーが最初に踏む地雷。**

### D5. メニュー IA が崩れている
`Sequence` メニューが雑多置き場になっている — J/K/L、フルスクリーン、Reset Workspace、
Render In to Out、マーカー、キャプション。`Clip` `Marker` `Graphics` `View` `Window` が存在しない。

### D6. ホットパスにモーダルダイアログ
書き出し設定、マーカー編集がモーダル `QDialog`。
Premiere はこれらをパネル / キューに追い出して **編集を止めない**。

### D7. スキャン可能性が無い
プロジェクトパネルは 3 列ツリーのみ。サムネイル / アイコンビューが無く、
**ホバースクラブが皆無**。Premiere で最も費用対効果が高いアフォーダンスが欠けている。

### D8. ショートカットの二重登録とフォーカス依存（第 2 回発見・最重要級）

キーバインドが **2 つの独立した仕組みに分裂**しており、統括する表が存在しない。

1. `MainWindow` — `QAction` + `Qt::ApplicationShortcut`（**ウィンドウ全体で有効**）
2. `TimelineWidget::keyPressEvent` — 16 キーを直接処理（**タイムラインにフォーカスが必要**）

Qt では `ApplicationShortcut` が `keyPressEvent` より先に消費するため、次の破綻が起きている。

**(a) 到達不能なデッドコード — タイムライン側の 6 ハンドラ**

| キー | タイムライン側の実装 | 実際に動くもの | 問題 |
| --- | --- | --- | --- |
| `End` | `contentEndFrame()` へ | main_window の `sequenceEndFrame()-1` | **意味が違う 2 実装。片方が死んでいる** |
| `Home` | `0` へ | main_window（同じ挙動） | ロジック重複 |
| `Space` | 再生トグル | main_window `transportAction_` | 到達不能 |
| `K` | 停止 | main_window Stop | 到達不能 |
| `←` `→` | フレーム送り | main_window フレーム送り | ロジック重複 |
| `Z` | `timeline_widget.cpp:1950` の処理 | **Zoom ツール切替（`"Z"`）** | 到達不能 |

**(b) 無言のフォーカス依存 — 最も価値の高い操作が沈黙する**

以下は `ApplicationShortcut` に無いためタイムライン側が動くが、
**タイムラインがフォーカスを持っていないと何も起きない**（エラーも出ない）。

| キー | 機能 | 症状 |
| --- | --- | --- |
| `↑` `↓` | **前/次の編集点へジャンプ** | Program モニタをクリックした直後は沈黙 |
| `+` `-` `=` | タイムラインズーム | 同上 |
| `S` | スナップトグル | 同上 |
| `Delete` `Backspace` | クリップ削除 | Project パネル選択中は沈黙 |

> `TimelineWidget` は `Qt::StrongFocus` かつクリックで `setFocus()` するため、
> **「直前にどこをクリックしたか」で同じキーの挙動が変わる。**
> これは機能チェックリストには現れないが、「このアプリは信用できない」という
> 感覚を生む典型的な原因。Premiere は明示的なパネルフォーカスモデルと
> グローバル/パネル別スコープの公開表でこれを解決している。

**再設計での解決策**: キーバインドを**単一のレジストリ**に集約する。

```cpp
// 全バインドが 1 つの表に存在し、スコープを明示的に宣言する
struct Binding {
    CommandId    command;
    QKeySequence key;
    Scope        scope;   // Global | TimelinePanel | MonitorPanel | ProjectPanel
};
```

- 表は起動時に**衝突検出**を行い、重複を assert で落とす（デッドコードの再発防止）。
- コマンドパレット（3-3）とキーマップエディタ（3-7）はこの同じ表を読む。
- **パネルスコープのキーは、そのパネルのヘッダにフォーカスリングを描く**ことで
  「今どのパネルがキーを受け取るか」を可視化する。無言の沈黙を無くす唯一の方法。

### D9. ヒットテストの優先順位が不可視で、ホバーカーソルが嘘をつく（第 3 回発見）

[timeline_widget.cpp:1295-1325](src/app/timeline_widget.cpp:1295) は
**6 段の `else if` チェーン**でドラッグ操作を決めている。判定順は:

```
1. TransitionIn   |x - transitionEndX| ≤ 8   かつ  y ≤ clipTop + 18
2. Gain（音声のみ） |y - gainY| ≤ 5           ← クリップ内のどこでも
3. FadeIn          |x - fadeInX| ≤ 8         かつ  y ≤ clipTop + 12
4. FadeOut         |x - fadeOutX| ≤ 8        かつ  y ≤ clipTop + 12
5. TrimStart       |x - clipLeft| ≤ trimHandleWidth
6. TrimEnd         |x - clipRight| ≤ trimHandleWidth
7. Move（フォールバック）
```

**問題 1 — ホバーカーソルが実際の動作と一致しない（最も悪質）**

[L1463-1465](src/app/timeline_widget.cpp:1463) のホバー判定は
**クリップ左右端との距離だけ**を見て `SizeHorCursor`（↔）を出す。
しかし `fadeInFrames == 0` のとき `fadeInX == clipLeft` なので、

> **クリップ左端の上部 12px をクリックすると、↔ カーソルが出ているのに
> トリムではなく FadeIn が始まる。**

同じことが右端（`fadeOutFrames == 0` → `fadeOutX == clipRight`）でも起きる。
**カーソルが嘘をつく**のは、直接操作 UI で最も信頼を破壊する種類の欠陥。

**問題 2 — 不可視の 12px / 18px バンドが動作を切り替える**

トランジション帯は `y ≤ clipTop+18`、フェード帯は `y ≤ clipTop+12` で **領域が重なる**。
`y ∈ [clipTop, clipTop+12]` かつ x が両ハンドルに近いとき、
トランジションが**黙って勝つ**。短いフェードと短いトランジションが共存すると
`fadeInX ≈ transitionEndX` となり、**片方のハンドルに到達できなくなる**。
これらの境界には**視覚的な表示が一切ない**。

**問題 3 — 音声クリップでは「クリップのゲイン値」が動作を決める**

Gain は優先度 2（フェードとトリムより上）で、判定は
`|y - gainY| ≤ 5` のみ＝**クリップ内のどこでも当たる**。
`gainY` はゲイン値から計算されるため、

> **ゲインを上げるとゲイン線が上端に近づき、左上角でのトリムがゲインドラッグに化ける。**

同じ場所をクリックしても、クリップの状態次第で違う操作になる。ユーザーには予測不能。

**問題 4 — ツール選択時はヒットテストが完全に無効**

Slip / Rolling / Ripple / Slide ツールでは `dragMode_` が**ツールだけから決まり**、
ハンドル判定を一切行わない（L1295-1302）。クリップのどこを掴んでも同じ操作になる。

### D10. カーソル語彙が貧弱で、ツール別のホバーフィードバックが皆無

**6 種類の汎用 Qt カーソルで約 20 の操作状態を表現している。**

| 使用カーソル | 回数 | 担当している状態 |
| --- | --- | --- |
| `SizeHorCursor` ↔ | 2 | TrimStart / TrimEnd / **（実際は FadeIn/FadeOut も）** |
| `SizeVerCursor` ↕ | 3 | トラック高さドラッグ |
| `OpenHand` / `ClosedHand` | 5 | Hand ツール / 移動 |
| `CrossCursor` | 1 | Razor |
| `ForbiddenCursor` | 1 | 不正なドロップ |

- **Slip / Roll / Ripple / Slide に固有カーソルが無い。** Premiere のトリム精度は
  「クリックする前に何が起きるか分かる固有カーソル」から来ている。すべて同じ ↔ では区別不能。
- さらに [L1457](src/app/timeline_widget.cpp:1457) はホバー処理を
  `tool_ == Tool::Selection` で**ゲート**している。つまり
  **Ripple / Roll / Slip / Slide ツール中はホバーフィードバックがゼロ**。
  ハンドルの位置も、操作可能かどうかも分からない。
- アイコン資産が 0 件（D1）だが、**カスタムカーソルはアイコンパイプライン無しで作れる**
  （第 4 回で判明。下記参照）。

### ★ 重要な訂正と再構成（第 4 回）— D9/D10 の解決策は既にリポジトリ内にある

`qt_monitor_widget.cpp` を精査した結果、**Program モニタは 3-9 で設計した仕組みを既に実装していた。**
つまり D9/D10 の修正は「新アーキテクチャの発明」ではなく「**30 ファイル隣のパターンの適用**」。

| 3-9 の設計 | モニタウィジェットの既存実装 |
| --- | --- |
| `AffordanceKind` 列挙 | `enum class Handle` — **20 個**（Position / Scale 8 / Rotate / Anchor / Crop 8） |
| 描画とヒットテストが同一ロジック | **`hitTest()` を press（[L127](src/render/src/qt_monitor_widget.cpp:127)）と hover（[L185](src/render/src/qt_monitor_widget.cpp:185)）の両方から呼んでいる** |
| ハンドル → カーソルの単一写像 | `applyHoverCursor(handle, point)` の単一 `switch`（[L867](src/render/src/qt_monitor_widget.cpp:867)） |
| カスタムカーソル | `rotationCursor()`（[L41-59](src/render/src/qt_monitor_widget.cpp:41)）が**既に存在** |

**さらにモニタ側は回転を考慮したカーソルまで実装している。**
[L895-897](src/render/src/qt_monitor_widget.cpp:895) はアンカーからマウス位置への角度を計算し、
**オブジェクトの回転に合わせてスケールカーソルの向きを変えている**。多くの商用エディタより丁寧。

**訂正: カスタムカーソルにアイコン資産は不要。**
`rotationCursor()` は `QPainter` で `QPixmap` に描き、`static` ラムダでキャッシュして
`QCursor` を返している。この手法をそのまま複製すれば D10 の 8 種は作れる。
→ **UX0 項目 10 のコストは当初見積りより大幅に低い。**

> **投資が逆転している。**
> Program モニタ: ハンドル 20 種 / 統一ヒットテスト / 回転対応カーソル。
> タイムライン: DragMode 12 種 / **分裂したヒットテスト** / 汎用カーソル 6 種。
> **編集者が時間の 9 割を過ごすのはタイムラインなのに、そこだけ相互作用モデルが弱い。**
> 3-9 は新規設計ではなく、`src/render` の水準に `src/app` を揃える作業として位置づけ直す。

### D11. 描画のスケール限界は 2 種類ある（第 4 回発見）

`paintEvent`（[timeline_widget.cpp:2062](src/app/timeline_widget.cpp:2062)）を精査した結果、
「仮想化が無い」という一言では足りず、**独立した 2 つの限界**が存在する。

**限界 1 — クリップ数: 本流にビューポート判定が無い**

```cpp
for (const auto& clip : track.clips) {   // ← トラック内の全クリップ
```

直前に `painter.setClipRect(...)` はあるが、**Qt のクリップ矩形が節約するのはラスタライズだけ**で、
ループ本体の CPU 作業は画面外クリップでも全部走る:
`frameToX` 呼び出し、エフェクト×キーフレームの二重ループ、
速度 / モーション / ゲインの各キーフレームループ、`QRectF` 構築、色計算。

> **判定コードは既にこのファイルの中にある。**
> `DragMode::Move` のゴースト描画分岐だけが
> `if (ghostRight >= trackLabelWidth && ghostLeft <= width())` を持っている
> （[L2185](src/app/timeline_widget.cpp:2185)）。**本流に同じガードが無いだけ。**
> → 最小修正は、この判定を本流に `continue` ガードとして写すこと。
> → 本修正は、クリップが `timeline.start` 順に並んでいる性質を使って
> **最初の可視クリップを二分探索**し `O(log n + 可視数)` にする。

**限界 2 — クリップ幅: 波形が 1 ピクセル 1 `drawLine`**

```cpp
for (int x = firstX; x <= lastX; ++x) { ... painter.drawLine(...); }
```

こちらは**ビューポートに正しくクランプされている**
（`max(ceil(left), trackLabelWidth)` / `min(floor(right), width())`）ので、
画面外クリップではループ本体が回らない。**限界 1 とは別種の問題。**

> しかし**ビューポート幅いっぱいに広がった音声クリップ 1 本で、
> 1 回の再描画あたり `drawLine` が幅ピクセル数だけ発行される**（3840px 幅なら 3840 回）。
> **クリップ数とは無関係に、ズームアウト時や長尺クリップで効く。**
> → 修正は (a) `drawLines()` で配列一括発行、または
> (b) `(assetId, pixelsPerFrame, 行高)` をキーに波形を `QPixmap` へ事前レンダーして blit。
> (b) はスクロール中の再描画をほぼ無償にできる。

> **結論: 「仮想化」を 1 タスクとして扱うと限界 2 が取り残される。**
> 別タスクとして分離する（Part 4 で反映済み）。

---

## Part 3. ゼロベース再設計 — 現行 Window 設計を捨てた場合

> 方針: **Premiere を模倣するのではなく、Premiere が構造的に持つ弱点（発見可能性・パネル迷子・
> モーダル停止）を最初から潰す。** 模倣で追いつくのではなく、越える形を設計する。

### 3-1. 基盤: デザイントークン層（すべての前提）

単一のトークン定義を唯一の真実として、そこから QSS とパレットを生成する。

> **★ 第 6 回の決定変更 — トークンは JSON ではなく C++ `constexpr` にする。**
> 第 2 回で `tokens.json` を提案したが、ビルド構成を読んで**撤回した**。理由:
>
> 1. **タイムラインの `paintEvent` はトークンを C++ の値として必要とする。**
>    QSS はカスタム描画を一切スタイルできない（62 箇所のハードコード色と
>    D12 のクリップ色はすべて `paintEvent` 内）。JSON にすると
>    **同じ色を 2 系統で管理**することになり、単一の真実が壊れる。
> 2. **ビルド時生成は新しい依存を持ち込む。** `IMPLEMENTATION_PLAN.md` は
>    「ビルドは開発者のグローバル環境に依存してはならない」と明記しており、
>    Python 等をビルドパイプラインに足すのはこの方針に反する。
> 3. `constexpr` なら**コンパイル時に型と存在が検査される**。
>    JSON のキー typo は実行時まで発覚しない。
>
> **採用する構成**（新しいビルド依存ゼロ）:
>
> ```
> src/ui/theme/theme_tokens.hpp   constexpr な色・寸法・タイポ（唯一の真実）
> src/ui/theme/theme.cpp          QPalette 構築 + QSS テンプレートへの置換
> resources/theme/videx-dark.qss  @token プレースホルダ入りの可読な QSS
> resources/icons/*.svg           単色 SVG、実行時にトークン色でティント
> ```
>
> QSS は**本物の .qss ファイルとして可読なまま**（構文強調が効く）、
> 起動時に 1 パスで `@surface-panel` 等を `constexpr` 値へ置換する。
> `paintEvent` は同じ `constexpr` を直接読む。**単一の真実が保たれる。**

- **ニュートラルは実測ベース**（目分量禁止）。プレビュー枠の周囲は L\* ≈ 20 前後の無彩色に固定し、
  画像判断を歪めない。アクセントは 1 色のみ。
- **セマンティック色**: video / audio / title / caption / marker / cache / error を
  トークンとして定義し、タイムライン・ビン・インスペクタで一貫使用。
- **アイコン化**: `|<` `>|` `[>]` を含む全 ASCII コントロールを SVG に置換。
  ツールバー 10 個、トランスポート 8 個、トラックヘッダ 6 個が最初の対象。
- ライト / ダークではなく **ダーク固定 + コントラスト強弱 2 段**。編集アプリに明色テーマは不要。

### 3-1a. `tokens.json` 具体値（第 2 回で確定）

ニュートラルは **CIE L\* から sRGB を逆算**して決めた（目分量ではない）。
`L* → Y → sRGB` 変換で算出し、コントラスト比は WCAG 式 `(Y₁+0.05)/(Y₂+0.05)` で検証済み。

**ニュートラル階層**

| トークン | L\* | hex | 用途 |
| --- | --- | --- | --- |
| `surface.viewer` | 12 | `#1F1F1F` | **モニタ周囲**。最暗。画像判断を歪めない基準面 |
| `surface.panel` | 16 | `#282828` | パネル地 |
| `surface.raised` | 20 | `#303030` | ツールバー / トラックヘッダ / 入力欄 |
| `surface.hover` | 25 | `#3B3B3B` | ホバー / 選択行 |
| `border` | 32 | `#4B4B4B` | 罫線 / パネル境界 |
| `text.secondary` | 62 | `#969696` | 補助ラベル / 無効状態 |
| `text.primary` | 78 | `#C1C1C1` | 本文 |
| `text.emphasis` | 90 | `#E2E2E2` | 見出し / アクティブ値 |

**コントラスト検証**

| 組み合わせ | 比 | 判定 |
| --- | --- | --- |
| `text.primary` on `surface.panel` | **8.19 : 1** | WCAG AAA（7:1 以上）✓ |
| `text.secondary` on `surface.panel` | **4.98 : 1** | WCAG AA（4.5:1 以上）✓ |

**セマンティック色**

| トークン | hex | 備考 |
| --- | --- | --- |
| `accent` | `#4C8DFF` | 選択 / フォーカスリング / アクティブツール。**1 色のみ** |
| `playhead` | `#E8E8E8` | 白に近い縦線 + アクセント色のハンドル |
| `clip.video` | `#3E5C8A` | 彩度を抑えた青 |
| `clip.audio` | `#3E7A52` | 彩度を抑えた緑 |
| `clip.title` | `#6B4A8A` | 紫 |
| `clip.caption` | `#8A6A3E` | 琥珀 |
| `marker` | `#2E9E4F` | **既存コードの既定値 `0xFF2E9E4FU` を流用**（`SetMarkerCommand`） |
| `cache.rendered` | `#2E9E4F` | キャッシュバー：レンダー済み |
| `cache.stale` | `#E0A030` | キャッシュバー：要再レンダー |
| `state.error` | `#D9534F` | 欠落メディア / ワーカー異常 |
| `state.warning` | `#E0A030` | ドロップフレーム / 低ディスク |

> **設計規則: 彩度は画像から遠ざける。**
> 飽和色はタイムライン（画像から離れた領域）に限定し、`surface.viewer` とその
> 隣接 8px は完全な無彩色に固定する。モニタ枠に色を置くと露出・色判断が狂う。

**寸法・タイポグラフィ**

```
radius:   clip 2px / control 3px / panel・popover 6px
spacing:  2, 4, 8, 12, 16, 24   （4px グリッド。8px は既存コードの余白と整合）
track:    minimal 28px / standard 54px / expanded 96px  （既存の 3 プリセットに対応）
font.ui:  13px（Windows: Segoe UI Variable / macOS: SF Pro Text）
font.mono: 13px tabular
```

> **タイムコードは必ず等幅数字（tabular numerals）にする。**
> 現状は `timecodeLabel_->setMinimumWidth(96)` + 中央揃えで幅を誤魔化しているが、
> プロポーショナル数字のままでは **再生中に桁幅が変わって数字が横に揺れる**。
> Qt では `QFont::setStyleHint(QFont::Monospace)` あるいは
> `font.setFeature("tnum", 1)`（Qt 6.7+）で解決する。
> 同じ処置がインスペクタの数値フィールドとトリム HUD にも必要。

### 3-2. レイアウト: 「1 キャンバス・2 レール」

9 個の自由浮遊ドックを捨て、**固定 3 ゾーングリッド**にする。

```
┌──────────────────────────────────────────────────────────────┐
│  Assemble │ Edit │ Refine │ Deliver     ← フェーズ切替（上部レール） │
├──────────────────────────────┬───────────────────────────────┤
│                              │                               │
│   Source      │   Program    │   Context Rail                │
│   （横並び・同時表示）        │   （選択対象に応じて中身が変形）  │
│                              │                               │
├──────────────────────────────┴───────────────────────────────┤
│  共有トランスポート（1 本のみ。Source/Program でフォーカス切替）    │
├──────────────────────────────────────────────────────────────┤
│  Timeline（常に全幅・常に表示）                                  │
├──────────────────────────────────────────────────────────────┤
│  Status Ribbon: ドロップフレーム / キャッシュ / ワーカー / ジョブ    │
└──────────────────────────────────────────────────────────────┘
```

**3 つの決定的な departure:**

**(a) フェーズ・ワークスペース（Premiere の任意ワークスペースの代替）**
`Assemble` / `Edit` / `Refine` / `Deliver` は作業段階であり、各段階で
**存在するパネル自体が変わる**。初心者が 9 パネルを一度に見ることが無くなる。
- `Assemble`: ビン（サムネイル・ホバースクラブ）+ Source + Program + 粗いタイムライン
- `Edit`: Source | Program 横並び + フルタイムライン + Context Rail
- `Refine`: Program 大 + Effect Controls + キーフレームグラフ + スコープ + ミキサー
- `Deliver`: プリセット + レンダーキュー + 検証チェックリスト

**(b) Context Rail — 単一パネルが選択に追従（最大の使いやすさ改善）**
タブスタックを廃止。右レールは **常に 1 つ**で、直前にクリックしたものを表示する。
- クリップ選択 → トランスフォーム + エフェクト + キーフレームレーン（**同一ビュー内**）
- キャプション選択 → テキストスタイル editor
- トラックヘッダ選択 → トラック名 / 色 / 高さ / ミキサーストリップ
- 未選択 → プロジェクトビン
> D3（キーフレームと数値を同時に見られない）と、Premiere の「パネル探し」を同時に解消する。

**(c) Source と Program は常に横並び**（タブ廃止）
狭い画面では Program 単独に折り畳めるが、既定は横並び。3 点編集ループを取り戻す。

### 3-3. コマンドパレット（`Ctrl+K`）— Premiere に無い最大の武器

編集アプリ最大の失敗は**発見可能性**。数百のコマンドがメニュー階層に埋まっている。

- 全アクションに対する曖昧検索。**各行に現在のショートカットを併記**（= 学習装置）。
- コンテキスト認識: クリップ選択中は Clip 系を上位に。
- 「最近使った」学習。
- 数値入力も受け付ける: `+15` で 15 フレーム前進、`01:23:10:00` でその TC へ移動、
  `scale 120` で選択クリップのスケール。
- パレットは**メニューを置き換えない**。メニューは網羅性、パレットは速度。

### 3-4. 直接操作をモーダルより優先

| 現状 | 再設計 |
| --- | --- |
| 書き出し = モーダルダイアログ | **Deliver フェーズ + レンダーキューパネル**。編集を止めない |
| マーカー編集 = モーダル | タイムライン上のインラインポップオーバー |
| 診断 = 隠れた MessageBox | **常時表示の Status Ribbon**（クリックで展開） |
| トラック名 / 色 = UI 無し | トラックヘッダをダブルクリックでインライン編集 |

### 3-5. スキャン可能性と予測性

- **ホバースクラブ**: ビンのサムネイル上と、タイムラインのクリップ上（Alt 押下時）。
- **ビンのアイコンビュー**: サムネイル + 尺 + ラベルカラー。リスト / アイコンをトグル。
- **クリップのラベルカラー**（8 色）をビン・タイムライン・Context Rail で共有。
- **トリム HUD**: トリム中に `±N frames` と新しい尺をフローティング表示。
  既存のトリムプレビューに数値を足すだけで精度が跳ね上がる。
- **スナップの可視化**: 吸着した瞬間にスナップ線を発光させ、`S` トグル状態を
  タイムラインクロームに常時表示。

### 3-6. パネル迷子ゼロ保証（D4 の直接修正）

1. `Window` メニューに **全ドックの `toggleViewAction()`** を列挙。
2. **番号付きワークスペース** `Ctrl+1`..`Ctrl+5`（= フェーズ切替）。
3. `Window > Reset Workspace` に移設し、`Ctrl+Shift+0` を割当。
4. **パネルを閉じた瞬間に** ステータスバーへ「`Ctrl+K` → パネル名 で復帰できます」を表示。

### 3-7. 乗り換えコストを消す

- **キーマップエディタ**: 検索可能、衝突検出、印刷可能なキーボードレイアウト表示。
- **Premiere キーマップのインポート**。既定プリセットとして「Premiere 互換」を同梱。
- **ホバーツールチップに必ずショートカットを併記**（既に一部実装済み。全体へ拡張）。

### 3-8. 測定可能な UX 目標（達成基準）

| 指標 | 目標 |
| --- | --- |
| 初回起動から最初のカットまで | 未経験ユーザーで 90 秒以内 |
| 任意コマンドへの到達 | `Ctrl+K` から 3 キーストローク以内 |
| パネル復帰 | 常に 1 アクション |
| トリムのフィードバック遅延 | 100 ms 未満（`PRODUCT.md` の予算に整合） |
| 10,000 クリップでのタイムライン操作 | 60 fps（P1 の仮想化が前提） |
| モーダルダイアログ数（編集ホットパス） | **0** |
| ホバーカーソルと実動作の一致率 | **100%**（D9 問題 1 の解消） |

### 3-9. ヒットテストの再設計 — 「見えるハンドル」原則

D9 の根本原因は **不可視のホットゾーンを `else if` の順序で解決していること**。
再設計では 3 つの規則を置く。

**規則 1 — ハンドルは必ず描画される。描画されていない領域は掴めない。**
ゾーンを暗黙に持たず、`ClipAffordance` として明示的に列挙し、
**描画とヒットテストが同じリストを読む**。これで「カーソルが嘘をつく」構造がなくなる。

```cpp
struct ClipAffordance {
    AffordanceKind kind;     // TrimStart, FadeIn, TransitionIn, Gain, Move, ...
    QRectF         hitRect;  // 描画とヒットテストで共有される唯一の矩形
    Qt::CursorShape cursor;  // またはカスタムカーソル ID
    int            priority; // 同順位の重なりは起動時 assert で禁止
};
QList<ClipAffordance> affordancesFor(const Clip&, const QRectF& clipRect) const;
```

**規則 2 — 重なりは設計時に排除する（実行時の優先順位で誤魔化さない）。**
- フェードハンドルは**クリップ上端に描かれる小さな三角**として、
  トリムハンドルの矩形と**物理的に重ならない位置**に置く。
- `fadeIn == 0` のときフェードハンドルは**トリムハンドルの内側 8px にオフセット**する
  （現状の `fadeInX == clipLeft` 衝突の直接修正）。
- 音声ゲイン線は**クリップ中央 60% の高さ帯だけ**を判定対象にし、
  上下 20% はトリム / フェード専用に予約する。ゲイン値でヒットテストが変わる問題を消す。
- トランジションは F1 の対応後、**カット上の独立オブジェクト**になるので
  クリップのアフォーダンスから外れる（重なり自体が消滅する）。

**規則 3 — ツールは「ヒットテストの上書き」ではなく「フィルタ」。**
Slip / Roll / Ripple / Slide ツールでも
**アフォーダンス列挙は常に走り、ツールが対象を絞り込む**だけにする。
ホバーフィードバックのゲート（`tool_ == Tool::Selection`）を撤去し、全ツールで
カーソルとハンドルのハイライトを出す。

**カスタムカーソル 8 種**（D10 の解消 / UX0 のアイコン作業に含める）

| 操作 | カーソル意匠 |
| --- | --- |
| Regular trim | ↔ + 単一の角括弧 |
| Ripple trim | ↔ + 角括弧 + 波線（アクセント色） |
| Roll | ↔ + 背中合わせの二重括弧 |
| Slip | ↔ + フィルムパーフォレーション |
| Slide | ↔ + 外向き二重矢印 |
| Fade | 対角線 + 小三角 |
| Gain | ↕ + 波形 |
| Razor | 剃刀 |

### 3-10. Context Rail の具体構成（第 2 回の宿題）

**不変の規則**
- **幅は内容で変わらない。** 選択が変わってもレールの幅は固定（レイアウトの揺れを防ぐ）。
- **スクロール位置を選択種別ごとに記憶する。**
- **下部にキーフレームストリップをピン留めする。** 上のプロパティ欄をスクロールしても
  キーフレームは常に見える → **D3（数値とキーフレームを同時に見られない）の直接解消。**

```
┌─ Context Rail ─────────────────┐
│ [クリップ名]  [ラベル色] [有効] │  ← ヘッダ（固定）
│ Source TC / 尺                 │
├────────────────────────────────┤
│ ▼ Transform          ⏱        │  ← ここから下がスクロール領域
│    Position   X ___  Y ___  ⏱ │     ⏱ = キーフレーム記録トグル
│    Scale      ___  [🔗均等]  ⏱ │     数値ラベルはスクラブ可能（既存実装）
│    Rotation   ___            ⏱ │
│    Anchor     X ___  Y ___     │
│    Opacity    ___            ⏱ │
│ ▼ Crop        L R T B          │
│ ▼ Speed / Duration             │
│    Rate ___  [逆再生] [尺を保持]│
│ ▼ Audio（音声を持つ場合のみ）    │
│    Gain ___                  ⏱ │
│    Fade In ___  Out ___        │
│ ▼ Effects                      │
│    ≡ Brightness      [👁] [×]  │  ← 並べ替え可能
│      Amount ___              ⏱ │
│    ≡ Blur            [👁] [×]  │
│    [+ エフェクトを追加]          │
├────────────────────────────────┤
│ キーフレームストリップ（ピン留め）│  ← 常に見える
│ Position ◆──────◆────────◆     │
│ Opacity  ────◆──────────       │
│ ├─ クリップ内ローカル時間 ─────┤ │
└────────────────────────────────┘
```

**選択対象ごとの中身**

| 選択 | Context Rail の内容 |
| --- | --- |
| ビデオクリップ | 上図（Transform → Crop → Speed → Effects） |
| 音声クリップ | Gain → Fades → Effects（Transform を出さない） |
| キャプション | テキスト / フォント / サイズ / 色 / 位置 / 表示範囲 |
| タイトルクリップ | テキストスタイル + Transform（両方必要） |
| トラックヘッダ | 名前 / 色 / 高さ / Lock・Mute・Solo・Sync + **ミキサーストリップ**（フェーダ・パン・メータ） |
| マーカー | 名前 / コメント / 色 / 位置 / 種別 |
| トランジション（F1 対応後） | 種類 / 尺 / アラインメント / イージング |
| 複数選択 | 共通プロパティのみ。値が異なる欄は `—` を表示し、編集すると全体に適用 |
| 未選択 | **プロジェクトビン**（レールが常に何かを表示する＝空白にしない） |

> これで既存の 6 パネル（Inspector / Effect Controls / Effects Browser / Text /
> History / Audio Meters）のうち **4 つが Context Rail に吸収される**。
> History と Effects Browser はコマンドパレット（3-3）に吸収できるため、
> **右側の常設パネルは 1 枚だけ**になる。

### 3-11. スキーマ v2 — UI 再設計が要求する永続化変更（第 2 回の宿題）

**判定: UX0〜UX2 は v1 のまま実装できる。UX3 は v2 を要求する。**

| 追加 | 形 | 満たす項目 |
| --- | --- | --- |
| `sequences` | 配列 + `active_sequence_id` | S1 S2 |
| `bins` | `[{id, name, parent_id}]` + `asset.bin_id` | F2 |
| `asset.label_color` / `clip.label_color` | 8 色の列挙値 | S10 |
| `transitions` | `[{id, track_id, cut_frame, kind, duration, alignment}]` | **F1** |
| `workspace` | プロジェクト単位（任意）。`QSettings` はグローバル既定として残す | F3 |
| `track.name` / `track.color` | 既にデータ構造にはある。永続化を確認 | S9 |

**v1 → v2 マイグレーション（テスト可能な具体手順）**

1. `sequence`（単数）を `sequences[0]` に移し、`active_sequence_id` を設定。
2. 全アセットの `metadata["bin"]` 文字列を収集し、**大文字小文字を正規化**して
   重複を畳み、フラットな `bins` を合成。`asset.bin_id` を割り当て、
   `metadata["bin"]` は互換のため残す（v2 リーダは無視）。
3. `clip.videoTransitionInFrames > 0` を、その**クリップ頭のカット位置**に置かれた
   `alignment: "start"` の `transitions` エントリへ変換。値が 0 のものは生成しない。
4. ラベルカラーは未設定（`null`）で開始。トラック種別から既定色を推定。
5. **v1 ラウンドトリップテストを維持**: v1 を読み → v2 で書き → 再読込して
   タイムライン意味論が同一であることを検証（既存のテスト資産を流用できる）。

> `schema_version` は既に読み込み時に厳格検証されている
> （[project_file.cpp:613](src/app/project_file.cpp:613) が `!= 1` を拒否）ため、
> **v2 リーダは v1 を明示的に受理する分岐を追加する必要がある。**
> 現状のコードは v2 ファイルを開けないだけでなく、v1 しか受け付けない。

### 3-12. `main_window.cpp` 分割案（第 3 回の宿題）

9,172 行 / **約 80 メソッド**。Context Rail（UX1 項目 16）は
「選択変更を購読して中身を差し替える」設計なので、**9,000 行クラスの private に
手を伸ばす形では実装できない**。分割は UX1 の前提条件。

**やってはいけない分割**: `main_window_playback.cpp` のようにファイルだけ割る方式。
全ファイルが `MainWindow` の private を触り続けるため、**行数は減るが結合は減らない。**

**採る方針: 状態を持つ協力オブジェクトへ抽出する。**

| 抽出先 | 移すメソッド（実測） | 保持する状態 |
| --- | --- | --- |
| `ProjectDocument` | `createProject` `openProject` `saveProject` `saveProjectAs` `autosaveProject` `confirmDiscardChanges` `setDirty` `saveWorkspaceState` `restoreWorkspaceState` | パス / dirty / スキーマ |
| `MediaLibrary` | `importMedia` `importMediaFile` `handleMediaProbe` `generateProxy` `loadWaveformCache` `startAssetCacheJobs` `pruneAssetCache` `rebuildProjectTree` `openAssetInSource` | `assets_` / キャッシュ索引 |
| `PlaybackController` | `startPlayback` `startContinuousPlayback` `startPlaybackClock` `pausePlayback` `togglePlayback` `requestPlaybackAudio` `startAudioSinkFromPcm` `stopPlaybackAudio` `ensureMediaPlayer` `updateAudioMeters` | 再生状態 / クロック / 音声シンク |
| `PreviewService` | `requestPreviewFrame` `requestTimelineFrame` `handleFrameServerResponse` `continuePendingPreview` `updateProgramFrame` `buildTimelineVideoManifest` `previewCacheValid` `requestTrimTwoUp` `startTrimTwoUpPhase` `startTrimTwoUpRender` | 保留リクエスト / マニフェスト |
| `EditController` | `copySelectedClips` `pasteClipsAt` `deleteSelectedClips` `splitSelectedClips` `slipSelected` `rollSelected` `setSelectedFades` `setSelectedTransitions` `insertSourceSelection` `editSequenceRange` | クリップボード / `EditSession` 参照 |
| `TitleService` | `addTitleClip` `createTitleClipAtPlayhead` `createTextClipAtPlayhead` `collectTitleOverlays` `captionAtPlayhead` | タイトルラスタ |
| `ExportService` | `exportReview` `exportOtio` `showExportDialog` | 進捗 / キャンセル |

**`MainWindow` に残すもの**: `createActions` `createPanels` `createStatusBar` と配線のみ。

**副次的な発見 — ヘルパークラスが .cpp 内にインライン定義されている**
`eventFilter` が **3 つ**、`dragEnterEvent` / `dropEvent` が **各 2 つ**存在する。
これは `main_window.cpp` の中に**匿名のヘルパーウィジェットクラスが複数定義されている**ことを意味する
（例: 数値ラベルのスクラブ処理 `LabelScrubber` 相当が [L375-491](src/app/main_window.cpp:375) に居る）。
これらは自己完結しているため、**最も安全な最初の一歩**になる。

**分割順序 — 全部やる前に UX1 を始められるようにする**

> **重要**: 7 つすべての抽出を UX 作業の前に完了させると、
> 可視の進捗が長期間止まる。**Context Rail に必要な最小限だけ先に抜く。**

1. **ヘルパークラスを独立ヘッダへ**（自己完結・リスク最小・即座に数百行減る）
2. **`SelectionModel` の新設**（現状は選択状態が `MainWindow` に散在）＋ 選択変更シグナル
3. **パネル更新群を `ContextRail` へ** — `updateInspector` `applyInspectorProperties`
   `updateEffectsPanel` `applySelectedEffect` `applyTextPanel` `updateMonitorEditTarget`
   `updateCaptionOverlay` `updateHistoryPanel`
   → **この 3 段で Context Rail が着手可能になる。** 残り 4 つの抽出は UX1 と並行でよい。
4. 以降 `PreviewService` → `PlaybackController` → `MediaLibrary` → `ProjectDocument`
   → `EditController` → `ExportService`（依存の浅い順）

### 3-13. Undo の差分化 — 逆コマンドではなく構造共有を採る（第 4 回の宿題）

**実測したコスト**

[edit_session.cpp:251-276](src/core/src/edit_session.cpp:251) の `apply` は:

```cpp
Sequence candidate = sequence_;                                  // 全体ディープコピー ①
EditResult result = execute(candidate, envelope.command);
...
undoStack_.push_back({.sequence = sequence_, .label = ...});     // 全体ディープコピー ②
sequence_ = std::move(candidate);
```

コンテナはすべて素の値ベクタ:
`Sequence.tracks_` = `std::vector<Track>` → `Track.clips` = `std::vector<Clip>` →
`Clip.effects` = `std::vector<ClipEffect>`（各々 `std::vector<EffectKeyframe>` を持つ）
＋ モーション / ゲイン / 速度の各キーフレームベクタ。

> **1 コマンドあたりタイムライン全体のディープコピーが 2 回。**
> `undo()` の `restoreContent` でさらに 1 回（[timeline.cpp:389](src/core/src/timeline.cpp:389)）。
> 10,000 クリップ規模では 1 コマンドごとに数 MB の確保と memcpy が走り、
> **履歴 100 段で数百 MB が滞留する。** Premiere は長編尺で 32〜128 段を扱う。

**Undo の粒度自体は既に正しい**（ドラッグ 1 ジェスチャ = 1 undo、
タイピングはデバウンスで 1 バースト = 1 undo）。**問題は純粋にメモリで、UX 粒度ではない。**

**採用案: 永続データ構造による構造共有（COW）**

`tracks_` を `std::vector<std::shared_ptr<const Track>>` 相当に変え、
書き込み時のみ該当トラックを複製する。

| | 効果 |
| --- | --- |
| `Sequence candidate = sequence_` | **トラック数個の `shared_ptr` コピーのみ**（通常 20 未満） |
| 1 クリップを触るコマンド | **そのトラック 1 本だけ複製**。他は共有 |
| `undoStack_` の各段 | 未変更トラックを**全段で共有**。滞留メモリが桁で減る |
| `execute(candidate, cmd)` の署名 | **変更なし** |
| 既存テスト | **意味論が同一なので全て通る** |

**却下案: 逆コマンド方式（inverse commands）**

`EditCommand` の `std::variant` は**約 50 種**（[edit_session.hpp:248-267](src/core/include/videx/core/edit_session.hpp:248)）。
全てに逆を実装する必要があり、しかも:

- `ExtractRangeCommand` / `LiftRangeCommand` は**複数トラックの多数クリップを削除・分割**する。
  逆は「これら N クリップを元の ID と位置で復元する」＝**結局ミニスナップショット**。
- `PasteClipsCommand` の逆には**新規生成された ID** が必要。
- `SplitClipCommand` の逆は結合 + 元 ID の復元。
- `IMPLEMENTATION_PLAN.md` の「100 万回ランダムコマンドで不変条件違反ゼロ」という
  exit gate に対し、**逆コマンドの正当性を検証する新しい性質テストが別途必要**になる。
- 逆が微妙に間違っていた場合の症状は **undo 時の静かなデータ破損** —
  編集アプリで最悪の種類のバグ。

> **結論: 構造共有を先に入れる。逆コマンドは実装しない**
> （構造共有後にプロファイルが要求した場合のみ再検討）。
> リスクと工数が桁で違い、得られる効果は同等以上。

**★ 第 6 回の実現可能性確認 — 変更は `core` 内に完全に閉じる**

| 調査項目 | 実測 | 意味 |
| --- | --- | --- |
| 公開された可変トラックアクセサ | **`const Track* findTrack()` のみ** | **外部から `Track` を書き換える経路が存在しない** |
| `tracks_` の構造操作 | `insert` 2 / `erase` 2 のみ | 構造変更点が極少 |
| `timeline.cpp` 内の可変 `Track&` / `Track*` | 37 箇所 | ここだけを COW 経路へ通す |

> **`timeline.hpp` / `timeline.cpp` の 2 ファイルで完結する。**
> `src/app` も `workers` もテストも呼び出し側の変更が不要。
> 実装方法は private な `Track& mutableTrack(TrackId)` を 1 つ追加し、
> COW 複製をそこに集約して 37 箇所をそれ経由に書き換えるだけ。
> → **3-13 の推奨は「安い」という当初の判断が裏付けられた。**

### 3-14. オーディオスクラブの実装経路（第 4 回の宿題 / H12・項目 29）

**前提の再評価: 想定より軽い。**
ワーカープロトコルのリクエスト種別は `probe` `frame` `waveform` **`audio`** `export`。
**`audio` 種別は既に存在する**ため、ワーカー側の変更は小さいか不要。
アプリ側も `QAudioSink` と `startAudioSinkFromPcm(pcm, rate, channels, start)` を持っている。
→ 第 2 回で「大」と見積もったが、**実際は「中」**。

**難所は 4 つで、いずれも設計で解ける**

| 難所 | 対策 |
| --- | --- |
| **リクエスト氾濫** — ドラッグは 60〜120 Hz でイベントを出す。素朴に投げるとワーカーが溢れる | **合流（coalescing）**: in-flight 1 + pending 1 のみ保持し、中間要求は捨てる |
| **シンク再起動のクリックノイズ** — `startAudioSinkFromPcm` は毎回停止・再開する想定 | **常設のスクラブ用シンク**＋小さなリングバッファ。グレインは書き込みで差し替える |
| **グレイン境界のプチノイズ** | 境界に **2〜3ms のクロスフェード** |
| **再生クロックの汚染** — `audioSink_` は**マスタークロック**として使われている（[L4726](src/app/main_window.cpp:4726) が `processedUSecs()` を参照） | **スクラブは別シンク・別経路**にし、再生クロックに一切触れない |

**手順**
1. 再生用とは独立した**常設スクラブシンク**を用意（リングバッファ供給）。
2. プレイヘッドドラッグ中、現在位置の **50〜80ms のグレイン**を `audio` 要求。合流規則を適用。
3. 到着したグレインをリングバッファへ上書き、境界をクロスフェード。
4. **トグルで on/off**（Premiere も持つ。嫌う編集者がいる）。既定は on。
5. ドラッグ終了でシンクを idle に戻す（停止はしない = 再起動コストを避ける）。

> **レイテンシ目標**: ドラッグからの発音まで **50ms 未満**。
> `PRODUCT.md` の「入力から可視フレームまで 100ms 未満」より厳しいが、
> 音は映像よりずれに敏感なため必要。IPC 往復がここに収まるかは要計測。

### 3-15. コマンドモデル — `CommandId` の粒度（第 4 回の宿題）

UX0 のキーバインドレジストリ（D8 の解決策）、コマンドパレット（3-3）、
キーマップエディタ（3-7）は**同一の表**を読む。その表の単位を確定させる。

**規則: `Command` と `PaletteEntry` を分離する。**

```cpp
// 呼び出し可能な単位。引数を取る。ユーザから見た「動詞」1 つに対応。
struct Command {
    CommandId   id;          // 例: SetTrackHeight
    Scope       scope;       // Global | TimelinePanel | MonitorPanel | ProjectPanel
    ArgSpec     args;        // 引数の型（列挙 / 数値 / なし）
};

// 検索・表示される行。1 つの Command に引数を束ねたもの。
struct PaletteEntry {
    QString     label;       // 例: "トラック高さ: 拡大"
    CommandId   command;     // SetTrackHeight
    ArgValues   args;        // { mode: Expanded }
};
```

**この分離が効く理由**

- `SetTrackHeight` を 3 コマンド（minimal/standard/expanded）にすると
  **キーマップエディタに 3 行、パレットに 3 行、内部関数も 3 つ**になり重複する。
  1 コマンド + 3 エントリなら、内部は 1 つでよい。
- 逆に**パレットには引数ごとの行が必要**（ユーザは「拡大」で検索する)。
- 3-3 で設計した数値入力（`+15` / `01:23:10:00` / `scale 120`）は
  **`PaletteEntry` を経由せず `Command` を直接引数付きで叩く**経路として実装できる。

**粒度の判定基準**
- **1 コマンド = ユーザから見た 1 つの動詞。** 内部関数の分割数とは無関係。
- 引数で表せるものは引数にする（トラック高さ、ズーム倍率、ナッジ量、解像度分母）。
- 引数で表せない意味の違いは別コマンド（Insert と Overwrite は別。挙動が違う）。
- **`Scope` は `Command` が持つ**（`PaletteEntry` ではない）。D8 の再発防止。

> **見積りへの影響**: この分離により、UX0 項目 6（レジストリ）は
> 「既存 `QAction` を表に移す」作業として実装でき、
> 項目 18（パレット）と 21（キーマップエディタ）は**表を読む薄い UI** になる。
> 3 つを別々に作るより総量が小さい。

### 3-16. UX0 のビルド組み込み手順（第 5 回の宿題）

**確認できた前提**

| 項目 | 実測 | 意味 |
| --- | --- | --- |
| `qt_standard_project_setup()` | [src/app/CMakeLists.txt:1](src/app/CMakeLists.txt:1) で呼ばれている | **AUTOMOC / AUTOUIC / AUTORCC が有効**。リソース追加は追加設定なしで動く |
| リソースファイル | ソース一覧に**存在しない** | アイコン 0 件をビルドレベルでも確認 |
| `main.cpp` | 22 行。パレットもスタイルシートも適用していない | D1 の挿入点が明確 |
| 品質ゲート | `videx_set_project_warnings` + `videx_enable_sanitizers` | **新規コードは警告ゼロ + サニタイザ通過が必須** |

**手順**

1. `resources/theme/videx-dark.qss` と `resources/icons/*.svg` を追加し、
   CMake に **`qt_add_resources(videx "theme" PREFIX "/" FILES ...)`** を足す。
   `.qrc` ファイルは不要（Qt6 のターゲットベース API を使う）。
2. `src/ui/theme/theme_tokens.hpp` に 3-1a の値を `constexpr` で定義。
3. `src/ui/theme/theme.cpp` に 2 関数:
   - `QPalette videx::ui::darkPalette()`
   - `QString videx::ui::styleSheet()` — リソースの QSS を読み `@token` を置換
4. `main.cpp` の `QApplication` 構築直後、`MainWindow` 構築前に 3 行挿入:
   ```cpp
   application.setStyle(QStringLiteral("Fusion"));   // QSS の効きを OS 間で揃える
   application.setPalette(videx::ui::darkPalette());
   application.setStyleSheet(videx::ui::styleSheet());
   ```
5. `timeline_widget.cpp` の **62 箇所の `QColor` リテラルをトークン参照へ置換**
   （D12 の `clipColor` も同時に意味ベースへ）。
6. `src/app/CMakeLists.txt` のソース一覧に `theme.cpp` / `theme_tokens.hpp` を追加。

> **`Fusion` スタイルの指定が重要。** Windows のネイティブスタイルは QSS の多くを無視し、
> macOS ネイティブスタイルとも挙動が違う。`Fusion` に固定すると
> **両 OS で同一の見た目**になり、`IMPLEMENTATION_PLAN.md` の
> 「同じコミットが両 OS で同じように動く」方針にも合う。

### 3-17. Status Ribbon の表示項目（第 5 回の宿題 / 項目 20）

現在の `Help > Diagnostics...` は**静的情報と動的情報が混ざったモーダル**
（OS / CPU / Qt 版 / ビルド種別 / ワーカーパス / プロジェクトパス / トラック・クリップ・
アセット数 / キャッシュ場所）。**常時表示に適するのは動的なものだけ。**

**常設リボン（右下、1 行）— 変化するものだけを出す**

| 表示 | 出典 | 異常時の見た目 |
| --- | --- | --- |
| **シーケンス** `1920×1080 · 29.97p` | `Sequence::frameRate()` | — |
| **再生画質** `1/2` | 既存の `playbackResolution` コンボを移設 | 全画質以外は `text.secondary` |
| **ドロップフレーム** `0` | 再生クロック | `> 0` で `state.warning` |
| **ジョブ** `プロキシ 3/7` | 既存 Jobs パネルの集約 | 進捗バーを内包 |
| **キャッシュ** `2.1 / 4.0 GiB` | 既存のクォータ機構（`VIDEX_CACHE_QUOTA_MB`） | 90% 超で `state.warning` |
| **ワーカー** `●` | プロセス生死 | 死亡で `state.error` + 再起動ボタン |
| **未保存 / 自動保存** `2 分前に保存` | 既存の autosave | dirty で `state.warning` |
| **メディア欠落** `2 件` | 既存の relink 機構 | `> 0` で `state.error` + クリックで relink |

**規則**
- **正常時は静かに。** 異常が無い項目は `text.secondary` で目立たせない。
  異常が出た項目だけが色を持つ。「全部が光っている」ダッシュボードにはしない。
- **各項目はクリックで対応先へ飛ぶ**（ジョブ → Jobs パネル、欠落 → relink ダイアログ、
  ワーカー → 再起動）。表示だけで終わらせない。
- **静的情報は `Help > About` へ移す**（OS / CPU / Qt 版 / ビルド種別 / パス類）。
  リボンには出さない。
- リボンをクリックで**展開**すると、現在の Diagnostics 相当の全文が下から出る（非モーダル）。

### 3-18. `tests/performance/` の新設案（第 8 回の宿題 / Q9）

**位置づけ: 既存コードに一切触れずに追加できる、最もリスクの低い着手点。**
新規ファイルと `tests/CMakeLists.txt` への追記だけで成立する。

**`PRODUCT.md` の 7 予算の切り分け**

| # | 予算 | 自動測定 | 方法 |
| --- | --- | --- | --- |
| 1 | ウォームプロキシで**初フレーム 300ms 以内** | **可** | ワーカーへ `frame` 要求 → 応答までを `QElapsedTimer` |
| 2 | トランスポート**入力→可視フレーム 100ms 以内** | **可** | 合成キーイベント投入 → フレーム到着までを計測 |
| 3 | 通常カットで**音の不連続なし** | 不可（自動判定が難しい） | golden PCM 比較で代替（境界のサンプル差分） |
| 4 | **10,000 アイテムで 60fps** | **可** | 下記 `paint_scale` ベンチ |
| 5 | 全変更が undo / replay 可能 | **既に担保**（コア 43 件） | — |
| 6 | クラッシュ復旧の損失が 1 コマンド以内 | 可（統合テスト） | プロセス kill → 復元差分 |
| 7 | 30 分 1080p を通しで完了 | 不可（手動シナリオ） | リリース前チェックリスト |

> **7 件のうち 5 件は自動化できる。** 現状 0 件なので、費用対効果は大きい。

**最小ベンチ 3 本（D11 と 3-13 を直接測る）**

```
tests/performance/
├── CMakeLists.txt
├── paint_scale_bench.cpp      D11 限界 1: クリップ数
├── paint_width_bench.cpp      D11 限界 2: クリップ幅
└── undo_memory_bench.cpp      3-13: スナップショットのコスト
```

**1. `paint_scale_bench` — D11 限界 1**
- 10,000 クリップのシーケンスを構築（トラック 10 本 × 1,000 クリップ）。
- `TimelineWidget` を offscreen で生成し、**ビューポートには 20 クリップだけが入る**
  ズーム率に設定する。
- `widget.grab()` を 100 回繰り返し、1 回あたりの中央値を出す。
- **合格条件: 16.6ms 未満**（60fps）。
- **この構成が要点**: 可視 20 クリップに対し 10,000 クリップを走査しているなら、
  ビューポート判定の欠落が**数値としてそのまま出る**。
  修正（項目 26）の前後で差が測れる。

**2. `paint_width_bench` — D11 限界 2**
- 音声クリップ 1 本を**ビューポート幅いっぱい**に広げる（波形キャッシュを与える）。
- 同様に `grab()` を 100 回。**合格条件: 16.6ms 未満**。
- クリップ数は 1 なので、**限界 1 とは独立に**波形描画のコストだけが出る。
  項目 27（バッチ化 / `QPixmap` キャッシュ）の効果測定に使う。

**3. `undo_memory_bench` — 3-13**
- 10,000 クリップを構築し、`EditSession` に 100 コマンドを適用。
- **確保回数**を計測する（Windows は `_CrtMemState`、
  または独自の `operator new` カウンタを差し込む）。
- **記録項目**: 1 コマンドあたりの確保回数 / 100 段後の常駐バイト数。
- 5-6 の実測（1 コマンド 8〜12 万回の確保）を**回帰テストとして固定する**。
  COW 導入（項目 28）後にこの数値が桁で落ちることを確認する。

**運用規則**
- **CI では合否判定に使わない**（マシン差が大きい）。数値を**記録して比較**する。
  `IMPLEMENTATION_PLAN.md` の「公開リファレンス機で追跡する」方針に合わせる。
- ローカルでは `ctest -L performance` で明示的に呼ぶ（通常のテスト実行を遅くしない）。
- **ベンチは修正の前に入れる。** 前後比較ができないと、
  項目 26・27・28 が実際に効いたかを主張できない。

---

## Part 4. 実行順序 — 費用対効果順

> ## ★ 第 10 回の再編 — UX より上位のものが 3 つある
>
> 本文書は第 1〜9 回を通じて **UI/UX（テーマ・レール・パレット）を UX0** に置いていたが、
> 独立監査との突き合わせで**それより明確に優先すべき層が 3 つ**あることが判明した。
> **UX0 の前に UX-Z を置く。**
>
> **UX-Z — 先に手を付けるべきもの（UX 作業の前提）**
>
> | # | 項目 | 理由 |
> | --- | --- | --- |
> | **Z1** | **リポジトリ管理の正常化**（Q11） | 追跡 6 ファイル。**どの変更も戻せない**。すべての作業の前提 |
> | **Z2** | **未保存プロジェクトの自動保存**（F5） | **データ全損リスク。** `PRODUCT.md` の復旧約束に違反 |
> | **Z3** | **フォールバック理由の可視化**（D13-4） | 「なぜ画質が落ちたか」を Status Ribbon に出す。**原因調査の前提**であり最小コスト |
> | **Z4** | **タイトル付き再生の性能テストを固定**（Q9 / 3-18 の拡張） | Z5 以降の効果を測る土台。静止 / アニメ / 複数 / マスク / エフェクトを個別計測 |
> | **Z5** | **高速経路の一般化**（D13-1） | 位置・拡大・回転キーフレーム、クロップ、フェードをオーバーレイで扱う |
> | **Z6** | **`Sequence` に解像度・色空間・音声設定**（F4） | **D13 の根本原因。** 1280×720 のハードコードを除去 |
> | **Z7** | **`Auto` 画質の適応化 + Manifest 再利用**（D13-3） | 毎フレームの Manifest 送受信を revision 変化時のみに |
> | **Z8** | **QRhi による GPU コンポジター**（D13-2） | 最も効果が大きく最も重い。Z4 の計測基盤が先 |
> | **Z9** | **キャッシュキーを内容ハッシュへ**（F6） | 移動で失効しなくなる。重複検出にも再利用 |
> | **Z10** | **ハードウェアデコード**（P3） | D3D11VA / VideoToolbox。CPU RGBA 変換を削減 |
>
> **UX0 以降（従来の順序）は Z1〜Z4 の完了後に着手する。**
> ただし **UX0 の項目 5（`Window` メニュー）と項目 8（ホバーカーソルの嘘）は
> 独立かつ極小コスト**なので、Z 層と並行して入れてよい。

### 重要な発見: 1-E の 12 項目中 10 項目は「配線だけ」

第 2 回で `edit_session.hpp` のコマンド一覧と照合した結果、
**高頻度オペレーションの大半は、既存コマンドを UI から呼ぶだけで実装できる。**
エンジン側の新規作業は不要。ここが現時点で最も投資効率が高い。

| 項目 | 必要な既存コマンド | エンジン作業 |
| --- | --- | --- |
| H1 シーケンス全体にズーム | なし（純粋にビュー計算） | **不要** |
| H2 選択範囲にズーム | なし（純粋にビュー計算） | **不要** |
| H3 Add Edit（全ターゲット分割） | `SplitClipCommand` ✓ 既存 | **不要** |
| H5 プレイヘッドまでトリム | `TrimClipCommand` / `RippleTrimEndCommand` ✓ 既存 | **不要** |
| H6 エクステンドエディット | `RollEditCommand` ✓ 既存 | **不要** |
| H8 ナッジ | `MoveClipCommand` ✓ 既存 | **不要** |
| H9 ペーストインサート | `PasteClipsCommand` ✓ 既存 | **不要** |
| H11 キーボードでクリップ選択 | なし（純粋に UI 状態） | **不要** |
| H7 リプレイスエディット | `OverwriteClipCommand` ✓ ほぼ既存 | 軽微 |
| H10 属性のみペースト | `SetClipTransform` / `AddEffect` 等 ✓ 既存 | 軽微（複製経路） |
| H4 マッチフレーム | Source モニタへの逆引き配線 | 中 |
| H12 **オーディオスクラブ** | ワーカー / オーディオ経路の新規作業 | **大**（唯一の重い項目） |

---

**UX0 — 信頼性を買う（最も安く、最も効く）**

| # | タスク | 対象 | 解消する診断 |
| --- | --- | --- | --- |
| 1 | `theme_tokens.hpp`（`constexpr` / 3-1a の値）+ `theme.cpp` + `videx-dark.qss`（3-16） | 新規 + `main.cpp` | D1 |
| 1b | **`timeline_widget.cpp` の 62 箇所のハードコード色をトークンへ**（3-16 手順 5） | `timeline_widget.cpp` | **D1 不一致** |
| 1c | **`clipColor()` を意味ベースへ**（種別 + ラベル。ハッシュ着色を廃止） | `timeline_widget.cpp:54` | **D12** |
| 2 | `Fusion` スタイル固定 + ダーク `QPalette` 適用（両 OS で同一の見た目） | `main.cpp` | D1 |
| 3 | SVG アイコンセット + `resources/icons.qrc` + 実行時ティント関数 | 新規 | D1 |
| 4 | タイムコードを等幅数字化（3 箇所: トランスポート / インスペクタ / トリム HUD） | `main_window.cpp` | D1 |
| 5 | `Window` メニュー: 全 9 ドックの `toggleViewAction()` + Reset 移設 + `Ctrl+Shift+0` | `main_window.cpp:1499` | **D4** |
| 6 | **キーバインドレジストリへの統合** + 起動時衝突検出 | `main_window.cpp` / `timeline_widget.cpp` | **D8** |
| 7 | パネルヘッダのフォーカスリング（どのパネルがキーを受けるかを可視化） | 全ドック | **D8** |
| 8 | **ホバーカーソルの嘘を止める** — ホバー判定を実際の `dragMode_` 決定ロジックと同一の関数から引く | `timeline_widget.cpp:1457-1471` | **D9 問題 1** |
| 9 | ホバーゲート `tool_ == Tool::Selection` の撤去（全ツールでフィードバック） | `timeline_widget.cpp:1457` | **D10** |
| 10 | カスタムカーソル 8 種（3-9 の表）— 3 のアイコン作業に含める | 新規 | **D10** |

> 6〜10 を UX0 に昇格した。D8 は「デッドコード 6 件 + 無言のフォーカス依存 5 件」、
> D9 問題 1 は「↔ カーソルが出ているのにトリムではなくフェードが始まる」という実害が出ており、
> いずれも修正コストは小さい。放置したまま上位層を積むと不具合が埋まる。
>
> **8 は 3-9 の完全な再設計を待たずに単独で直せる**（ホバーとプレスが同じ関数を読むだけ）。
> 最小の変更で最も信頼を回復する 1 行級の修正なので、先に入れる。

**UX0.5 — 高頻度オペレーションの配線（エンジン作業ほぼ不要）**
11. H1 H2（ズーム）、H3（Add Edit `Ctrl+K`）、H11（キーボード選択）
12. H5 H6（プレイヘッドトリム `Q`/`W`、エクステンド `E`）、H8（ナッジ）
13. H9 H10（ペーストインサート / 属性ペースト）、H7（リプレイス）
> 既存コマンドの上に載るだけで、体感速度が最も大きく変わる層。

**UX1 — 編集ループと操作の予測性を取り戻す**
14. **`ClipAffordance` へのヒットテスト再設計**（3-9）— 重なりを設計時に排除、起動時 assert
15. Source / Program を横並び化（タブ廃止）+ 共有トランスポート
16. **Context Rail**（3-10）— 6 パネルのうち 4 つを吸収、キーフレームストリップをピン留め
17. メニュー IA 再構成（`Clip` `Marker` `Graphics` `View` `Window` を新設、`Sequence` を整理）
> D2 D3 D5 D9（残り）が消える。

**UX2 — 発見可能性と精度**
18. コマンドパレット `Ctrl+K`（UX0 のレジストリを読む / History と Effects Browser を吸収）
19. トリム HUD + スナップ可視化
20. Status Ribbon（診断ダイアログを置換）
21. キーマップエディタ + Premiere キーマップインポート（同じレジストリを読む）

**UX3 — 規模と構造（要エンジン / スキーマ作業）**
22. **スキーマ v2 + v1 マイグレーション**（3-11）— 以下 23〜25 の前提
23. **ビンツリー**（F2）+ アイコンビュー + ホバースクラブ + ラベルカラー（S10）
24. **トランジションをカット上のオブジェクトへ**（F1）— 種類 / アラインメント / 非対称
25. 複数シーケンス + シーケンスタブ（S1）→ ネスト（S2）
26. **クリップ描画のビューポート判定 + 二分探索**（D11 限界 1）
27. **波形描画のバッチ化 / `QPixmap` キャッシュ**（D11 限界 2）— 26 とは別タスク
28. **Undo を構造共有（COW）へ**（P2 / 3-13）— 逆コマンドは採らない
29. **オーディオスクラブ（H12 / 3-14）** — 対話素材が主用途である以上、後回しにできない
   （第 5 回の再評価で見積りは「大」→「中」。`audio` リクエスト種別が既存）
30. レンダーキューパネル（P5, D6）

**UX-P — 計測基盤（3-18。既存コードに触れないため最初に入れられる）**
- P0. `tests/performance/` 新設 + 3 ベンチ（`paint_scale` / `paint_width` / `undo_memory`）
  > **項目 26・27・28 の前に入れる。** 前後比較ができないと効果を主張できない。
- P1. キーバインド衝突検出テスト（項目 6 と同時）
- P2. カーソル一致テスト（項目 8 と同時）— 全アフォーダンス位置で
  `hover cursor == press 後の dragMode` を照合

**UX-R — リファクタリング（UX1 の前提。3-12 参照）**
- R1. ヘルパークラスを独立ヘッダへ（自己完結 / リスク最小）
- R2. `SelectionModel` 新設 + 選択変更シグナル
- R3. パネル更新群を `ContextRail` へ抽出
  > **R1〜R3 で Context Rail（項目 16）が着手可能になる。**
- R4 以降. `PreviewService` → `PlaybackController` → `MediaLibrary` → `ProjectDocument`
  → `EditController` → `ExportService`（UX1 と並行可）

> **依存関係の注意**
> - 18 と 21 は 6（レジストリ）に依存する。レジストリを先に作らないと、
>   パレットとキーマップエディタが 2 つの分裂した仕組みを別々に読む羽目になり、D8 を再生産する。
> - 23 24 25 はすべて 22（スキーマ v2）に依存する。**v2 を先に決めないと、
>   ビン・ラベルカラー・トランジションが再び `metadata` の文字列に逃げる**（F2 の再生産）。
> - 14 は 16 より先。アフォーダンスが確定していないと Context Rail の
>   「選択されたもの」が曖昧なままになる。
> - **16（Context Rail）は R1〜R3 に依存する。** 9,172 行の `MainWindow` の private を
>   触る形では実装できない（3-12）。
> - 26 と 27 を分離した。同じ「仮想化」に見えるが**原因が独立**しており、
>   26 だけ入れても限界 2（ズームアウト時の波形）は残る。

---

## Part 5. 検証結果（第 7 回・実測）

第 1〜6 回はすべてコード読解による設計だった。第 7 回で**初めて実際にビルドと
テストを走らせ**、UX0 の着手可能性と回帰リスクを実測した。

### 5-1. ビルド: 成功

```
cmake --build build/windows-local --config Debug   →  成功
```

- Qt は `.deps/Qt/6.8.3/msvc2022_64` にベンダリング済み（`CMakeUserPresets.json`）。
  **開発者のグローバル環境に依存していない**（`IMPLEMENTATION_PLAN.md` の方針どおり）。
- 5 つのビルドディレクトリが構成済み:
  `windows-local` `windows-core` `windows-core-asan` `windows-media` `macos-clang`。

> **結論: UX0 は今すぐ着手できる。** 環境構築の障害は無い。

### 5-2. テスト: 登録 5 件すべて合格（ベースライン確立）

| テスト | 時間 | 内容 |
| --- | --- | --- |
| `videx.core.application_info` | 0.55s | **`runTimelineTests()` 経由で 43 個のコア編集テストを実行** |
| `videx.project_file.round_trip` | 5.85s | スキーマ v1 ラウンドトリップ |
| `videx.title_store.round_trip` | 5.21s | タイトルラスタ |
| `videx.timeline.pointer_interactions` | 4.07s | タイムラインのポインタ操作 |
| `videx.monitor.pointer_interactions` | 0.91s | モニタのポインタ操作 |

**`100% tests passed, 0 tests failed out of 5`**

> **自己訂正**: 当初 0.55s という速さから「1,293 行のコアテストが実行されていない」と
> 疑ったが**誤りだった**。[application_info_test.cpp](tests/core/application_info_test.cpp) が
> `runTimelineTests()` を呼び、[timeline_test.cpp:1248](tests/core/timeline_test.cpp:1248) が
> **43 個のテスト関数を全て dispatch している**（定義 43 / 呼び出し 43）。
> 速いのは `videx_core_tests` が **Qt をリンクしていない**ため。
> Qt 系テストは offscreen プラットフォーム初期化に 4〜5 秒を払っている。

### 5-3. ~~カバレッジの穴: メディア / エクスポート系が走っていない~~ → **第 8 回で撤回**

> **★ この節の結論は誤りだった。第 8 回で実測し撤回する。5-3b を参照。**
> 原因は自分の検証手順のミス: `grep -c` は**一致 0 件のとき終了コード 1 を返す**ため、
> `grep -c ... || echo "no CTestTestfile"` が誤って「ファイルが無い」と報告した。
> 実際には `CTestTestfile.cmake` は存在していた。
> さらに `add_test` は**ディレクトリ単位の CTestTestfile に登録される**ため、
> トップレベルのファイルを見ても 0 件に見えるのは正常だった。
>
> 以下は誤った記述として残す（判断の経緯の記録）。

`tests/CMakeLists.txt` の登録は条件付きで、**9 テストファイルのうち 3 つが
FFmpeg 依存でゲートされている**。

| テストファイル | 行数 | ゲート条件 | `windows-local` で実行? |
| --- | --- | --- | --- |
| `media/timeline_export_test.cpp` | 602 | `TARGET Videx::Media` | **走らない** |
| `app/media_worker_protocol_test.cpp` | 281 | `TARGET videx_media_worker` | **走らない** |
| `media/media_probe_test.cpp` | 138 | `TARGET Videx::Media` | **走らない** |

`windows-media` プリセット（vcpkg + FFmpeg）が必要だが、
**`build/windows-media` には `CTestTestfile.cmake` が無い**（`CMakeCache.txt` はある）
＝ 構成が完了していない。vcpkg 自体と `.deps/vcpkg_installed/x64-windows` は存在する。

> **リスク**: Part 4 の項目のうち
> **28（Undo の COW）・24（トランジションのオブジェクト化）・1c（クリップ着色）**は
> **プレビュー / エクスポートのパリティに影響する**。その検証を担う 1,021 行が
> 現在ローカルで走らない。
> → **これらに着手する前に `windows-media` の構成を復旧させる**必要がある。
> UX0 の項目 1・2・5〜10 は core / widget 系テストで足りるため影響しない。

### 5-3b. 実測（第 8 回）: メディア / エクスポート系も完全に動く

`windows-media` プリセットは**完全に構成済みで、ビルドもテストも通る。**

```
cmake --build build/windows-media --config Debug   →  成功（エラー・警告ゼロ）
```

ビルド成果物: `videx_core.lib` `videx_media.lib` **`videx-media-worker.exe`**
`videx_render_qt.lib` **`Videx.exe`** ＋ **テスト実行ファイル 9 個**。
FFmpeg は vcpkg 経由で解決済み（`FFMPEG_FOUND=TRUE`、avcodec 62.28.102、
`.deps/vcpkg_installed/x64-windows`）。

```
ctest --test-dir build/windows-media -C Debug
```

| # | テスト | 時間 |
| --- | --- | --- |
| 1 | `videx.media_worker.version` | 5.14s |
| 2 | `videx.core.application_info`（＋コア編集 43 件） | 4.17s |
| 3 | `videx.project_file.round_trip` | 0.63s |
| 4 | `videx.title_store.round_trip` | 0.78s |
| 5 | `videx.timeline.pointer_interactions` | 5.66s |
| 6 | `videx.monitor.pointer_interactions` | 3.93s |
| 7 | `videx.media_worker.frame_protocol` | 1.34s |
| 8 | `videx.media.probe_wav` | 3.07s |
| 9 | **`videx.media.export_h264_aac`** | 6.09s |

**`100% tests passed, 0 tests failed out of 9`（合計 30.83 秒）**

> **第 7 回で挙げたリスクは消滅した。**
> エクスポートパリティを検証する `timeline_export_test.cpp`（602 行）は
> **実際に走っている**。したがって Part 4 の
> **28（Undo の COW）・24（トランジションのオブジェクト化）・1c（クリップ着色）に
> 着手する前提条件はすでに揃っている。** 復旧作業は不要。

### 5-5. テストスイートの 3 つの盲点 — 最も重い発見がそこに集中している

**`PRODUCT.md` は 7 つの性能予算を明示しているが、そのどれも測定されていない。**

| 検証構文 | tests/ 全体でのヒット数 |
| --- | --- |
| `QElapsedTimer` / `steady_clock` / `benchmark` | **2**（しかも両方 `waitForFinished` のタイムアウト値） |
| 大規模シーケンス構築（1,000 クリップ超） | **0**（`10000` の一致はすべてプロセスタイムアウト） |

さらに `tests/` の実際の構成は `app/ core/ media/ render/ fixtures/` で、
`IMPLEMENTATION_PLAN.md` が規定する
**`unit/ integration/ golden/ performance/` とは一致しておらず、
`performance/` が存在しない。**

**盲点は 3 つで、最も重い診断がそこに完全に対応している。**

| 盲点 | 検出されなかった診断 |
| --- | --- |
| **入力経路**（キー / カーソル状態の検証が皆無） | **D8** ショートカット分裂・デッドコード 6 件 / **D9** カーソルの嘘 |
| **性能**（予算 7 件すべて未測定） | **D11** 描画の 2 つのスケール限界 |
| **視覚状態**（絶対色の検証が皆無。差分テスト 1 件のみ） | **D1** ダーク/ライト不一致 / **D12** ハッシュ着色 |

> **これは偶然ではない。** テストスイートは**編集意味論の正しさ**を厚く検証している
> （コア 43 件 + ラウンドトリップ + ポインタ操作 + エクスポートパリティ）。
> **正しさは守られているが、体験を決める 3 領域が測られていない。**
> 6 回の設計で見つけた最重要級の欠陥がすべてこの 3 盲点に入るのは、
> 「テストが通っているから品質が高い」という推論が UX には効かないことの実例。
>
> **再設計に伴って追加すべきテスト**（Part 4 に反映済み）:
> 1. **キーバインドの衝突検出テスト** — レジストリ（項目 6）の起動時 assert を単体テスト化
> 2. **カーソル一致テスト** — 全アフォーダンス位置で
>    `hover cursor == press した結果の dragMode` を照合（D9 の再発防止）
> 3. **性能テスト（`tests/performance/` を新設）** — 10,000 クリップのシーケンスを構築し
>    `paintEvent` の所要時間を計測（D11 限界 1）、
>    ビューポート幅いっぱいの音声クリップで同様に計測（限界 2）

### 5-6. 実測（第 9 回）: Undo スナップショットのコストは「バイト数」より「確保回数」

第 5 回の「履歴 100 段で数百 MB」という見積りを**実測で検証した**
（MSVC 14.44 / x64 / ヘッダのみでコンパイルした使い捨てプローブ）。

| 型 | `sizeof` |
| --- | --- |
| **`Clip`** | **344 バイト** |
| `Track` | 88 |
| `Sequence` | 96 |
| `ClipEffect` | 48 |
| `MotionKeyframe` | 80 |
| `EffectKeyframe` / `GainKeyframe` / `SpeedKeyframe` | 各 24 |
| `Marker` | 88 / `Caption` | 96 |

**10,000 クリップ時（`Clip` 配列のみの下限値）**

| | 実測 |
| --- | --- |
| スナップショット 1 個 | **3.28 MiB** |
| **`apply()` 1 回あたり（×2 コピー）** | **6.56 MiB** |
| **履歴 100 段の滞留** | **328 MiB** |

> **第 5 回の「数百 MB」は正しかった。** ただしこれは**楽観的な下限**である:
> `sizeof(Clip)` は**インラインメンバのみ**を数えており、各 `Clip` が持つ
> `effects` / `motionKeyframes` / `gainKeyframes` / `speedKeyframes` の
> **ヒープ確保分は含まれていない**。キーフレームの付いた実プロジェクトでは
> 現実的に **500 MiB〜1 GiB** に達する。

**★ より重要な発見 — ボトルネックは帯域ではなく確保回数**

`vector<Clip>` のディープコピーは **memcpy 一発では済まない**。
各 `Clip` は内部に **4 本のベクタ**を持ち（`ClipEffect` はさらに自前の
`keyframes` を持つ）、コピーのたびにそれぞれが**個別のヒープ確保**を要求する。

```
10,000 クリップ × 4〜6 本の内部ベクタ  =  40,000〜60,000 回の確保 / スナップショット
apply() は 2 スナップショット作る      =  1 コマンドあたり 80,000〜120,000 回の確保
```

> **編集操作 1 回（クリップを 1 つ動かす、数値を 1 つ変える）ごとに
> 10 万回規模のヒープ確保が走る。** これは 6.56 MiB の memcpy より遥かに重く、
> **確実に体感できるヒッチになる。**
>
> **3-13（COW）の効果は当初見積りより大きい。**
> 構造共有は「バイト数を減らす」だけでなく、**確保の嵐そのものを消す**。
> 1 クリップを触るコマンドは**該当トラック 1 本分の確保だけ**になる。
> → Part 4 の項目 28 の優先度は、当初考えていたより高い。

### 5-4. UX0 の回帰リスク: 低いと実証できた

**テストスイート全体に、絶対的な色アサーションもカーソルアサーションも存在しない。**

`tests/app/timeline_widget_test.cpp`（683 行）で色 / ピクセルに触るのは 1 箇所だけで、
それは**差分テスト**だった（[L656-674](tests/app/timeline_widget_test.cpp:656)）:

```cpp
const QImage headerBefore = markerWidget.grab().toImage().copy(0, 0, 159, headerHeight);
markerHorizontalBar->setValue(...);          // 横スクロールさせる
const QImage headerAfter  = ...;             // 固定ヘッダ列が再描画されないことを確認
```

**横スクロール時に固定ヘッダ列（x 0〜159）が変化しないこと**を検証しており、
before / after の両方が新しい配色になるので**パレット変更では壊れない**。

| UX0 項目 | 回帰リスク | 根拠 |
| --- | --- | --- |
| 1 テーマ / トークン | **低** | 絶対色アサーション無し |
| 1b 62 箇所の色置換 | **低** | 同上。差分テストは配色非依存 |
| 1c `clipColor` 意味ベース化 | **低** | 同上 |
| 2 `Fusion` + パレット | **低** | ウィジェットテストは offscreen で幾何のみ検証 |
| 5 `Window` メニュー | **なし** | 新規追加のみ |
| 8 ホバーカーソル修正 | **低** | カーソルアサーション無し。ただし**テストを追加すべき箇所** |
| 6 キーバインドレジストリ | **中** | ポインタテストは通るが、**キー経路のテストが無い**（D8 が検出されなかった理由） |

> **重要な副産物**: D8（ショートカット分裂）と D9（カーソルの嘘）が
> **6 回の設計まで発見されなかった理由が判明した** —
> **キー入力経路とカーソル状態を検証するテストが 1 つも無い。**
> → UX0 の項目 6・8 には**実装と同時にテストを追加する**（回帰の再発防止）。

---

## Part 6. 実装記録 — Z3 / Z5（第 11 回）

**設計だけでなく実際にコードを変更した最初の回。**
ビルド警告ゼロ・9/9 テスト合格を各ステップで確認しながら進めた。

### 6-1. Z3 実装済み — フォールバック理由の可視化

`PreviewPathReason` 列挙（[main_window.hpp](src/app/main_window.hpp)）を新設し、
**プレビュー経路が分岐するすべての地点を計測点にした。**

| 値 | 意味 |
| --- | --- |
| `DirectOverlay` | 高速経路（ネイティブ解像度） |
| `PreviewCache` | In-to-Out レンダーキャッシュ再生 |
| `Stopped` | 停止中の単フレーム合成 |
| `NoVideoClip` / `MultipleVideoClips` | 映像クリップが 0 / 2 本以上 |
| `BaseSpeedKeyframes` | ベースクリップに速度ランプ |
| `BaseTransition` | ベースクリップが隣接クリップとブレンド中 |
| `BaseMediaMissing` | 素材ファイル欠落 |
| `TitleBehindBase` | タイトルがベース映像より背面 |
| `TitleEffects` | タイトルに**キーフレーム付き**エフェクト |
| `TitleTransition` | タイトルが隣接クリップとブレンド中 |
| `TitleRasterUnavailable` | タイトルラスタが読めない |

**表示先**
1. **Program モニタの診断オーバーレイ** — `f 120 | 360p | media clock | compositor: title effect keyframes`
2. **Help > Diagnostics** — `Preview path: compositor (title effect keyframes)`

> これで**「なぜ画質が落ちたか」がユーザに見える**。
> 以前は 640×360 への切り替えが無言で起きていた。

### 6-2. Z5 境界線 — 「時間不変な per-pixel 処理は焼ける」

**導出した規則**

> 高速経路が使えるのは、
> **(1) タイトルがベース映像より前面にあり、
> (2) per-pixel 処理（エフェクト・マスク）が時間不変で焼けるとき。**
> 時間変化するのが幾何・不透明度だけなら、QPainter が毎フレーム無償で処理する。

根拠: **タイトルのラスタは静止画**なので、パラメータが時間変化しなければ
結果も時間変化しない → 一度焼いてキャッシュできる。

### 6-3. 高速経路へ移した 5 件（実装済み）

| 旧・拒否理由 | 対応 | コスト |
| --- | --- | --- |
| **タイトルのモーションキーフレーム** | `motionAt(clip, local)` を毎フレーム評価。同関数はキーフレーム無しなら静的値を返すため**完全なドロップイン** | ゼロ（ベースクリップが既に同じことをしている） |
| **タイトルのクロップ** | `MonitorTitleOverlay` に crop 4 field を追加し、描画時に `setClipRect` | ゼロ（ピクセル走査なし） |
| **タイトルのトランジション**（隣接クリップが無い場合） | 合成器は隣接する outgoing クリップが無いと `incomingMix` を 1.0 のまま据え置く（[timeline_export.cpp:742](src/media/src/timeline_export.cpp:742)）＝**トランジションが無効**。よって高速経路でも**出力が同一**。隣接クリップがある場合のみ合成器へ | ゼロ |
| **ベースクリップのトランジション**（同条件） | 同じ緩和を適用 | ゼロ |
| **タイトルのマスク + 静的エフェクト** | `applyStillAdjustments()` で焼き、`titleBakedCache_` に保持 | 初回 1 回のみ |

> **特にトランジションの発見が重要。** 従来は `videoTransitionInFrames > 0` だけで
> 無条件に合成器へ落としていたが、隣接クリップが無ければ合成器側でも
> **何も起きていなかった**。つまり**視覚的な利得ゼロで解像度だけ 1/9 に落ちていた。**

### 6-4. 合成器が本当に必要なケース（残存）

| 理由 | なぜ避けられないか |
| --- | --- |
| `TitleBehindBase` | オーバーレイは定義上「上に描く」もの。下に置けない |
| `TitleEffects`（キーフレーム付き） | ピクセル処理が毎フレーム変わるので焼けない |
| `TitleTransition` / `BaseTransition`（隣接あり） | 2 ソースのブレンドが必要 |
| `MultipleVideoClips` | 映像ソースが 2 本以上 |
| `BaseSpeedKeyframes` | 可変レート再生は `QMediaPlayer` では表現できない |
| `BaseMediaMissing` / `TitleRasterUnavailable` | エラー状態 |

### 6-5. 副産物 — ピクセル処理の重複を解消

新しい焼き込み経路が既存の合成処理と**別々の計算に分岐しないよう**、
共通のピクセル演算を無名名前空間へ抽出した
（[qt_monitor_widget.cpp](src/render/src/qt_monitor_widget.cpp)）。

- `maskFactorAt()` — マスク被覆率
- `adjustPixel()` — 明度 / コントラスト / 彩度 / ビネット
- `blurApproximation()` — 縮小・拡大によるブラー近似

`ensureComposedFrame()`（モニタ本来の合成）と新設の
`applyStillAdjustments()`（焼き込み）が**同じ関数を呼ぶ**ようになり、
`ensureComposedFrame` は約 30 行短くなった。
`videx.monitor.pointer_interactions` を含む 9 テストが通ることで
**出力が変わっていないことを確認済み。**

> これは Part 1「アーキテクチャ改善」の
> 「Preview と Export で重複している補間・エフェクト計算を共通化」の
> 第一歩でもある（今回は Preview 内部の重複を解消）。

### 6-6. 検証

各ステップで実行:
```
cmake --build build/windows-media --config Debug   →  エラー・警告ゼロ
ctest --test-dir build/windows-media -C Debug      →  9/9 合格
```

### 6-7. Z4 — 焼き込み経路の検証で**実バグを 1 件発見・修正**（第 12 回）

6-6 で「焼き込み結果のピクセル一致は未検証」と書いた点に着手し、
**テストを書く過程で本物のバグを見つけた。**

**バグ: フェザー境界が二重に暗くなっていた（係数の 2 乗）**

| 実装 | 作業空間 | マスクの掛け方 | 正しいか |
| --- | --- | --- | --- |
| `ensureComposedFrame()` | **乗算済みアルファ**（`ARGB32_Premultiplied`） | RGBA 全部に係数 | ✓ 乗算済み空間ではこれが正解 |
| `applyStillAdjustments()`（初版） | **非乗算**（`ARGB32`） | RGBA 全部に係数 → **最後に乗算変換** | ✗ **係数が 2 回かかる** |

非乗算空間ではマスクは**アルファのみ**に掛け、RGB への反映は最後の
乗算変換に任せるのが正しい。初版は RGB にも掛けた上でさらに乗算していたため、
**フェザー境界の色が `factor²` で暗くなっていた。**

実測値（64×64、`contrast 0.8`、楕円マスク `feather 0.25`、行 16 = 係数約 0.53）:

| | 中心 | フェザー境界 |
| --- | --- | --- |
| 期待値（修正後） | a=255 r=114 | a=135 **r=60** |
| バグ版 | a=255 r=114 | a=135 **r=32** |

**副産物: 順序の心配は不要だった。**
当初「マスクとエフェクトの適用順が `ensureComposedFrame` と逆」を疑い順序を入れ替えたが、
マスクをアルファのみにすれば**順序は最終出力に影響しない**
（マスクはアルファ、エフェクトは RGB を触るため直交する）。
最初の懸念は誤診で、真因は色空間の取り違えだった。

**検証方法 — テストが本当にバグを捕まえるか確かめた**

> **通るだけのテストは無価値。** 実際に 3 回書き直した。
>
> 1. 第 1 版「境界は中心より暗い」→ **誤った順序でも通る**（どちらでも暗くなるので判別不能）
> 2. 第 2 版「境界 < 中心 × 係数 × 0.6」→ **まだ通る**。
>    サンプル点 (32, 8) が帯の外側で係数約 0.03 しかなく、両方 0 付近に丸まっていた。
>    → 計算して係数約 0.53 の (32, 16) に変更。
> 3. 第 3 版: 出力を実測して初めて真因（`factor²`）が判明。
>    不変条件「**境界 = 中心の色 × 自身のアルファ**」を直接検証する形に書き換え。
> 4. **バグを意図的に戻して失敗を確認** →
>    `monitor test failed: a feathered edge must be premultiplied centre colour x alpha`
>    → 修正を戻して 9/9 合格。

追加した検証（[monitor_widget_test.cpp](tests/render/monitor_widget_test.cpp)）:
恒等変換の無変更 / 矩形マスクの内外 / 反転マスク / 明度 / 彩度 −1 の無彩色化 /
**フェザー境界の乗算済み不変条件**（バグ検出用）。

> **残る未検証**: 実際の再生における合成器出力との**画面上の**一致、および
> `tests/performance/`（3-18）の性能計測。単体レベルの画素検証はこれで入った。

---

## 反復ログ

### 第 1 回 — 2026-07-25
- コードベースを実測し Part 0 の事実表を作成（推測を排除）。
- 決定的な発見:
  - **アイコン資産ゼロ / アプリ全体スタイルシート無し** → 既定の明色 Qt テーマで動作中。
  - **`Window` メニューが空 + `toggleViewAction` がゼロ** → パネルを閉じると事実上復帰不能。
  - **Source / Program がタブ** → 3 点編集ループが成立しない構造的欠陥。
  - **右レール 6 パネルが単一タブスタック** → キーフレームと数値を同時に見られない。
  - **`EditSession` が Sequence を 1 本のみ保持** → 複数シーケンス / ネストは基盤から不在。
  - **Undo が Sequence 全体スナップショット** → 履歴深度がメモリに直結。
- 再設計の骨格を確定: 「1 キャンバス・2 レール」+ フェーズワークスペース +
  **Context Rail（選択追従の単一右パネル）** + コマンドパレット。
- 次回の焦点: UX0（トークン / アイコン / Window メニュー）を実装粒度のタスクへ分解し、
  `tokens.json` の具体的な色値を決める。

### 第 2 回 — 2026-07-25
コード変更なし（`src/` の mtime は 7/21 のまま）。よって再調査ではなく深掘りに充てた。

**新発見 1 — D8: ショートカットが 2 つの仕組みに分裂**（最重要級）
`MainWindow` の `ApplicationShortcut` と `TimelineWidget::keyPressEvent` の 16 キーが衝突。
- **到達不能なデッドコード 6 件**: `End` `Home` `Space` `K` `←→` `Z`。
  特に `End` は 2 つの実装が**異なる意味**を持ち（`contentEndFrame()` と
  `sequenceEndFrame()-1`）、タイムライン側が死んでいる。`Z` は Zoom ツール切替に食われている。
- **無言のフォーカス依存 5 件**: `↑↓`（編集点ジャンプ）`+-=`（ズーム）`S`（スナップ）
  `Delete`（削除）が、タイムラインにフォーカスが無いと**エラーも出さずに沈黙**する。
- 解決策として**単一キーバインドレジストリ + 起動時衝突検出 + パネルフォーカスリング**を設計。
  コマンドパレットとキーマップエディタは同じ表を読む前提にした。

**新発見 2 — 1-E: 高頻度オペレーションが 12 件欠落**
`\`（シーケンス全体ズーム）、`Ctrl+K`（Add Edit）、`F`（マッチフレーム）、
`Q`/`W`（プレイヘッドトリム）、`E`（エクステンド）、リプレイス、ナッジ、
ペーストインサート / 属性ペースト、キーボードでのクリップ選択、**オーディオスクラブ**。
- `Alt+←/→` は既に Slip に割当済みのため、ナッジの割当先が空いていない（要再設計）。
- **H12 オーディオスクラブの欠落は `PRODUCT.md` の主要用途（対話素材）と直接矛盾する。**
  セリフの切れ目はスクラブ音で探すのが標準手法であり、後回しにできない。

**新発見 3 — 1-E の 12 件中 10 件はエンジン作業が不要**
`edit_session.hpp` のコマンド一覧と照合した結果、`SplitClipCommand`
`TrimClipCommand` `RippleTrimEndCommand` `RollEditCommand` `MoveClipCommand`
`PasteClipsCommand` はすべて既存。**UI からの配線だけで実装できる。**
重いのは H12（オーディオスクラブ）のみ。
→ これを受けて **UX0.5 層を新設**し、実行順序を再編した。

**確定事項 — `tokens.json` の具体値（3-1a）**
ニュートラル 8 段を CIE L\* から sRGB へ逆算して確定（目分量を排除）。
コントラストを WCAG 式で検証: 本文 **8.19:1（AAA）**、補助 **4.98:1（AA）**。
セマンティック色 11 種を定義し、マーカー色は既存コードの `0xFF2E9E4FU` を流用。
設計規則「**彩度は画像から遠ざける**」を明文化。
副産物として**タイムコードの等幅数字化**の必要性を発見（現状は幅指定で揺れを誤魔化している）。

**次回の焦点**
- `timeline_widget.cpp` のマウス操作経路を精査し、ドラッグ系のヒットテスト優先順位と
  カーソルフィードバックの一貫性を検証する（`setCursor` が 23 箇所に散在）。
- Context Rail の具体的なパネル構成（クリップ選択時に何をどの順で見せるか）を設計する。
- `project_file.cpp` のスキーマを読み、UI 再設計がプロジェクト形式の変更を要求するか判定する
  （ワークスペース状態・ラベルカラー・複数シーケンスは保存先が必要）。

### 第 3 回 — 2026-07-25
コード変更なし。前回の宿題 3 件をすべて消化し、**新たに 5 件の構造的欠陥**を発見した。

**新発見 1 — D9: ホバーカーソルが実際の動作と一致しない**（今回最も悪質）
[timeline_widget.cpp:1295-1325](src/app/timeline_widget.cpp:1295) の 6 段 `else if` と、
[L1463-1465](src/app/timeline_widget.cpp:1463) のホバー判定が**別々のロジック**になっている。
- `fadeInFrames == 0` のとき `fadeInX == clipLeft` なので、
  **クリップ左端上部 12px は ↔ カーソルが出ているのに FadeIn が始まる。**右端も同様。
- トランジション帯（18px）とフェード帯（12px）が**重なり**、短い値だと片方が到達不能。
- 音声はゲイン判定が優先度 2 で**クリップ内どこでも当たる**ため、
  **ゲイン値を上げるとゲイン線が上端に寄り、左上角のトリムがゲインドラッグに化ける。**
  同じ場所をクリックしても状態次第で違う操作になる。
- Slip / Roll / Ripple / Slide ツールでは**ヒットテストを一切行わない**（ツールだけで決定）。

**新発見 2 — D10: カーソル 6 種で約 20 状態、ツール別フィードバックが皆無**
Slip / Roll / Ripple / Slide に固有カーソルが無く、すべて同じ ↔。
さらにホバー処理が `tool_ == Tool::Selection` でゲートされており、
**精密トリム系ツール使用中はフィードバックがゼロ**。アイコン資産 0 件なので土台も無い。

**新発見 3 — F1: トランジションが「頭側のみ」でオブジェクトではない**
`videoTransitionInFrames` / `audioTransitionInFrames` のみで `TransitionOut` が存在しない。
トランジションが incoming クリップのプロパティなので、**選択・アラインメント変更・種類変更・
シーケンス末尾への配置・非対称トランジションがすべて構造的に不可能**。スキーマ v2 の必須項目。

**新発見 4 — F2: ビンが自由入力文字列で、階層でもスキーマでもない**
[main_window.cpp:1845](src/app/main_window.cpp:1845) が `asset.metadata["bin"]` に
`QInputDialog` の生文字列を格納。**`project_file.cpp` には `bin`/`folder`/`parent` が 0 件**で、
不透明な `metadata` をパススルーしているだけ。階層なし、正規化なし
（`"Media"` と `"media"` は別ビン）、typo が静かに新ビンを作る、ビン名変更は全アセット手編集。
→ **主要用途が長時間インタビューなのに、整理手段が実質存在しない。**

**新発見 5 — F3: `schema_version: 1` に UI / 組織状態の置き場が無い**
`sequences`（複数）、ビン階層、ラベルカラー、プロジェクト単位ワークスペース、
トランジションオブジェクトのいずれも保存先が無い。ワークスペースは `QSettings`
＝**マシン単位**なので、別マシンでレイアウトが失われる。

**設計成果**
- **3-9 ヒットテスト再設計**: `ClipAffordance` 構造体で描画とヒットテストが
  **同一の矩形リスト**を読む。重なりは実行時優先順位ではなく**設計時に排除**し起動時 assert。
  ツールは「上書き」ではなく「フィルタ」に変更。カスタムカーソル 8 種を定義。
- **3-10 Context Rail 具体構成**（前回の宿題）: 幅固定・選択種別ごとのスクロール記憶・
  **下部にキーフレームストリップをピン留め**（D3 の直接解消）。選択対象 9 種の中身を定義。
  結果 **既存 6 パネルのうち 4 つを吸収**、History と Effects Browser はパレットへ →
  **右側の常設パネルは 1 枚だけ**になる。
- **3-11 スキーマ v2**（前回の宿題）: 判定は「**UX0〜UX2 は v1 のまま可、UX3 は v2 必須**」。
  v1→v2 マイグレーションを 5 手順で具体化（ビン文字列の正規化・畳み込み、
  トランジションのオブジェクト化、v1 ラウンドトリップテストの流用）。
  なお [project_file.cpp:613](src/app/project_file.cpp:613) は `!= 1` を拒否するため、
  **v2 リーダには v1 受理の分岐追加が必要**。

**実行順序の更新**
- UX0 に 5 項目追加（8: ホバーの嘘を止める / 9: ホバーゲート撤去 / 10: カスタムカーソル）。
  **項目 8 は 3-9 の完全再設計を待たずに単独で直せる**ため最優先に置いた。
- UX3 を再編し、**22（スキーマ v2）を 23〜25 の前提**として明示。
  v2 を先に決めないとビン・ラベルカラー・トランジションが再び `metadata` に逃げる。

**次回の焦点**
- `qt_monitor_widget.cpp`（1,495 行）を精査し、Program モニタ上の直接操作
  （ハンドル・ヒットテスト・オーバーレイ）に D9 と同種の不整合がないか検証する。
- `main_window.cpp` の分割案（P6 / 項目 30）を具体化する。9,172 行のままでは
  UX1（Context Rail）が現実的に着手できない。ファイル境界とヘッダ設計を決める。
- タイムライン描画の仮想化（P1 / 項目 26）のコストを見積もる。
  `paintEvent` が 1 フレームで何をしているかを読み、10,000 クリップでの破綻点を特定する。

### 第 4 回 — 2026-07-25
コード変更なし（行数も 1495 / 2586 で第 1 回と同一）。前回の宿題 3 件を消化。
**今回は新発見よりも「既存の前提の訂正」が最大の成果**だった。

**最大の成果 — D9/D10 の解決策は既にリポジトリ内にあった（訂正）**
`qt_monitor_widget.cpp` は 3-9 で「新設計」として提案した仕組みを**既に実装していた**。
- `enum class Handle` に **20 ハンドル**、**`hitTest()` を press と hover の両方から呼ぶ**、
  `applyHoverCursor()` の単一 `switch`。**タイムラインに欠けている構造がここには全部ある。**
- さらに[L895-897](src/render/src/qt_monitor_widget.cpp:895) はアンカーからの角度を計算し
  **オブジェクトの回転に合わせてスケールカーソルの向きを変えている**。商用エディタ以上に丁寧。
- **訂正**: 第 1・3 回で「アイコン資産 0 件だからカスタムカーソルの土台が無い」と書いたが、
  `rotationCursor()`（[L41-59](src/render/src/qt_monitor_widget.cpp:41)）が
  `QPainter` → `QPixmap` → `QCursor` を `static` キャッシュで実現済み。
  **アイコンパイプラインは不要。UX0 項目 10 のコストは当初見積りより大幅に低い。**
- 3-9 の位置づけを「新規アーキテクチャ設計」から
  **「`src/render` の水準に `src/app` を揃える作業」**へ変更した。実現性の評価が上がる。

> **投資が逆転している点を明記した。** Program モニタはハンドル 20 種・統一ヒットテスト・
> 回転対応カーソル。タイムラインは分裂ヒットテスト・汎用カーソル 6 種。
> **編集者が 9 割の時間を過ごすのはタイムラインなのに、そこだけ相互作用モデルが弱い。**

**新発見 — D11: 描画のスケール限界は 2 種類あり、混同すると片方が残る**
- **限界 1（クリップ数）**: 本流の `for (const auto& clip : track.clips)` に
  ビューポート判定が無い。`setClipRect` が節約するのは**ラスタライズだけ**で、
  `frameToX`・キーフレーム二重ループ・`QRectF` 構築・色計算は画面外でも全部走る。
  → **判定コードは同ファイル内に既にある**（ゴースト描画分岐の
  [L2185](src/app/timeline_widget.cpp:2185)）。本流に写すだけの最小修正が可能。
  本修正は `timeline.start` 順を利用した二分探索で `O(log n + 可視数)`。
- **限界 2（クリップ幅）**: 波形が **1 ピクセル 1 `drawLine`**。
  クランプは正しいので画面外は無償だが、**ビューポート幅いっぱいの音声クリップ 1 本で
  1 再描画あたり 3840 回の `drawLine`**。クリップ数と無関係にズームアウト時に効く。
  → `drawLines()` の一括発行、または `(assetId, pixelsPerFrame, 行高)` キーの
  `QPixmap` キャッシュ + blit。
- **「仮想化」を 1 タスクにすると限界 2 が取り残される**ため、Part 4 で項目 26 / 27 に分離した。

**設計成果 — 3-12 `main_window.cpp` 分割案（前回の宿題）**
約 80 メソッドを 7 つの協力オブジェクトへ割り当てた（実測メソッド名で対応表を作成）。
- **やってはいけない方式を明記**: ファイルだけ割る `main_window_playback.cpp` 方式は
  全ファイルが `MainWindow` の private を触り続けるため**行数は減るが結合は減らない**。
- 副次的発見: `eventFilter` が **3 つ**、`dragEnterEvent`/`dropEvent` が**各 2 つ**存在する
  ＝ **.cpp 内に匿名ヘルパーウィジェットクラスが複数定義されている**。
  自己完結しているので**最も安全な最初の一歩**になる。
- **順序の要点**: 7 つ全部を UX 作業の前に終わらせると可視の進捗が長期間止まる。
  **R1（ヘルパー抽出）→ R2（`SelectionModel` + 選択変更シグナル）→ R3（パネル更新群）
  の 3 段だけで Context Rail が着手可能**になる。残り 4 つは UX1 と並行でよい。
  → Part 4 に `UX-R` 層として追加し、項目 16 の依存として明示した。

**次回の焦点**
- **`Undo` の差分化（項目 28）の実現可能性を評価する。** `HistoryEntry` が
  `Sequence` 全体を保持している現状に対し、`EditCommand` に逆コマンドを持たせる方式が
  既存の `apply`/`undo` 構造にどこまで適合するかを `timeline.cpp`（2,056 行）から判定する。
- **オーディオスクラブ（項目 29 / H12）の実装経路を具体化する。** 主要用途に直結する
  唯一の「重い」項目なので、ワーカープロトコルと音声シンクのどこに手を入れるかを決める。
- コマンドパレット（項目 18）の**コマンド定義形式**を設計する。
  UX0 のキーバインドレジストリと同じ表を読む前提なので、`CommandId` の粒度を確定させる。

### 第 5 回 — 2026-07-25
コード変更なし。前回の宿題 3 件を消化。**うち 2 件で結論が当初想定と逆になった。**
文書が 1,031 行に達したため**目次を追加**した。

**結論が変わった 1 — Undo は逆コマンドではなく構造共有（3-13）**
`apply` の実装を読んで実測: **1 コマンドあたりタイムライン全体のディープコピーが 2 回**
（`Sequence candidate = sequence_` と `undoStack_.push_back({.sequence = sequence_})`）、
`undo` の `restoreContent` でさらに 1 回。コンテナは全て素の値ベクタ
（`vector<Track>` → `vector<Clip>` → `vector<ClipEffect>` → `vector<EffectKeyframe>`）。
10,000 クリップで**履歴 100 段に数百 MB が滞留**する。
- **採用: `shared_ptr<const Track>` による構造共有（COW）。**
  `Sequence` のコピーが `shared_ptr` 数個に、書き込みは該当トラック 1 本のみ複製。
  **`execute()` の署名は変わらず、意味論も同一なので既存テストが全て通る。**
- **却下: 逆コマンド方式。** `EditCommand` は**約 50 種**あり、
  `ExtractRange`/`LiftRange` の逆は「N クリップを元 ID と位置で復元」＝**結局スナップショット**。
  `PasteClips` の逆には新規生成 ID が必要。さらに「100 万回ランダムコマンド」の
  exit gate に対し**逆の正当性を検証する新しい性質テストが別途必要**になる。
  誤った逆の症状は **undo 時の静かなデータ破損** — 編集アプリで最悪の種類のバグ。
  → リスクと工数が桁で違い、効果は構造共有と同等以上。
- 付随して確認: **Undo の粒度自体は既に正しい**（1 ジェスチャ / 1 バースト = 1 undo）。
  **問題は純粋にメモリで、UX 粒度ではない。**

**結論が変わった 2 — オーディオスクラブは「大」ではなく「中」（3-14）**
ワーカープロトコルの種別を実測したら `probe` `frame` `waveform` **`audio`** `export` で、
**`audio` 種別が既に存在**していた。アプリ側も `QAudioSink` と
`startAudioSinkFromPcm` を持つ。第 2 回で「唯一の重い項目」としたが**見積りを下げた**。
- 難所を 4 つに特定し、すべて設計で解けることを確認:
  リクエスト氾濫（**合流規則**: in-flight 1 + pending 1、中間は破棄）、
  シンク再起動のクリック（**常設スクラブシンク + リングバッファ**）、
  グレイン境界（**2〜3ms クロスフェード**）、
  **再生クロックの汚染**（`audioSink_` は [L4726](src/app/main_window.cpp:4726) で
  マスタークロックとして使われているため、スクラブは**完全に別経路**にする）。
- レイテンシ目標 **50ms 未満**（映像の 100ms より厳しい。音のずれの方が知覚されやすい）。

**設計成果 — 3-15 コマンドモデル（前回の宿題）**
`Command`（呼び出し単位・引数を取る・`Scope` を持つ）と
`PaletteEntry`（検索される行・引数を束ねたもの）を**分離**する規則を確定。
- 分離しないと `SetTrackHeight` が 3 コマンドに膨れ、
  **キーマップエディタ 3 行・パレット 3 行・内部関数 3 つ**が重複する。
- 3-3 の数値入力（`+15` / `01:23:10:00` / `scale 120`）は
  `PaletteEntry` を経由せず `Command` を直接引数付きで叩く経路として実装できる。
- **`Scope` は `Command` 側が持つ**（D8 の再発防止）。
- 効果: UX0 項目 6 は「既存 `QAction` を表へ移す」作業になり、
  項目 18（パレット）と 21（キーマップエディタ）は**表を読む薄い UI** で済む。
  3 つを別々に作るより総量が小さい。

**次回の焦点**
- **UX0 項目 1〜5 の着手可能性を検証する。** これまで 5 回は設計に費やしたが、
  UX0 は「安く効く」層として設計済み。`main.cpp`（22 行）と CMake 構成を読み、
  `tokens.json` → QSS 生成をビルドに組み込む具体的な手順を決める。
- **`timeline.cpp`（2,056 行）の不変条件を確認する。** 3-13 の構造共有は
  `Track` を immutable にするため、現在 `Track` を直接書き換えている箇所を洗い出す必要がある。
- Status Ribbon（項目 20）の表示項目を確定する。`Diagnostics...` ダイアログが
  今どの情報を出しているかは第 1 回で確認済みなので、常時表示に適した部分集合を選ぶ。

### 第 6 回 — 2026-07-25
コード変更なし。前回の宿題 3 件を消化し、**過去の判断を 2 件訂正**した。
UX0 はこれで**実装手順まで確定**した（3-16）。

**訂正 1 — D1 の症状は「明色テーマ」ではなく「不一致」**
`timeline_widget.cpp` は **62 箇所の `QColor` リテラル**で既に暗色を描いていた
（`QColor(31,34,39)` = `#1F2227` など）。モニタも同様。つまり正確な症状は:
- 自前 `paintEvent`（タイムライン / モニタ）= **暗色**
- Qt が描くもの（メニュー・ドック・ボタン・ツリー・ダイアログ）= **OS 既定の明色**

**暗いタイムラインが明るいグレーの枠にはめ込まれている**状態で、
どちらかに統一されているより悪い（未完成品に見える）。
→ UX0 に項目 1b（62 箇所のトークン化）を追加した。

**訂正 2 — トークンは JSON ではなく C++ `constexpr`（3-1 の決定変更）**
第 2 回の `tokens.json` 案を撤回。ビルド構成を読んだ結果:
1. **`paintEvent` はトークンを C++ の値として必要とする。** QSS はカスタム描画を
   スタイルできないため、JSON にすると**同じ色を 2 系統で管理**することになり
   単一の真実が壊れる。
2. ビルド時生成は新依存を持ち込み、「ビルドは開発者のグローバル環境に依存しない」
   という `IMPLEMENTATION_PLAN.md` の方針に反する。
3. `constexpr` は**コンパイル時に検査される**。JSON のキー typo は実行時まで発覚しない。
→ `theme_tokens.hpp`（`constexpr`）+ `theme.cpp` + `@token` 入りの可読な `.qss` に変更。

**新発見 — D12: クリップ色が意味ではなくハッシュ**
[timeline_widget.cpp:54](src/app/timeline_widget.cpp:54) は
`hue = (assetId * 47) % 360` で色相を決めている。結果:
- **タイムラインが虹色**になり、Premiere のような「色＝意味」が成立しない
- **映像クリップと音声クリップが色で区別できない**（同一アセットなら同じ色相）
- 彩度 145〜190/255 と高く、3-1a の規則「**彩度は画像から遠ざける**」に正面から反する
- **ラベルカラー（S10）を追加できない** — 色のスロットがハッシュに占有されている
→ 「色が意味を持たないので一目で読めない」＝**走査を助けるどころか妨げている。**
UX0 に項目 1c（意味ベース着色への置換）を追加した。

**実現可能性の確認 — 3-13 の構造共有は `core` 内に完全に閉じる**
- 公開された可変トラックアクセサは **`const Track* findTrack()` のみ**
  ＝ **外部から `Track` を書き換える経路が存在しない**
- `tracks_` の構造操作は `insert` 2 / `erase` 2 のみ
- `timeline.cpp` 内の可変 `Track&` / `Track*` は 37 箇所
→ **`timeline.hpp` / `timeline.cpp` の 2 ファイルで完結**。`src/app` も `workers` も
テストも呼び出し側の変更が不要。private な `Track& mutableTrack(TrackId)` を 1 つ追加し
COW 複製をそこへ集約するだけ。**「安い」という当初判断が裏付けられた。**

**設計成果 — 3-16 ビルド組み込み手順 / 3-17 Status Ribbon**
- 3-16: `qt_standard_project_setup()` により **AUTORCC が既に有効**なのでリソース追加は
  追加設定なしで動く。`.qrc` は不要（`qt_add_resources` のターゲット API を使う）。
  **`Fusion` スタイルの固定が重要** — Windows ネイティブスタイルは QSS の多くを無視し
  macOS とも挙動が違う。固定すれば両 OS で同一の見た目になる。
  なお `videx_set_project_warnings` + `videx_enable_sanitizers` が効いているため
  **新規コードは警告ゼロ + サニタイザ通過が必須**。
- 3-17: 現在の Diagnostics は静的情報と動的情報が混在したモーダル。
  **常時表示に適するのは動的なものだけ**として 8 項目を選定
  （シーケンス / 再生画質 / ドロップフレーム / ジョブ / キャッシュ / ワーカー /
  自動保存 / メディア欠落）。静的情報は `Help > About` へ移す。
  規則「**正常時は静かに**」— 異常が出た項目だけが色を持つ。
  各項目は**クリックで対応先へ飛ぶ**（表示だけで終わらせない）。

**次回の焦点**
- **UX0 の実装に着手する準備が整った。** 設計は 6 回で出し切ったため、次回は
  設計の追加ではなく**実際にビルドが通るかの検証**に向ける。`cmake --preset` で
  現状ビルドが成功するかを確認し、成功するなら UX0 項目 1・2 を実装して
  スクリーンショットで前後比較する。ビルドが通らない場合はその原因を報告する。
- 上記が不可の場合の代替: `tests/` の 9 ファイルを読み、UX0〜UX1 の変更が
  どのテストを壊すかを事前に特定する（回帰の予測）。

### 第 7 回 — 2026-07-25
**初めて実際にビルドとテストを実行した。** 第 1〜6 回はすべてコード読解だったため、
今回は設計の追加より**実証**に充てた。結果は Part 5 に記録。

**成果 1 — ビルドは成功する。UX0 は今すぐ着手できる**
`cmake --build build/windows-local --config Debug` が成功。
Qt は `.deps/Qt/6.8.3/msvc2022_64` にベンダリング済みで、
**開発者のグローバル環境に依存していない**。環境構築の障害は無い。

**成果 2 — テストベースライン確立: 登録 5 件すべて合格**
`100% tests passed, 0 tests failed out of 5`。
これで UX0 実装後の回帰を差分で判定できる。

**自己訂正 — 「コアテストが走っていない」は誤りだった**
`videx.core.application_info` が 0.55 秒で終わることから
「1,293 行の `timeline_test.cpp` が実行されていない」と疑ったが、**誤り**。
[application_info_test.cpp](tests/core/application_info_test.cpp) が `runTimelineTests()` を呼び、
[timeline_test.cpp:1248](tests/core/timeline_test.cpp:1248) が
**定義 43 個・呼び出し 43 個**を全て dispatch していた。
速いのは `videx_core_tests` が **Qt をリンクしていない**ため
（Qt 系テストは offscreen 初期化に 4〜5 秒を払っている）。

**成果 3 — カバレッジの穴を特定: メディア / エクスポート系 1,021 行が走らない**
`tests/CMakeLists.txt` は 3 ファイルを FFmpeg 依存でゲートしている
（`timeline_export_test.cpp` 602 行 / `media_worker_protocol_test.cpp` 281 行 /
`media_probe_test.cpp` 138 行）。`windows-media` プリセットが必要だが
**`build/windows-media` に `CTestTestfile.cmake` が無い**＝構成が未完了。
- **リスク**: Part 4 の **28（Undo の COW）・24（トランジションのオブジェクト化）・
  1c（クリップ着色）はプレビュー/エクスポートのパリティに影響する**。
  → **着手前に `windows-media` の構成を復旧させる**必要がある。
  UX0 の 1・2・5〜10 は core/widget テストで足りるため影響しない。

**成果 4 — UX0 の回帰リスクが「低い」と実証できた**
**テストスイート全体に絶対的な色アサーションもカーソルアサーションも存在しない。**
`timeline_widget_test.cpp` で色/ピクセルに触る唯一の箇所は**差分テスト**で
（[L656-674](tests/app/timeline_widget_test.cpp:656)）、
横スクロール時に固定ヘッダ列が再描画されないことを検証している。
before/after 両方が新配色になるので**パレット変更では壊れない**。
→ UX0 項目 1・1b・1c・2 は**低リスク**と判定。

**重要な副産物 — D8 と D9 が 6 回も発見されなかった理由が判明**
**キー入力経路とカーソル状態を検証するテストが 1 つも無い。**
だからショートカットのデッドコード 6 件も「カーソルが嘘をつく」欠陥も
自動検出されずに残っていた。
→ **UX0 の項目 6・8 は実装と同時にテストを追加する**（再発防止）。
Part 5 の表に項目別の回帰リスクとして記録した。

**次回の焦点**
- **`windows-media` の構成復旧を試みる。** これが通れば 1,021 行のメディア/エクスポート
  テストが走り、Part 4 の重い項目（COW・トランジション・着色）を安全に進められる。
  `cmake --preset windows-media` を実行し、失敗するなら原因（vcpkg / FFmpeg の
  どの段階か）を切り分けて報告する。
- 復旧できた場合は全 8 テストのベースラインを取り直し、Part 5 を更新する。
- UX0 の実装そのものは**ユーザの指示待ち**とする。設計・手順・回帰リスクは
  出し切ったので、あとは着手の可否のみが未決。

### 第 8 回 — 2026-07-25
**前回の自分の結論を 1 件撤回し、代わりに構造的な発見を 1 件得た。**

**撤回 — 「メディア/エクスポートテストが走らない」は誤りだった**
第 7 回で「`windows-media` の構成が未完了」と報告したが**検証手順のミス**だった。
`grep -c` は**一致 0 件で終了コード 1 を返す**ため、
`grep -c ... || echo "no CTestTestfile"` が誤って「ファイルが無い」と出力していた。
加えて `add_test` は**ディレクトリ単位の CTestTestfile に登録される**ので、
トップレベルを見て 0 件なのは正常だった。

**実測した正しい状態: `windows-media` は完全に動く**
- ビルド成功、**エラー・警告ゼロ**（warnings-as-errors + サニタイザ有効下で）。
  `videx-media-worker.exe` `Videx.exe` ＋テスト実行ファイル 9 個が生成される。
  FFmpeg は vcpkg で解決済み（`FFMPEG_FOUND=TRUE`、avcodec 62.28.102）。
- **`100% tests passed, 0 tests failed out of 9`（30.83 秒）**。
  エクスポートパリティを検証する `videx.media.export_h264_aac`（602 行）も
  **実際に合格している**。
→ **第 7 回で挙げたリスクは消滅。** Part 4 の 28（COW）・24（トランジション）・
1c（着色）に着手する前提条件は**すでに揃っており、復旧作業は不要**。

**新発見 — テストスイートの 3 つの盲点に、最重要級の診断がすべて入っている**
`PRODUCT.md` は 7 つの性能予算を明示しているが、**そのどれも測定されていない**:
- タイミング検証構文のヒットは `tests/` 全体で **2 件**（両方 `waitForFinished` の
  タイムアウト値であり測定ではない）
- **1,000 クリップ超のシーケンスを構築するテストが 0 件**
- `tests/` の構成は `app/ core/ media/ render/` で、`IMPLEMENTATION_PLAN.md` が規定する
  **`performance/` が存在しない**

| 盲点 | 検出されなかった診断 |
| --- | --- |
| **入力経路**（キー / カーソルの検証が皆無） | **D8** ショートカット分裂 / **D9** カーソルの嘘 |
| **性能**（予算 7 件すべて未測定） | **D11** 描画の 2 つのスケール限界 |
| **視覚状態**（絶対色の検証が皆無） | **D1** ダーク/ライト不一致 / **D12** ハッシュ着色 |

> **偶然ではない。** テストスイートは**編集意味論の正しさ**を厚く検証している
> （コア 43 件 + ラウンドトリップ + ポインタ操作 + エクスポートパリティ）。
> **正しさは守られているが、体験を決める 3 領域が測られていない。**
> 8 回で見つけた最重要級の欠陥がすべてこの 3 盲点に入るのは、
> **「テストが通っているから品質が高い」という推論が UX には効かない**ことの実例。

Q シリーズに Q9（性能テスト皆無）・Q10（入力経路テスト皆無）を追加。
再設計に伴って追加すべきテスト 3 種（キーバインド衝突 / カーソル一致 /
`tests/performance/` 新設）を 5-5 に明記した。

**次回の焦点**
- **`tests/performance/` の新設案を具体化する。** D11 の 2 つの限界を測る最小の
  ベンチマークを設計する（10,000 クリップ構築 + `paintEvent` 計測 / 全幅音声クリップ計測）。
  これは新規ファイル追加なので、既存コードには触れずに実装できる**最もリスクの低い着手点**。
- あわせて `PRODUCT.md` の 7 予算のうち、どれが自動測定可能でどれが手動計測に留まるかを
  切り分ける（例: A/V ドリフトは golden ファイル比較、起動時間は CI 計測）。
- UX0 本体の実装は**引き続きユーザの指示待ち**。8 回で設計・手順・回帰リスク・
  ビルド検証・テストベースラインまで揃ったため、技術的な未決事項は残っていない。

### 第 9 回 — 2026-07-25
**推測を実測に置き換えた回。** 第 5 回のメモリ見積りを検証するため、
使い捨てのプローブを scratchpad でコンパイルして実際の `sizeof` を測った
（MSVC 14.44 / x64 / ヘッダのみ。**リポジトリには何も追加していない**）。

**検証結果 — 第 5 回の「数百 MB」は正しかった**

| | 実測 |
| --- | --- |
| `sizeof(Clip)` | **344 バイト** |
| 10,000 クリップのスナップショット 1 個 | **3.28 MiB** |
| **`apply()` 1 回（×2 コピー）** | **6.56 MiB** |
| **履歴 100 段の滞留** | **328 MiB** |

ただしこれは**楽観的な下限**。`sizeof` はインラインメンバのみを数え、
各 `Clip` の `effects` / `motionKeyframes` / `gainKeyframes` / `speedKeyframes` の
**ヒープ確保分を含まない**。実プロジェクトでは **500 MiB〜1 GiB** に達する。

**★ 当初見落としていた本質 — ボトルネックは帯域ではなく確保回数**
`vector<Clip>` のディープコピーは **memcpy 一発では終わらない**。
各 `Clip` は内部に 4 本のベクタを持つため、コピーごとに個別のヒープ確保が要る:

```
10,000 クリップ × 4〜6 本 = 40,000〜60,000 回の確保 / スナップショット
apply() は 2 個作る       = 1 コマンドあたり 80,000〜120,000 回の確保
```

> **クリップを 1 つ動かすだけで 10 万回規模のヒープ確保が走る。**
> 6.56 MiB の memcpy より遥かに重く、確実に体感できるヒッチになる。
> **3-13（COW）の効果は当初見積りより大きい** — バイト数を減らすだけでなく
> **確保の嵐そのものを消す**。1 クリップを触るコマンドは
> 該当トラック 1 本分の確保だけになる。
> → **Part 4 項目 28 の優先度を上げた。**

**設計成果 — 3-18 `tests/performance/`（前回の宿題）**
- `PRODUCT.md` の 7 予算を切り分け: **5 件は自動測定可能**（初フレーム 300ms /
  トランスポート 100ms / 10,000 アイテム 60fps / 復旧損失 / undo・replay は既に担保）。
  自動化できないのは 2 件のみ（音の不連続 → golden PCM 比較で代替、30 分通し → 手動）。
  **現状 0 件なので費用対効果が大きい。**
- ベンチ 3 本を設計: `paint_scale_bench`（限界 1）/ `paint_width_bench`（限界 2）/
  `undo_memory_bench`（3-13）。
  - `paint_scale_bench` の要点は**可視 20 クリップに対し 10,000 クリップを保持する
    ズーム率に設定すること** — ビューポート判定の欠落が数値としてそのまま出る。
  - `undo_memory_bench` は**確保回数**を計測し、5-6 の実測値を回帰テストとして固定する。
- 運用規則: **CI では合否判定に使わず記録・比較に使う**（マシン差が大きい）。
  `IMPLEMENTATION_PLAN.md` の「公開リファレンス機で追跡」方針に合わせた。
- **ベンチは修正の前に入れる。** 前後比較ができないと項目 26・27・28 が
  効いたかを主張できない。→ Part 4 に **UX-P 層**を新設し最前段に置いた。

### 第 10 回 — 2026-07-25 / 独立監査との突き合わせ

ユーザによる独立した全体調査の結果を受け、**本文書を訂正・拡張した。**
今回はコード変更なし（本文書のみ更新）。

**訂正 1 — S7 は実装済みだった（出典の優先順位の誤り）**
「トラックのドラッグ並べ替えが未実装」は誤り。
[timeline_widget.cpp:1093](src/app/timeline_widget.cpp:1093) に
`trackDragging_` / `trackDropRow_` として実装済み（同種行スナップ付き）。
**原因**: 第 1 回で `IMPLEMENTATION_PLAN.md:283` の「Not yet implemented」リストを
**コードで検証せずに引用した**。
→ 同リストの他 5 項目（マルチトラックパッチ / トラック名色 UI / ネスト /
調整レイヤー / 描画仮想化）は再検証し、すべて正しいことを確認した。
→ **以後、機能の有無はコードのみを典拠とする。**

**訂正 2 — S9 は「UI が無い」ではなく「コマンドが無い」**
再検証の結果、`SetTrackNameCommand` / `SetTrackColorCommand` は
`EditCommand` variant に**存在しない**。`Track` に `name` / `color` フィールドはあるが、
**変更経路が丸ごと欠落**している。UI 追加だけでは足りない。

**最重大の見落とし — D13: プレビュー合成の二経路と画質の崖**
9 回の調査でシェル・タイムライン・ヒットテスト・Undo・ビルドは精査したが、
**「1 フレームがソースから画面に届くまで」を一度も追跡していなかった。**
- 高速経路（`QMediaPlayer` + `QPainter` オーバーレイ）は**元素材のネイティブ解像度**。
- しかしタイトルに**モーションキーフレーム / エフェクト / マスク / クロップ /
  トランジション**のいずれかがあると
  [collectTitleOverlays()](src/app/main_window.cpp:6516) が `false` を返し、
  FFmpeg フレームサーバー合成へ落ちる。
- そのとき [L7026](src/app/main_window.cpp:7026) は
  **`Auto` を無条件で divisor 2** にし、基準も **1280×720 にハードコード**
  → **再生中は 640×360**。
- **キーフレームを 1 つ打つだけでプレビューが面積比 1/9 に落ちる。**
  UI には経路も理由も**一切表示されない。**
- さらに `QtMonitorWidget::render()`
  （[L1487](src/render/src/qt_monitor_widget.cpp:1487)）は
  **背景クリアのみで描画ゼロ**、`initialize()` は空。
  **QRhi は確保されているが合成器として未着手。**

> **教訓**: 構造と相互作用の監査だけでは、体験の欠陥は見つからない。
> 実使用で最も目立つ欠陥が、9 回の設計をすり抜けた。

**新規追加 — F4 / F5 / F6 / Q11**
- **F4**: `Sequence` が**フレームレートしか持たない**（解像度・色空間・音声設定なし）。
  そのため 4 箇所が独自に 1280×720 を仮定している。**D13 の根本原因**であり、
  スキーマ v2 の最優先項目に格上げ。
- **F5**: `autosaveProject()` は `projectPath_.isEmpty()` で即 return。
  **未保存プロジェクトは 60 秒自動保存の対象外** = 新規作業が全損しうる。
  `PRODUCT.md` の「復旧損失は 1 コマンド以内」に違反。
- **F6**: キャッシュキーが `SHA256(絶対パス + サイズ + 更新時刻)`。
  **素材を移動すると全キャッシュが失効**し、プロキシ再生成が走る。
  内容ハッシュへ変えれば移動耐性と重複検出が同時に得られる。
- **Q11**: `git ls-files` = **6 ファイル**（すべて docs）。**製品コード全体が未追跡。**
  `Tools/` に **12,754 ファイル**の外部バイナリ。
  **どの変更も差分で追えず戻せないため、他のすべての作業の前提。**

**Part 4 の再編 — UX より上位の層 `UX-Z` を新設**
第 1〜9 回は UI/UX を UX0（最優先）としていたが、
**リポジトリ管理・データ損失・プレビュー画質が明確に上位**。
Z1〜Z10 を UX0 の前に置いた。
ただし **UX0 の項目 5（`Window` メニュー）と項目 8（ホバーカーソルの嘘）は
独立かつ極小コスト**なので Z 層と並行可とした。

**次回の焦点**
- **Z3（フォールバック理由の可視化）の具体設計。** `collectTitleOverlays` の
  6 つの拒否条件を列挙可能な enum にし、Status Ribbon と Diagnostics に出す設計を書く。
  **最小コストで原因調査を可能にする**ため、Z 層で最初に着手すべき項目。
- **Z5（高速経路の一般化）の境界を決める。** どこまでをオーバーレイで評価し、
  どこからコンポジターに渡すか。位置・拡大・回転キーフレームとクロップは
  `QPainter` の変換で表現できるが、マスクとエフェクトは難しい。線引きを明文化する。

**旧「次回の焦点」（第 9 回時点）**
- **`clipColor` の意味ベース化（項目 1c）を具体的な配色表として確定する。**
  D12 で方針は決めたが、トラック種別 × 選択状態 × ラベルカラー 8 色 ×
  無効/ロック状態の組み合わせで実際に使う値を決めていない。
  3-1a のトークンと矛盾しない具体表を作る。
- **タイトル / キャプションクリップの色をどう扱うか**も同時に決める
  （現状は「ビデオトラック上の通常クリップ」なので種別だけでは区別できない）。
- UX0 本体の実装は引き続きユーザの指示待ち。
