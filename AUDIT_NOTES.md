# 監査・改善作業メモ

最終更新: 2026-09-02

compact 後に作業と判断経緯を引き継ぐためのメモ。
`CLAUDE.md` の設計規約が引き続き正本であり、このファイルは作業状態と未解決事項の記録とする。

## 絶対に維持する方針

- `PACE_SLACK_CHUNKS = 4` は実測による調整結果。今回の改善対象にしない。
- `pa_simple` は維持し、実際の Discord 画面共有で検証できるまで `pa_stream` へ移行しない。
- レイテンシ制御は「リング + サーバ側キュー」の合計を見る。
- 排出時のクロスフェード、無音パディングの減衰・復帰ランプは壊さない。

## 現在の未コミット変更

作業開始時の worktree は clean だった。現在の変更は以下。

1. PulseAudio デバイス列挙で、operation のキャンセルと callback 失敗をエラーとして扱う。
2. `PcmRing` を `std::deque` から事前確保型の連続循環バッファへ変更。capture 開始前に 3 秒分 + 1 チャンク分を確保する。
3. TUI の波形履歴スナップショット、点字セル、行文字列を描画間で再利用。
4. ドリフト平滑化は元の固定 `alpha = 1/64` を維持。5 / 20 / 200ms の回帰テストを追加。
5. 補正の飽和時は 1 フレーム未満の端数だけを残し、積分ワインドアップを防止。
6. 最大補正量の計算を共通化し、playback の使い回しバッファを上限まで事前確保。
7. `ErrorLog` の本文と件数を同じロック内で更新。
8. `WaveHistory` を事前確保型の循環 `std::vector` へ変更。
9. プライミング中の write 失敗を記録し、終了時のブロッキング `drain` を廃止。
10. relay 有効時の sink 0 件を起動時エラーにし、`--no-relay` では sink を要求しない。
11. PulseAudio のデバイス列挙に 3 秒の接続・operation タイムアウトを追加。
12. テストを 69 件から 75 件へ追加。

## ドリフト平滑化の判断（対応済み）

### ドリフト平滑化の変更を戻した

一時的に以下の係数へ変更していた。

```text
alpha(chunk_ms) = 1 - (1 - 1/64) ^ (chunk_ms / 20)
```

これは係数を固定したのではなく、実時間の時定数を固定したもの。
しかし平滑化の主目的は、実時間上の一般的な低域通過ではなく、capture/playback の「チャンク単位のノコギリ波」を無視すること。
そのため元の `alpha = 1/64`、つまり常に約 64 チャンクで平滑化する方が設計意図に合う。

簡易シミュレーションでは、200ms チャンクで可変係数がノコギリ波へ過度に反応した。
間違った方針を固定していた「平滑化の時定数はチャンク長に依存しない」テストも置き換えた。

実施内容:

- `DRIFT_SMOOTHING_REFERENCE_ALPHA` / `DRIFT_SMOOTHING_REFERENCE_CHUNK_MS` / `smoothing_alpha_` を削除。
- `DRIFT_SMOOTHING_ALPHA = 1.0 / 64.0` を維持。
- `update()` は固定 `DRIFT_SMOOTHING_ALPHA` を使う。
- 5 / 20 / 200ms で同じ 1/64 が使われ、ノコギリ成分が生の振幅の 2% 以下に平滑化されることを検証。
- 200ms で「補正回数がゼロ」まで要求すると、4 秒の補正時間が 20 チャンクしかないことと衝突する。そのため、テストは平滑化係数と減衰量を直接検証する。

## 追加で見つかった問題の対応状況

優先度順。`pa_simple` の実行中 I/O 以外は対応済み。

### 1. `correction_debt_` の積分ワインドアップ（対応済み）

`src/domain/drift_control.cpp` で補正を約 5% に clamp しても、未適用分の debt は蓄積し続ける。
大きな一時的滞留や overflow 後に誤差が反転しても、長時間排出要求が残る可能性がある。
下限ガードによりすぐ underrun にはならないが、リングが低い水位に長時間張り付く。

飽和時は未適用の整数部を捨て、1 フレーム未満の端数だけを残す。大渋滞後に誤差が反転したら古い方向へ排出し続けない回帰テストを追加。

### 2. `popped_buffer` の予約量が最大補正量より小さい（対応済み）

`src/adapters/pulse_playback.cpp` は `chunk_frames + 64` しか reserve していない。
一方、最大補正は `max(8, chunk_frames / 20)`。
CLI 上限の 200ms チャンクでは 480 フレームなので、最初の大補正時にホットパス内で再確保される。

`max_drift_correction_frames()` を domain に置き、controller と playback の `reserve` が同じ値を使う。

### 3. ブロッキング I/O 中は失敗窓も停止フラグも効かない（一部対応・制約あり）

`pa_simple_read` / `pa_simple_write` / `pa_simple_drain` 自体が返らない場合、
3 秒の `FailureWindow` へ到達できず、main の `join()` も待ち続ける。
`pa_simple` を維持する制約があるため、これは単純な API 置き換えではなく設計判断が必要。

`PulseDeviceLister` の connect / operation 待ちは 3 秒でタイムアウトするよう修正した。終了時の `pa_simple_drain` も廃止した。

`pa_simple_read` / `pa_simple_write` そのものが戻らない場合は依然として中断できない。`pa_simple` を維持する設計制約上、スレッド強制終了のような危険な対策は入れていない。

### 4. `ErrorLog` の本文と件数が一瞬不整合になる（対応済み）

`message_` の更新後、mutex を解放してから `count_` を加算している。
その間の snapshot は「新しい本文 + 古い件数」を返す。

`count_.fetch_add(1)` も mutex 内へ入れ、snapshot との整合性を保つ。

### 5. ホットパスに残っているヒープ確保（大口は対応済み）

`WaveHistory` は事前確保型の循環 `std::vector` へ変更した。`PcmRing`、履歴 snapshot、点字描画バッファも使い回す。

TUI の波形用大口バッファは再利用済みだが、`ostringstream` や文字列連結による小さな一時確保は残っている。

### 6. プライミング中の最初の write 失敗を報告しない（対応済み）

初回失敗を `ErrorLog` と `FailureWindow` の両方に記録する。

### 7. sink が 0 件でも、既定 sink 指定は起動時に成功扱いになる（対応済み）

relay 有効時は起動時に「出力 sink がない」と明示し、`--no-relay` では sink 選択自体を行わない。

## 検証状態

現在の未コミット変更に対する結果:

- 通常ビルド成功。`-Wall -Wextra -Wpedantic` で警告ゼロ。
- domain テスト 75 / 75 成功。
- ASan + UBSan 付きで 75 / 75 成功。実行環境が ptrace 下なので LeakSanitizer のみ `detect_leaks=0`。
- `git diff --check` 成功。
- GCC `-fanalyzer` は重大警告なし。
- clang-tidy (`clang-analyzer-*`, `bugprone-*`, `performance-*`) の指摘は、主に明示変換可能な数値変換と `SharedState` の 36 byte padding。直接のメモリ破壊指摘はなし。
- 実行環境に PulseAudio / PipeWire-Pulse サーバがなく、`--list` は `pa_context_connect failed: Connection refused`。実音声・TUI・20/120/1000ms の実機検証は未実施。

## 残る検証

1. 実機で `--no-tui` を使い、20 / 120 / 1000ms を含む設定を数分ずつ確認する。
2. Discord の実際の画面共有で音が取り込まれることを確認する。
3. PulseAudio/PipeWire-Pulse サーバが実行中に無応答になる障害まで対象にするなら、`pa_simple` 制約とのトレードオフを改めて設計する。
