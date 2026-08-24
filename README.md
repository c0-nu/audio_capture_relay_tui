# AudioCaptureRelay TUI

任意の PulseAudio / PipeWire-Pulse capture source を読み取り、このアプリ自身の再生ストリームとして出力する最小限の中継ツールです。

この版では `top` / `htop` のような1画面TUIを追加し、Unicode点字文字で波形を表示します。

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

## レイテンシ

`--latency-ms`(既定 120)は **capture から実際に音が出るまでの合計**に対する目標です。
AudioCaptureRelay 自身のバッファと、PulseAudio / PipeWire 側のキューの両方を見て、
再生の消費量を少しずつ調整して合わせます。

TUI の `Latency:` 行に内訳(`ring` = 自前のバッファ、`out` = サーバ側)が出ます。
`out` が目標より深い環境では目標まで下げられません。その場合は `[hold]` と表示され、
実レイテンシは `out` + 少しの予備で落ち着きます。`--latency-ms` を上げると余裕が増えます。

## 注意

`.monitor` source は「出力デバイスへ流れている音」をcaptureします。

同じ出力デバイスへ再出力すると、AudioCaptureRelay自身の音を再度拾ってループする場合があります。その場合は `pavucontrol` で AudioCaptureRelay の出力先を別sinkに変更してください。

null sinkを作る例:

```bash
pactl load-module module-null-sink \
  sink_name=discord_share \
  sink_properties=device.description=DiscordShare
```
