#include "domain/waveform.h"

#include <algorithm>
#include <cmath>
#include <cstdint>

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

    std::vector<std::string> make_braille_waveform(const std::vector<WaveBucket>& buckets, int cols, int rows) {
        if (cols <= 0 || rows <= 0) return {};
        if (buckets.empty()) return std::vector<std::string>(rows, std::string());

        const int pixel_w = cols * 2;
        const int pixel_h = rows * 4;

        std::vector<std::vector<unsigned char>> cells(rows, std::vector<unsigned char>(cols, 0));

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

            float lo = buckets[span.begin].min;
            float hi = buckets[span.begin].max;
            for (std::size_t b = span.begin + 1; b < span.end; ++b) {
                lo = std::min(lo, buckets[b].min);
                hi = std::max(hi, buckets[b].max);
            }

            const int top = y_of(hi);
            const int bottom = y_of(lo);

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
                    cells[cell_y][cell_x] |= DOT_MAP[y % 4][x % 2];
                }
            }

            prev_top = top;
            prev_bottom = bottom;
        }

        std::vector<std::string> lines;
        lines.reserve(rows);

        for (int y = 0; y < rows; ++y) {
            std::string line;
            line.reserve(static_cast<std::size_t>(cols) * 3);

            for (int x = 0; x < cols; ++x) {
                std::uint32_t code = 0x2800u + cells[y][x];
                line.push_back(static_cast<char>(0xE0 | ((code >> 12) & 0x0F)));
                line.push_back(static_cast<char>(0x80 | ((code >> 6) & 0x3F)));
                line.push_back(static_cast<char>(0x80 | (code & 0x3F)));
            }

            lines.push_back(std::move(line));
        }

        return lines;
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
