# AudioCaptureRelay TUI

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

## ビルド

```bash
cmake -S . -B build -G Ninja
cmake --build build
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

TUIなし:

```bash
./build/audio_capture_relay --no-tui
```

波形非表示で開始:

```bash
./build/audio_capture_relay --no-waveform
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
実測(PipeWire, quantum 1024 = 21.3ms、いずれも 14〜90 秒運転):

| 設定 | 実測レイテンシ | 内訳 ring / out |
|---|---|---|
| 既定(`--chunk-ms 20 --latency-ms 120`) | 約130ms | 36 / 80 |
| `--chunk-ms 10 --latency-ms 50` | 71ms | 20 / 41 |
| `--low-latency`(chunk 5 / target 60) | 60ms(90秒で underrun 0) | 25 / 33 |
| `--chunk-ms 5 --latency-ms 40` | 45〜50ms(60秒で underrun 1回) | 15 / 32 |
| `--chunk-ms 5 --latency-ms 10`(下限要求) | 46ms(`floor` 表示) | — |

これに capture 側の 5〜8ms が加わります(表示には含まれません)。

### それ以上下げたい場合

残りはほぼ **PipeWire の quantum**(既定 1024 = 21.3ms)です。sink がこの 1〜2 個分を
抱えるので、`out` の 30ms 前後はこれが正体です。

```bash
pw-metadata -n settings 0 clock.force-quantum 256
```

```bash
pw-metadata -n settings 0 clock.force-quantum 0
```

**全アプリに効く設定**で、負荷が高い環境では他のアプリごと音が途切れます。戻すのは
2 つ目のコマンド(再起動でも戻ります)。

### 下げると何が起きるか

バッファが薄くなるぶん、CPU が詰まったときに枯れやすくなります。枯れた分は数 ms で
無音へフェードして埋める(`underruns` が増える)ので 1 回なら聞こえませんが、
連続するようなら `--latency-ms` を上げてください。目標が実現不能なほど低い場合は
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
