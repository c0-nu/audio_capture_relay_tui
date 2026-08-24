#include "domain/device_match.h"

#include <catch2/catch_test_macros.hpp>

using namespace acr;

namespace {

    DeviceInfo make(const std::string& name, const std::string& desc, bool monitor = false, bool def = false) {
        DeviceInfo s;
        s.name = name;
        s.description = desc;
        s.is_monitor = monitor;
        s.is_default = def;
        return s;
    }

    std::vector<DeviceInfo> sample() {
        return {
            make("alsa_input.mic", "Internal Mic", false, false),
            make("alsa_output.hdmi.monitor", "Monitor of HDMI", true, false),
            make("alsa_input.usb", "USB Codec", false, true),
        };
    }

} // namespace

TEST_CASE("引数が空ならデフォルト source", "[source]") {
    auto m = match_device(sample(), "");
    REQUIRE(m.status == DeviceMatch::Status::Ok);
    CHECK(m.source->name == "alsa_input.usb");
}

TEST_CASE("デフォルトが無ければ先頭", "[source]") {
    auto sources = sample();
    for (auto& s : sources) s.is_default = false;

    auto m = match_device(sources, "");
    REQUIRE(m.status == DeviceMatch::Status::Ok);
    CHECK(m.source->name == "alsa_input.mic");
}

TEST_CASE("番号で選べる", "[source]") {
    auto m = match_device(sample(), "1");
    REQUIRE(m.status == DeviceMatch::Status::Ok);
    CHECK(m.source->name == "alsa_output.hdmi.monitor");

    CHECK(match_device(sample(), "99").status == DeviceMatch::Status::NoMatch);
}

TEST_CASE("名前・説明の完全一致が部分一致より優先される", "[source]") {
    auto sources = sample();
    sources.push_back(make("USB Codec extra", "another one"));

    auto m = match_device(sources, "USB Codec");
    REQUIRE(m.status == DeviceMatch::Status::Ok);
    CHECK(m.source->name == "alsa_input.usb");
}

TEST_CASE("部分一致は大文字小文字を無視し、1 件なら確定", "[source]") {
    auto m = match_device(sample(), "hdmi");
    REQUIRE(m.status == DeviceMatch::Status::Ok);
    CHECK(m.source->name == "alsa_output.hdmi.monitor");
}

TEST_CASE("部分一致が複数なら Ambiguous で候補を返す", "[source]") {
    auto m = match_device(sample(), "alsa");
    REQUIRE(m.status == DeviceMatch::Status::Ambiguous);
    CHECK(m.candidates.size() == 3);
    CHECK_FALSE(m.source.has_value());
}

TEST_CASE("一致なし・空リスト", "[source]") {
    CHECK(match_device(sample(), "zzz").status == DeviceMatch::Status::NoMatch);
    CHECK(match_device({}, "").status == DeviceMatch::Status::Empty);
}

TEST_CASE("sort_devices はデフォルト先頭・INPUT が MONITOR より前", "[source]") {
    auto sources = sample();
    sort_devices(sources);

    CHECK(sources[0].is_default);          // USB Codec
    CHECK_FALSE(sources[1].is_monitor);    // Internal Mic
    CHECK(sources[2].is_monitor);          // Monitor of HDMI
}
