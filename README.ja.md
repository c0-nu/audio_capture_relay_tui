# AudioCaptureRelay TUI

> ⚠️ **このコードは AI(Anthropic の Claude)が書いています。** 書き起こしだけでなく、
> 設計判断もです。実機で音は確認していますが、人間による 1 行ずつのレビューは
> していません。→ [このコードは AI が書いています](#このコードは-ai-が書いています)

[English README](README.md)

任意の PulseAudio / PipeWire-Pulse capture source を読み取り、このアプリ自身の再生ストリームとして出力する最小限の中継ツールです。

この版では `top` / `htop` のような1画面TUIを追加し、Unicode点字文字で波形を表示します。

用途の一例: Discord などの画面共有は「アプリの再生音」を拾うので、`.monitor` source を
このツールで中継して**自分の再生ストリームとして出し直す**と、共有相手に音を届けられます。

## 依存関係

Arch / CachyOS:

```bash
sudo pacman -S --needed base-devel cmake ninja pkgconf libpulse ncurses pavucontrol
```

Ubuntu / Debian:

```bash
sudo apt install build-essential cmake ninja-build pkg-config libpulse-dev libncursesw5-dev
```

Fedora:

```bash
sudo dnf install gcc-c++ cmake ninja-build pkgconf-pkg-config pulseaudio-libs-devel ncurses-devel
```

## ビルド

```bash
cmake -S . -B build -G Ninja
cmake --build build
```

インストール(任意):

```bash
sudo cmake --install build
```

Arch 系はパッケージとして入れられます:

```bash
cd packaging && makepkg -si
```

## 使い方

source一覧:

```bash
./build/audio_capture_relay --list
```

デフォルトsourceを使う:

```bash
./build/audio_capture_relay
```

番号で選択:

```bash
./build/audio_capture_relay --source 0
```

部分一致で選択:

```bash
./build/audio_capture_relay --source monitor
```

対話選択:

```bash
./build/audio_capture_relay --select
```

出力先sinkを選ぶ(省略時は既定のsink):

```bash
./build/audio_capture_relay --sink 1
./build/audio_capture_relay --sink hdmi
```

`--list` はcapture sourceと出力sinkの両方を表示します。

### Bluetooth の音を取り込む

特別な設定は要りません。Bluetooth オーディオ機器を繋ぐと、サウンドサーバ側が
普通の source として見せてくれます(スマホなどから**受け取る**音は
`bluez_input.XX_XX_XX_XX_XX_XX.a2dp-source`、ヘッドセットへ**送っている**音は
`bluez_output.XX_….monitor`)。`--list` に出てくるので、そのまま `--source` で選べます。

中継せず、ビジュアライザとしてだけ使う:

```bash
./build/audio_capture_relay --no-relay
```

source を読んで表示はしますが、**再生ストリームを一切作りません**。`pavucontrol` にも
出ませんし、画面共有にも乗りません。ドリフト補正も動きません。レベルと波形だけ
眺めたいときはこちら。

`--volume 0` とは別物です。あちらは再生ストリームを開いたままレイテンシ制御も
回り続けます。`--no-relay` では音量 / mute / pause と `Latency:` 行がまとめて
消えます(どれも意味を持たないため)。

TUIなし:

```bash
./build/audio_capture_relay --no-tui
```

波形非表示で開始:

```bash
./build/audio_capture_relay --no-waveform
```

バージョン表示:

```bash
./build/audio_capture_relay --version
```

波形の描き方を選ぶ:

```bash
./build/audio_capture_relay --waveform-style line
```

| Style | 見た目 | 特徴 |
|---|---|---|
| `envelope`(既定) | 各列の最小〜最大を塗る | 幅が狭くてもピークを取りこぼさない |
| `line` | 各列から1点拾って繋ぐ | オシロスコープ風の細い線 |

実行中に `s` でいつでも切り替えられます。

## 実行中キー

| Key | Action |
|---|---|
| `q` | 終了 |
| `+` / `-` | ソフトウェア音量変更 |
| `m` | mute / unmute |
| `w` | 点字波形表示のon/off |
| `s` | 波形の描き方切り替え (envelope / line) |
| `p` or Space | pause / resume |

## レイテンシが気になる人へ

`--latency-ms`(既定 120)は **capture から実際に音が出るまでの合計**に対する目標です。
AudioCaptureRelay 自身のバッファと、PulseAudio / PipeWire 側のキューの両方を見て、
再生の消費量を少しずつ調整して合わせます。TUI の `Latency:` 行に内訳
(`ring` = 自前のバッファ、`out` = サーバ側)が出ます。

### まず試すもの

```bash
./build/audio_capture_relay --low-latency
```

`--chunk-ms 5 --latency-ms 60` と同じです(明示した `--chunk-ms` / `--latency-ms` が優先)。

### 効くのは `--latency-ms` より `--chunk-ms`

チャンク長を詰めると、こちらが持つ予備もサーバ側に持たせる分も**両方**縮みます。
実測(PipeWire-Pulse、いずれも 14〜90 秒運転。graph quantum は要求に応じて
自動で 256 まで下がっている状態):

| 設定 | 実測レイテンシ | 内訳 ring / out |
|---|---|---|
| 既定(`--chunk-ms 20 --latency-ms 120`) | 121ms | 50 / 58 |
| `--chunk-ms 10 --latency-ms 50` | 71ms | 20 / 41 |
| `--low-latency`(chunk 5 / target 60) | 60ms(90秒で underrun 0) | 34 / 24 |
| `--chunk-ms 5 --latency-ms 20`(下限要求) | 41ms(`floor` 37) | — |

これに capture 側の 5〜8ms が加わります(表示には含まれません)。

### それ以上下げたい場合 — quantum は既に下がっています

「PipeWire の quantum(既定 1024 = 21.3ms)を下げれば `out` が縮む」と思いがちですが、
**このツールでは効きません。** PipeWire はクライアントが要求したレイテンシに合わせて
quantum を動的に下げるので、`--low-latency` で走っている間はすでに 256(5.3ms)です。

```bash
pw-top -b -n 3
```

`AudioCaptureRelay` と出力先 sink の `QUANT` 列が 256 になっているはずです
(`clock.force-quantum` は 0 のまま)。実測でも `clock.force-quantum 256` を
強制した前後で合計レイテンシは 60ms、`out` も 32〜35ms で変わりませんでした。

残る `out` の 30ms 前後は **sink の ALSA 側のバッファ**です。デバイスごとの値は:

```bash
pw-dump | grep -A2 api.alsa.period-size
```

実測環境の USB CODEC は `period-size 512` + `headroom 512` = 1024 フレーム ≒ 21.3ms で、
これは graph quantum とは別枠なので force-quantum では動きません。

ここを削るには WirePlumber でデバイス個別に `api.alsa.headroom` を下げます。
`~/.config/wireplumber/wireplumber.conf.d/51-alsa-headroom.conf` に:

```
monitor.alsa.rules = [
  {
    matches = [ { node.name = "<下げたい sink の node.name>" } ]
    actions = { update-props = { api.alsa.headroom = 128 } }
  }
]
```

`systemctl --user restart wireplumber` で反映(**全アプリの音が一度途切れます**)。
戻すときはこのファイルを消して同じコマンド。

実測(USB CODEC、90 秒運転):

| | 変更前 | 変更後 |
|---|---|---|
| sink の ALSA 側 | period 512 + headroom 512 = 21.3ms | period 128 + headroom 256 = 8ms |
| `--low-latency` | 60ms(out 32〜35) | 60ms(out 25〜28) |
| `--latency-ms 20 --chunk-ms 5` | 44〜47ms(`floor` 42) | **41ms**(`floor` 37) |

どちらも `underruns 0`。**`headroom = 128` と書いても実際に効いたのは 256** でした
(PipeWire が period に合わせて調整する)。それ以上は書いても下がりません。

**永続設定**であり、削りすぎると xrun(プチノイズ)を招きます。デバイスによっては
128 でも足りずに音が割れるので、少しずつ試してください。

### 下げると何が起きるか

バッファが薄くなるぶん、CPU が詰まったときに枯れやすくなります。枯れた分は数 ms で
無音へフェードして埋める(`underruns` が増える)ので 1 回なら聞こえませんが、
連続するようなら `--latency-ms` を上げてください。

`pads` は「枯れてはいないが、ドリフト補正の都合で 1 チャンクに足りない分を
埋めた」回数です。増えていても異常ではありません(1 フレーム程度の埋めが大半)。目標が実現不能なほど低い場合は
実現できる一番浅い水位に切り替わり、`floor` として表示されます。

## 注意

`.monitor` source は「出力デバイスへ流れている音」をcaptureします。

同じ出力デバイスへ再出力すると、AudioCaptureRelay自身の音を再度拾ってループする場合があります。その場合は `--sink` で別のsinkを指定するか、`pavucontrol` で AudioCaptureRelay の出力先を変更してください。

null sinkを作る例:

```bash
pactl load-module module-null-sink \
  sink_name=discord_share \
  sink_properties=device.description=DiscordShare
```

## 開発

テストの対象は `src/domain/`(純粋な側)だけです。標準ライブラリしか使わないので、
PulseAudio も ncurses も無い環境でビルドできます:

```bash
cmake -S . -B build-tests -G Ninja -DACR_BUILD_APP=OFF -DACR_BUILD_TESTS=ON
cmake --build build-tests
ctest --test-dir build-tests --output-on-failure
```

Catch2 v3 はシステムにあればそれを使い、無ければ取ってきます。

依存の向きは `main` → `app` / `adapters` → `domain` の一方向です。`domain/` に
`<pulse/*>` / `<ncurses.h>` / `<iostream>` を持ち込まないこと。規約は
[CLAUDE.md](CLAUDE.md) に全部書いてあります。

## このコードは AI が書いています

このリポジトリの C++ は、Anthropic の Claude が対話を重ねながら書いたものです。
層の分け方、ドリフト補正の考え方、書き込みのペーシング — **設計判断も含めて**
Claude によるものです。

リポジトリの持ち主である私がやったのは、次の 2 つです。

- **規約を書いたこと。** `CLAUDE.md` は私が書いたコーディング規約が元になっています。
  ただし、それを解釈して個々の場面で何を意味するかを決めたのは Claude です。
- **音を聴いて確かめたこと。** README の実測値はすべて手元の機材で取ったものです。
  プチノイズが乗っていないか、途切れないか、画面共有に正しく乗るか — その判断は
  私がしています。

やって**いない**のは、コードを 1 行ずつ読むレビューです。`tests/` は `src/domain/` を
覆っていて通りますし、PipeWire-Pulse(Arch / CachyOS)で長時間動かしてもいますが、
それは動作確認であって、読んだことにはなりません。

ですので、**大事な環境で動かす前に、ご自分でコードを読んでください。**
リアルタイムのオーディオスレッドを持ち、サウンドサーバに直接話しかけるものです。
「私の環境では動いている」もので、「人間が監査した」ものではありません。

## ライセンス

MIT。[LICENSE](LICENSE) を参照してください。
