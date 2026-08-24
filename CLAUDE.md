# audio_capture_relay_tui プロジェクト規約

PulseAudio / PipeWire-Pulse の capture source を読み取り、自分自身の再生ストリームとして
出し直す中継ツール + ncurses の 1 画面 TUI。C++20 / CMake / 単一バイナリ。

## 必読(常設コンテキスト)

実装前に必ず読み、以降すべての作業でこれに従うこと。

このリポジトリは小さいので、規約の本文はこのファイルに直接置きます。
元になっているのは手元の別プロジェクトで使っている共通のコーディング規約で、
迷ったとき・ここに書いていない論点はそちらを正本として参照してください。

### 絶対に守る 5 項目

1. **曖昧なまま進めない。** 判断が要る場面では、選択肢とトレードオフを示して質問する。
   「たぶんこうだろう」で進めない。実装途中で判断が必要になったら、その時点で止めて訊く。
2. **純粋な計算と副作用を分ける。** 副作用は入口と出口だけ。中間は純粋。
   → このリポジトリでは `src/domain/`(純粋)と `src/adapters/`(副作用)の分割がそれ。
3. **「動き」はデータの流れとして書く。** 手続きの羅列にしない。
   中間段は `void` を返さず、値(`ChunkAnalysis`, `PopResult`, `Decision`)を返す。
4. **モジュールは処理ではなくデータに依存する。** データを得るために他モジュールの処理を呼ばない。
   例: TUI は `SharedState` という**データ**に依存する。capture / playback の**処理**は知らない。
5. **1 ファイル 1 責務。依存は一方向。** 組み立ては `src/main.cpp`(Composition Root)だけ。

補足:

- 状態は不変が基本。可変にするなら「可変であること」が型と名前から分かるようにする
  (`SharedState`, `std::atomic`, 内部にロックを持つ `PcmRing` / `WaveHistory`)。
- ホットパス(1 チャンク = 既定 20ms ごとに回る経路)は例外的に in-place 書き換えを許す。
  その場合は**理由をコメントに書く**こと。
- 説明・メモ・変更理由は日本語。オーディオ処理の理屈を説明する既存の英語コメントは、
  そのまま残す(内容が正で、書き換える必然性が無いため)。

---

## プロジェクト構成

```
src/
  main.cpp                  Composition Root。配線だけ。処理は書かない
  domain/                   純粋。標準ライブラリ以外に依存しない(PulseAudio / ncurses を include しない)
    audio_format.h          S16LE / 48kHz / stereo の定数、フレーム <-> ms 変換
    relay_config.h          動作パラメータ(latency_ms / chunk_ms)と、そこから決まる値
    shared_state.h          スレッド間で共有する可変状態。所有するのは main
    pcm_ring.{h,cpp}        capture -> playback のリングバッファ(ロックは内部)
    wave_history.{h,cpp}    波形表示用の履歴。min/max のエンベロープで持つ(ロックは内部)
    drift_control.{h,cpp}   合計レイテンシから消費フレーム数を決める。ドリフト補正の中心
    level_meter.{h,cpp}     1 チャンクの RMS / ピーク / クリップ率 / モノラルミックス
    splice.{h,cpp}          つなぎ替え箇所のクロスフェード
    waveform.{h,cpp}        点字波形の生成、メーターバーの文字列化
    source_info.h           capture source 1 件のデータ。PulseAudio 型を漏らさない
    source_match.{h,cpp}    --source の引数から source を選ぶ判定(出力はしない)
    text_util.{h,cpp}       UTF-8 を壊さない切り詰め、小文字化、数値判定
  adapters/                 副作用。外部ライブラリはここでだけ触る
    pulse_source_lister.*   source 一覧の取得(libpulse の mainloop / context)
    pulse_capture.*         capture スレッドの本体(pa_simple_read)
    pulse_playback.*        playback スレッドの本体(pa_simple_write)
    tui_ncurses.*           全画面 TUI とキー入力(ncurses)
    plain_status.*          --no-tui の 1 行ステータス(標準出力)
  app/                      CLI と組み立ての補助
    options.{h,cpp}         引数解析と usage
    source_cli.{h,cpp}      source 一覧の表示・対話選択・エラーメッセージ
    signal_handling.{h,cpp} SIGINT / SIGTERM -> SharedState::request_stop
tests/                      domain/ のテスト(Catch2 v3)。本体とは独立にビルドする
```

依存の向きは **`main` → `app` / `adapters` → `domain`** の一方向。逆向きの include を作らない。
`domain/` に `<pulse/*>` や `<ncurses.h>`、`<iostream>` を持ち込まないこと
(この性質が壊れていなければ、`domain/` だけを切り離してテストに載せられる)。

### 触らないディレクトリ

- `build/` … 生成物

---

## ビルド・実行

```bash
cmake -S . -B build -G Ninja
cmake --build build
```

テスト(`domain/` だけ。PulseAudio も ncurses も要らない):

```bash
cmake -S . -B build-tests -G Ninja -DACR_BUILD_APP=OFF -DACR_BUILD_TESTS=ON
cmake --build build-tests
ctest --test-dir build-tests --output-on-failure
```

Catch2 v3 はシステムにあればそれを使い、無ければ FetchContent で取ってくる
(初回の configure だけネットワークが要る)。`domain/` が標準ライブラリしか使わないから
この分離が成立している。**テストに載せられるのは `domain/` の分だけ。**
本体側を試したくなったら、まず純粋な部分を `domain/` へ出すこと。

```bash
./build/audio_capture_relay --list          # source 一覧
./build/audio_capture_relay --select        # 対話選択
./build/audio_capture_relay --source 0      # 番号 / 完全一致 / 部分一致
./build/audio_capture_relay --no-tui        # TUI なし(1 行ステータス)
```

- 警告は `-Wall -Wextra -Wpedantic`。**警告ゼロを維持する。**
- ソースを増やしたら `CMakeLists.txt` の `DOMAIN_SOURCES` / `ADAPTER_SOURCES` / `APP_SOURCES`
  のどれかに追加する(glob は使わない)。テストを増やしたら `acr_tests` にも足す。
- TUI の動作確認は端末が要る。CI 的な環境では
  `TERM=xterm-256color script -qc "./build/audio_capture_relay --volume 0 --source 0" /dev/null`
  のように pty 越しに起動する。`--volume 0` にすれば音は出ない。

---

## このプロジェクト固有の注意

### スレッド構成

3 本。所有と停止は `main` が持つ。

| スレッド | 実体 | 役割 |
| --- | --- | --- |
| capture | `run_capture` | source から読む → 解析 → リングバッファへ積む |
| playback | `run_playback` | リングバッファから取る → 音量適用 → 再生ストリームへ書く |
| 表示(メイン) | `run_tui` / `run_plain_status` | 表示とキー入力 |

停止は `SharedState::running`(atomic)だけで伝える。各スレッドは自分でループを抜ける。
シグナルハンドラから触ってよいのはこの atomic だけ。

### ドリフト補正(壊しやすい所)

capture と playback は**別々のクロックを持つ独立したストリーム**なので、放っておくと
バッファが枯れる(プチプチ / 無音)か溜まり続ける(レイテンシ増大)。
そのため 1 チャンクごとに消費フレーム数を微調整している。理屈は
`src/domain/drift_control.h` の冒頭コメントに全部書いてある。**触る前に必ず読むこと。**

要点だけ:

- **制御対象は「リング + サーバ側キュー」の合計**(`--latency-ms` はこの合計に対する目標)。
  サーバ側の滞留は `pa_simple_get_latency()` で毎チャンク取る。リングだけを見ると、
  起動直後にサーバへ移った分を「減った」と誤認して十数秒かけて追いかけることになる。
- 瞬間のバッファ水位はノコギリ波状に振れる。**平滑値**で判断すること。
  生の水位に毎チャンク反応させると、周期的な「チッ」というノイズになる。
- 補正は端数を「未払い」として溜め、**1 フレーム単位でだけ**払い出す。
- 補正量に固定の上限(毎チャンク 1 フレーム等)を付けないこと。実ドリフトがそれを超えると
  永久に追い付けなくなる。
- 排出(多めに消費)できるのは**リングからだけ**。サーバ側が目標より深い環境で
  引き続けるとその場で枯れるので、リングの予備(`min_ring_frames`)を割る排出はしない
  (TUI に `[hold]` と出る)。深いレイテンシは受け入れる。
- 波形をつなぎ替える所(リングバッファのトリム、余分に消費した分の捨て際)は必ず
  `crossfade_tail` を通す。ハードスプライスはそのままクリック音になる。

### 起動時のプライミング(触ると再発する)

サーバ側は**走り出しのしばらく書き込みを一切ブロックしない**。実測(PipeWire-Pulse)で
700ms 以上、`pa_buffer_attr` の `maxlength` とは無関係に飲み込み、その間 `get_latency()` は
0 を返す。ここへ実音声を流すと、貯めたリングが 1ms 未満で空になり、起動直後に
underrun が並ぶ(`--volume 0` でも観測できる)。

そのため `run_playback` は **無音を書いて詰まるまで待ってから**、リングを目標水位に
合わせて中継を始める。この順番を崩さないこと。捨てる音は中継開始前の分なので問題ない。

**この辺りを変えたら、`--no-tui` で数分回して `drift` / `underruns` / `overflow_trims` を見る。**
`drift` が 0 付近に収束し、`underruns` が起動直後以外で増えなければ OK。
起動を数回繰り返して underruns が 0〜2 に収まることも見ること(実測でばらつく)。

### 音声フォーマット

S16LE / 48000Hz / 2ch 固定(`domain/audio_format.h`)。可変にする予定は今のところ無い。
サンプル値は `int16_t` のインターリーブ。フレーム数とサンプル数(= フレーム数 × 2)を
取り違えやすいので、変数名に `_frames` / `_samples` を付けて区別すること。

### PulseAudio / ncurses の扱い

- `pa_*` / ncurses の呼び出しは `adapters/` の中だけ。`domain/` の関数シグネチャに
  ライブラリの型を出さない。
- capture / playback のエラーは `std::cerr` に出しているが、TUI 表示中は画面が乱れる。
  直すならエラーを `SharedState` に積んで TUI 側で描く形にする(未着手)。
- `.monitor` source を同じ出力デバイスへ流し直すとループする。README の pavucontrol の
  注意書きを参照。

### 表示

- 端末幅での切り詰めは必ず `shorten()`(UTF-8 の途中で切らない)。`substr` を直接使わない。
- 波形は Unicode 点字(U+2800〜)。1 セル = 横 2 × 縦 4 ドット。
- 波形の履歴は生サンプルではなく **min/max のエンベロープ**(`WaveBucket`)で持つ。
  生で持つと表示のたびに 4 秒分(768KB)を複製することになり、しかも列ごとに 1 点だけ
  拾う描き方はピークを取りこぼす。列に複数バケットが対応するときは min/max を取ること。
- 描き方は 2 つ(`WaveformStyle`)。`envelope` は列の min〜max を塗り、`line` は
  `WaveBucket::last`(その区間の最後の生サンプル)を拾って隣の列と繋ぐ。
  実行中に `s` で切り替わるので、**どちらのスタイルでも成立するデータの持ち方**を保つこと
  (片方のためだけに履歴の形を変えない)。

---

## 未決事項(勝手に決めない)

- `--volume` / `--latency-ms` / `--chunk-ms` の不正な値(`--volume abc` など)で
  `std::stof` / `std::stoi` が例外を投げて異常終了する。現状の挙動をそのまま維持している。
  エラーメッセージを出して終了する形に変えるかは未決。
- エラー表示を TUI に統合するか(現状 `std::cerr` に出すので画面が乱れる)。
- underrun 時のパディングが最後のサンプルを保持し続ける(DC が乗る)。
  数 ms でゼロへフェードする形に変えるか。
- 出力先 sink を選ぶ `--sink` を足すか(いまは `pavucontrol` 頼み)。
- capture / playback のエラーが恒久化しても無限リトライする。打ち切るか再接続するか。

---

## 作業の進め方(設計規約ではありません)

### compact のタイミングは Claude 側から提案する

`/compact` を実行できるのは人間だけなので**決めるのはそちら**ですが、**言い出すのは Claude の役目**。
訊かれるまで黙っていないこと。提案するのは次がそろったとき:

- 文脈が減ってきた、または長い作業が一区切りした
- 作業ツリーが綺麗(`git status --short` が空)
- タスクが 1 つ区切れた直後で、途中の状態を抱えていない

**提案する前に、文脈にしか無いもの(決めたこと・分かったこと・次の一手)をファイルへ落とすこと。**
分割の途中やビルドが通らない状態では提案しない。先に区切りを作ってから言うこと。
