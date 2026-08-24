#pragma once

#include <cstdint>
#include <vector>

namespace acr {

    // リングが枯れたときに埋めた分の状態。チャンクをまたいで持ち越す。
    struct PaddingState {
        std::int16_t last_l = 0;   // 最後に流した「本物の」サンプル
        std::int16_t last_r = 0;
        int starved_frames = 0;    // 連続で埋めているフレーム数
    };

    // 枯れが続いたときに無音へ落としきるまでの長さ(5ms)。
    // 最後のサンプルを保持し続けると DC が乗り、復帰の瞬間に「ブツッ」と鳴る。
    constexpr int PAD_FADE_FRAMES = 240;

    // 実音声が戻ったときの立ち上がり(1ms)。無音から急に戻すのも段差になる。
    // ただしこれは「0 まで落ちきってから戻る」ときの長さ。実際のランプ長は
    // 埋め終わりのゲインに比例させる(下の FillResult のコメントを参照)。
    constexpr int PAD_RECOVER_FRAMES = 48;

    // assemble_output が「何をしたか」。埋めた事実が呼び出し側から見えないと、
    // 枯れていないのに埋める経路(ドリフト補正で 1 チャンク未満しか消費しない
    // とき)が underruns にも現れず、誰にも気付かれないまま音を削る。
    struct FillResult {
        int padded_frames = 0;     // このチャンクで埋めたフレーム数(0 なら全部実音声)
        int recovered_frames = 0;  // 復帰の立ち上げに使ったフレーム数
    };

    // popped(リングから取れた分)から 1 チャンク分の出力を組み立てる。
    //  - 多めに取れていたら、余分は捨て際をクロスフェードして捨てる
    //  - 足りなければ最後のサンプルで埋めつつ、PAD_FADE_FRAMES で 0 へ落とす
    //  - 埋めたあとに実音声が戻ったら立ち上げる。ランプ長は**実際に減衰した分**に
    //    比例させる(1 フレームしか埋めていないなら減衰していないので掛けない)
    FillResult assemble_output(std::vector<std::int16_t>& output,
                               const std::vector<std::int16_t>& popped,
                               int chunk_frames,
                               PaddingState& pad);

    // 出力音量。paused なら無音、muted なら 0 倍。
    void apply_volume(std::vector<std::int16_t>& output, bool paused, bool muted, float volume);

} // namespace acr
