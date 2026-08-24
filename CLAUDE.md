# audio_capture_relay_tui プロジェクト規約

PulseAudio / PipeWire-Pulse の capture source を読み取り、自分自身の再生ストリームとして
出し直す中継ツール + ncurses の 1 画面 TUI。C++20 / CMake / 単一バイナリ。

**主な用途は Discord などの画面共有に音を乗せること。** 画面共有は「アプリの再生音」を
拾うので、`.monitor` source を中継して自分の再生ストリームとして出し直す、という形を取る。
コンソールで波形を眺めたい、という動機もある。

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
    output_mix.{h,cpp}      1 チャンク分の出力の組み立て(枯れたときの埋め方・音量)
    splice.{h,cpp}          つなぎ替え箇所のクロスフェード
    waveform.{h,cpp}        点字波形の生成、メーターバーの文字列化
    device_info.h           source / sink 1 件のデータ。PulseAudio 型を漏らさない
    device_match.{h,cpp}    --source / --sink の引数から選ぶ判定(出力はしない)
    error_log.{h,cpp}       直近のエラーと件数、失敗が続いた時間の判定
    text_util.{h,cpp}       UTF-8 を壊さない切り詰め、小文字化、数値の解釈
  adapters/                 副作用。外部ライブラリはここでだけ触る
    pulse_device_lister.*   source / sink 一覧の取得(libpulse の mainloop / context)
    pulse_capture.*         capture スレッドの本体(pa_simple_read)
    pulse_playback.*        playback スレッドの本体(pa_simple_write)
    tui_ncurses.*           全画面 TUI とキー入力(ncurses)
    plain_status.*          --no-tui の 1 行ステータス(標準出力)
  app/                      CLI と組み立ての補助
    options.{h,cpp}         引数解析と usage
    device_cli.{h,cpp}      source / sink 一覧の表示・対話選択・エラーメッセージ
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
./build/audio_capture_relay --no-relay      # 中継せず表示だけ(再生ストリームを作らない)
./build/audio_capture_relay --no-tui        # TUI なし(1 行ステータス)
./build/audio_capture_relay --version
```

`--help` / `--version` の終了コードは 0、引数エラーは 2。`parse_args` は
`ParsedArgs`(`options` + `exit_code`)を返す。ここを `optional` に戻さないこと
(戻すと `--help` がエラー扱いになる)。

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

**`--no-relay` では playback スレッドを立てない。** 再生ストリームを作らないので、
`pavucontrol` にも出ないしドリフト補正も動かない。capture もリングに積まない。
表示層はこれを `SharedState::relay_enabled`(**データ**。起動時に一度だけ書く)で知る。
中継しているかどうかで意味を失う表示(音量 / `Latency:` 行 / underruns 等)は出さないこと。

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
- 排出(多めに消費)できるのは**リングからだけ**。サーバ側が目標より深い環境
  (例: `--latency-ms 20` でサーバが 70ms 抱えている)では、いくら引いても届かない。
  そこで実際に狙う水位を **`max(要求値, サーバ側の滞留 + リングの予備)`** とする。
  ここを「届かないから何もしない」にすると**開ループ**になり、リングの水位が
  成り行き任せになっていずれ枯れる。押し上げたときは TUI に `floor` と出る。
- 波形をつなぎ替える所(リングバッファのトリム、余分に消費した分の捨て際)は必ず
  `crossfade_tail` を通す。ハードスプライスはそのままクリック音になる。

### 書き込みのペーシング(触ると再発する)

サーバ側は**走り出しのしばらく書き込みを一切ブロックしない**。実測(PipeWire-Pulse)で
700ms 以上、`pa_buffer_attr` の `maxlength` とは無関係に飲み込み、その間 `get_latency()` は
0 を返す。ここへ実音声を流すと、貯めたリングが一瞬で空になる
(実測: 目標 1000ms でリングの 980ms が 250ms 以内に 111ms まで持って行かれた)。

対策は **`pa_simple_write` のブロックに頼らず、自分で実時間ペースを守ること**。
書き込みが実時間より `PACE_SLACK_CHUNKS`(4 チャンク)以上先行しないよう待つ。
これはそのまま「サーバ側に持たせる量の上限」になる。

- **リングが貯まるのを待つ間も、無音を流し続けること。** 待っている間に書き込みを
  止めるとサーバ側が枯れる(目標が大きいほど待ちが長い。実測で `out` が 0 に
  張り付いた = サーバ側が空 = 実際の音は途切れている)。
- **実測でサーバ側が浅いときは、ペースの基準をそこで取り直すこと。** 壁時計と
  オーディオクロックは少しずつずれるので、絶対スケジュールを持ち続けると
  何十分か後に「先行していないのに待つ」ようになり、やはりサーバ側を枯らす。
  走り出しは `get_latency()` が 0 を返すので、そのときだけ壁時計で抑える。

プライミングは「リングが `目標 - pace_slack` に達するまで無音を流す → 多ければ
クロスフェード付きで削る」。捨てる音は中継開始前の分なので問題ない。

**この辺りを変えたら、`--no-tui` で数分回して `drift` / `underruns` / `overflow_trims` を見る。**
`drift` が 0 付近に収束し、`underruns` が起動直後以外で増えなければ OK。
起動を数回繰り返して underruns が 0〜2 に収まることも見ること(実測でばらつく)。

**`--latency-ms` を振ることも忘れないこと。** 120ms だけ見ていると気付けない壊れ方をする
(大きい目標では起動時の持って行かれ方が効き、小さい目標では下限に当たる)。
最低でも 20 / 120 / 1000ms の 3 点。`lat=` が目標(押し上げ後は `floor=`)付近に
収束すれば OK。

### 枯れたときの埋め方

リングが枯れたら最後のサンプルで埋めるが、**保持しっぱなしにしない**。
DC が乗ったままだと復帰の瞬間に「ブツッ」と鳴るので、5ms(`PAD_FADE_FRAMES`)で
0 へ落とし、実音声が戻ったら立ち上げる。
組み立ては `domain/output_mix.h` の `assemble_output`(純粋、テスト済み)。

**立ち上げランプの長さは「実際に減衰した分」に比例させること。固定長にしない。**
埋め込みが走るのは枯れたときだけではない。**ドリフト補正が 1 チャンク未満しか
消費しないと、足りない分はここで埋まる**(リングは枯れていない)。そこへ固定長
1ms のランプを掛けると、何ともない実音声に穴が空く。実測でこれが
`--low-latency` で毎秒 2 回、既定でも毎秒 0.2〜1.3 回起きていて、**プチノイズの
正体だった**(2026-08-24 に修正)。

この経路は `underruns` には出ない(pop 自体は成功しているため)。見えるように
`pads` カウンタを出しているので、**「underruns 0 だからツール側は健全」と
読まないこと**。`assemble_output` は `FillResult`(埋めたフレーム数 /
ランプに使ったフレーム数)を返すので、判断はそれを見る。

### ホットパスの確保

1 チャンク(既定 20ms)ごとに回る経路では、毎回ヒープを取らない。
`PcmRing::pop` と `analyze_chunk` は呼び出し側のバッファへ書く形にしてある。
戻り値で `std::vector` を返す形に戻さないこと。

### 音声フォーマット

S16LE / 48000Hz / 2ch 固定(`domain/audio_format.h`)。可変にする予定は今のところ無い。
サンプル値は `int16_t` のインターリーブ。フレーム数とサンプル数(= フレーム数 × 2)を
取り違えやすいので、変数名に `_frames` / `_samples` を付けて区別すること。

### PulseAudio / ncurses の扱い

- **再生は `pa_simple` のまま。`pa_stream`(非同期 API)へ移さないこと。**
  以前 `pa_stream` にしたとき、Discord の画面共有に音が乗らなくなったことがある
  (2026-08-24 に利用者から共有された経験。原因は未特定)。低レイテンシ化のために
  API を変えたくなる場面があるが、**実際の画面共有で拾われるか確認するまでやらない**。
  レイテンシは `--chunk-ms` を詰めればほぼ足りる(README 参照)。
- `pa_*` / ncurses の呼び出しは `adapters/` の中だけ。`domain/` の関数シグネチャに
  ライブラリの型を出さない。
- **エラーを `std::cerr` に直接書かないこと。** TUI 表示中は画面が壊れる。
  `st.errors.report(...)`(`ErrorLog`)に積み、出すのは表示層の仕事。
  続行不能なときは `st.abort(...)` を使う(終了コードが 1 になる)。
- capture / playback は 3 秒失敗し続けたら諦めて終了する(`FailureWindow`)。
  source が消えたまま永久にリトライしない。
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

## この先の方向(2026-08-24 に決めたこと)

**「PulseAudio の中継ツール」から「TUI のオーディオ系ツール」へ広げる。** 本人の意向。
順序は下記。**上から順にやること**(理由も一緒に書いてあるので、飛ばす前に読む)。

1. **済: OSS 公開の下ごしらえ** … MIT / `--version` / `cmake --install` /
   英語 README(`README.ja.md` が日本語) / GitHub Actions / `packaging/PKGBUILD`。
   公開先は `github.com/c0-nu/audio_capture_relay_tui`(予定)。
   リポジトリ名は**当面このまま**。方向が固まってから rename する(GitHub が
   リダイレクトを張るので急がない)。
2. **済: `--no-relay`** … 中継せず取り込んで表示するだけ。
3. **ビジュアライザを内蔵で 2〜3 個増やす** … spectrum(FFT)/ オシロ / Lissajous など。
   **プラグイン API を先に切らないこと。** 今の `WaveHistory` は min/max/last の
   エンベロープしか持っておらず、スペクトラム系は作れない。何を渡すべきかは
   実際に 2〜3 個作るまで分からない。作ってから固める。
4. **Visualizer プラグイン API** を切る。
5. **オーディオファイル再生** … `AudioSource` の抽象が要る。デコーダは
   **`dr_libs`(dr_wav / dr_mp3 / dr_flac)+ `stb_vorbis`** を想定
   (ヘッダオンリー・public domain / MIT 系なので MIT のままでいられる。
   `libsndfile` は LGPL、`ffmpeg` は LGPL/GPL なのでライセンスが動く)。
   **一番の作業はリサンプラ**(ファイルは 44.1kHz が多いが、内部は 48kHz 固定)。
6. **オーディオフィルタ**(やらない可能性もある)… **既定は無効**。遅延が乗るため。

### プラグインは 2 系統に分けること(Visualizer と Filter を同じ API に載せない)

| | Visualizer | Filter |
| --- | --- | --- |
| 走る場所 | 表示スレッド(30fps 程度) | playback のホットパス(5〜20ms ごと) |
| データ | 履歴を**読むだけ** | 1 チャンクを**書き換える** |
| 落ちたら | 表示が壊れるだけ。音は続く | 音が止まる |
| 制約 | ゆるい | ヒープ禁止。遅延・CPU に直結 |

1 本の API にまとめると、ビジュアライザ側の都合(履歴が欲しい / 重い FFT を回したい)が
ホットパスに漏れる。**別インターフェースで切る。**

フィルタを後回しにするとしても、`assemble_output` の後・`apply_volume` の前に
**フックを刺せる隙間**だけは空けておくこと(実装は空でよい)。

### Bluetooth 取り込みは実装不要

BT オーディオ機器を繋ぐと、サウンドサーバ側が普通の source として見せる
(`bluez_input.XX_XX_XX_XX_XX_XX.a2dp-source` / `bluez_output.XX_….monitor`)。
`--source` でそのまま選べる。**「BT 対応」として何か実装しようとしないこと。**

## 次の一手(2026-08-24 時点)

### 済: quantum を下げても縮まない(実測で決着)

`clock.force-quantum 256` を強制した前後で、`--low-latency` の合計は 60ms、`out` は
32〜35ms のまま変わらなかった。理由は **PipeWire が要求レイテンシに応じて quantum を
自動で下げており、強制する前からすでに 256 だったから**(`pw-top -b -n 3` の QUANT 列で
`AudioCaptureRelay` と sink がどちらも 256、`clock.force-quantum` は 0)。

残る `out` の 30ms 前後は **sink の ALSA 側**(実測環境の USB CODEC は
`period-size 512` + `headroom 512` = 1024 フレーム ≒ 21.3ms、`pw-dump` で確認できる)。
graph quantum とは別枠なので force-quantum では動かない。**「quantum を下げれば縮む」と
再提案しないこと。**

### 残っている下げ代

1. **済: WirePlumber で `api.alsa.headroom` を下げた。** USB CODEC が
   period 512 + headroom 512(21.3ms)→ period 128 + headroom 256(8ms)になり、
   `out` が 32〜35ms → 25〜28ms、`--latency-ms 20` の `floor` が 42ms → 37ms へ。
   90 秒で underruns 0。設定は `~/.config/wireplumber/wireplumber.conf.d/` 
   (**環境側の永続設定**。ツールのリポジトリには入っていない)。
   `headroom = 128` と書いても実際は 256 に落ち着くので、**ここが底**。
2. `PACE_SLACK_CHUNKS` / `min_ring_frames` を CLI から触れるようにするか(下記の未決事項)。
   quantum で話が変わる可能性は消えたので、**判断材料はもう揃っている**。
   chunk 5ms では slack を 4 -> 2 にしても実測 50ms のまま変わらなかった = ALSA 側が
   支配的、というのが結論。触れるようにしても実利は薄い。
3. 済: `feat/low-latency` は main へマージ済み。作業ブランチはすべて削除済み。

---

## 未決事項(勝手に決めない)

- `PACE_SLACK_CHUNKS`(4)と `min_ring_frames`(2 チャンク)を CLI から触れるようにするか。
  いまは定数。chunk 5ms では実測でサーバ側の下限(約 30ms = sink の ALSA バッファ)が
  支配的なので、緩めても縮まなかった(2 に下げて実測 50ms、4 のままと同じ)。

- 出力先 sink は起動時にしか選べない。実行中に切り替えられるようにするか。
- 打ち切り(3 秒)のあと、再接続を試みるようにするか。いまは終了するだけ。
- `--source` / `--sink` は起動時の一覧に対する番号なので、デバイスの抜き差しで
  番号が変わる。名前指定を推奨する旨をどこまで README に書くか。

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
