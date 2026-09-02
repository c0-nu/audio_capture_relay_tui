#include "domain/waveform.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>

namespace acr {

    namespace {

        constexpr unsigned char DOT_MAP[4][2] = {
            {0x01, 0x08}, // dots 1,4
            {0x02, 0x10}, // dots 2,5
            {0x04, 0x20}, // dots 3,6
            {0x40, 0x80}  // dots 7,8
        };

        // 1 列が受け持つバケットの範囲。列がバケットより多いときも必ず 1 つは含む。
        struct Span {
            std::size_t begin = 0;
            std::size_t end = 0;
        };

        Span span_for_column(int x, int pixel_w, std::size_t bucket_count) {
            Span s;
            s.begin = static_cast<std::size_t>(static_cast<double>(x) * bucket_count / pixel_w);
            s.end = static_cast<std::size_t>(static_cast<double>(x + 1) * bucket_count / pixel_w);
            s.begin = std::min(s.begin, bucket_count - 1);
            s.end = std::clamp(s.end, s.begin + 1, bucket_count);
            return s;
        }

    } // namespace

    const char* waveform_style_name(WaveformStyle style) {
        return style == WaveformStyle::Line ? "line" : "envelope";
    }

    std::optional<WaveformStyle> parse_waveform_style(const std::string& s) {
        if (s == "envelope") return WaveformStyle::Envelope;
        if (s == "line") return WaveformStyle::Line;
        return std::nullopt;
    }

    void render_braille_waveform(const std::vector<WaveBucket>& buckets,
                                 int cols,
                                 int rows,
                                 WaveformStyle style,
                                 WaveformRenderBuffer& out) {
        if (cols <= 0 || rows <= 0) {
            out.cells.clear();
            out.lines.clear();
            return;
        }

        out.lines.resize(static_cast<std::size_t>(rows));
        if (buckets.empty()) {
            out.cells.clear();
            for (auto& line : out.lines) line.clear();
            return;
        }

        const int pixel_w = cols * 2;
        const int pixel_h = rows * 4;

        out.cells.assign(static_cast<std::size_t>(rows) * static_cast<std::size_t>(cols), 0);

        // 振幅 -> 画面 y。+1.0 が上端、-1.0 が下端。
        auto y_of = [&](float v) {
            v = std::clamp(v, -1.0f, 1.0f);
            int y = static_cast<int>(std::lround((1.0f - (v + 1.0f) * 0.5f) * (pixel_h - 1)));
            return std::clamp(y, 0, pixel_h - 1);
        };

        int prev_top = -1;
        int prev_bottom = -1;

        for (int x = 0; x < pixel_w; ++x) {
            Span span = span_for_column(x, pixel_w, buckets.size());

            int top = 0;
            int bottom = 0;

            if (style == WaveformStyle::Envelope) {
                // 列が受け持つ範囲の min〜max を塗る。幅が狭くてもピークが残る。
                float lo = buckets[span.begin].min;
                float hi = buckets[span.begin].max;
                for (std::size_t b = span.begin + 1; b < span.end; ++b) {
                    lo = std::min(lo, buckets[b].min);
                    hi = std::max(hi, buckets[b].max);
                }
                top = y_of(hi);
                bottom = y_of(lo);
            } else {
                // 代表サンプルを 1 点だけ拾う。隣の列との間は下の橋渡しで繋がるので、
                // 見た目は 1 本の線になる。
                const float value = buckets[span.end - 1].last;
                top = bottom = y_of(value);
            }

            // 隣の列と縦に離れていたら橋渡しして、途切れた点の集まりに見えないようにする。
            int draw_top = top;
            int draw_bottom = bottom;
            if (prev_top >= 0) {
                if (draw_top > prev_bottom) draw_top = prev_bottom;
                else if (draw_bottom < prev_top) draw_bottom = prev_top;
            }

            for (int y = draw_top; y <= draw_bottom; ++y) {
                int cell_x = x / 2;
                int cell_y = y / 4;
                if (cell_x >= 0 && cell_x < cols && cell_y >= 0 && cell_y < rows) {
                    out.cells[static_cast<std::size_t>(cell_y) * static_cast<std::size_t>(cols)
                              + static_cast<std::size_t>(cell_x)] |= DOT_MAP[y % 4][x % 2];
                }
            }

            prev_top = top;
            prev_bottom = bottom;
        }

        for (int y = 0; y < rows; ++y) {
            std::string& line = out.lines[static_cast<std::size_t>(y)];
            line.clear();
            line.reserve(static_cast<std::size_t>(cols) * 3);

            for (int x = 0; x < cols; ++x) {
                std::uint32_t code = 0x2800u
                                   + out.cells[static_cast<std::size_t>(y) * static_cast<std::size_t>(cols)
                                               + static_cast<std::size_t>(x)];
                line.push_back(static_cast<char>(0xE0 | ((code >> 12) & 0x0F)));
                line.push_back(static_cast<char>(0x80 | ((code >> 6) & 0x3F)));
                line.push_back(static_cast<char>(0x80 | (code & 0x3F)));
            }
        }
    }

    std::vector<std::string> make_braille_waveform(const std::vector<WaveBucket>& buckets,
                                                   int cols,
                                                   int rows,
                                                   WaveformStyle style) {
        WaveformRenderBuffer out;
        render_braille_waveform(buckets, cols, rows, style, out);
        return std::move(out.lines);
    }

    std::string meter_bar(float value, int width) {
        value = std::clamp(value, 0.0f, 1.0f);
        int filled = static_cast<int>(std::lround(value * width));
        std::string s;
        s.reserve(width + 2);
        s.push_back('[');
        for (int i = 0; i < width; ++i) s.push_back(i < filled ? '#' : '-');
        s.push_back(']');
        return s;
    }

} // namespace acr
