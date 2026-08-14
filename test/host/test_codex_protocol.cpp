#include "codex_protocol.hpp"

#include <cassert>
#include <cstdio>
#include <cstring>
#include <array>

static void test_identity_and_descriptor(void)
{
    static const uint8_t expected[] = {
        0x06, 0x00, 0xFF, 0x09, 0x01, 0xA1, 0x01, 0x85, 0x06,
        0x15, 0x00, 0x26, 0xFF, 0x00, 0x75, 0x08, 0x95, 0x3F,
        0x09, 0x01, 0x81, 0x02, 0x95, 0x3F, 0x09, 0x02, 0x91,
        0x02, 0xC0,
    };
    assert(buddy::codex::VendorId == 0x303A);
    assert(buddy::codex::ProductId == 0x8360);
    assert(buddy::codex::ReportId == 6);
    assert(buddy::codex::HidReportMap.size() == sizeof(expected));
    assert(memcmp(buddy::codex::HidReportMap.data(), expected, sizeof(expected)) == 0);
}

static void test_key_encoding(void)
{
    static const char *const expected_ids[] = {
        "AG00", "AG01", "AG02", "AG03", "AG04", "AG05",
        "ACT06", "ACT07", "ACT08", "ACT09", "ACT10", "ACT12",
        "ENC_CC", "ENC_CW", "ENC",
    };
    for (size_t i = 0; i < sizeof(expected_ids) / sizeof(expected_ids[0]); ++i) {
        assert(buddy::codex::keyId(static_cast<buddy::codex::Key>(i)) == expected_ids[i]);
    }
    assert(buddy::codex::keyId(static_cast<buddy::codex::Key>(99)).empty());

    std::array<char, 192> json{};
    size_t length = 0;
    assert(buddy::codex::encodeKeyEvent(
               buddy::codex::Key::Agent3, buddy::codex::KeyAction::Press,
               json, length) == buddy::codex::Result::Ok);
    assert(strcmp(json.data(),
                  "{\"method\":\"v.oai.hid\",\"params\":{\"k\":\"AG02\",\"act\":1,\"ag\":2}}") == 0);
    assert(length == strlen(json.data()));

    assert(buddy::codex::encodeKeyEvent(
               buddy::codex::Key::Mic, buddy::codex::KeyAction::Release,
               json, length) == buddy::codex::Result::Ok);
    assert(strcmp(json.data(),
                  "{\"method\":\"v.oai.hid\",\"params\":{\"k\":\"ACT10\",\"act\":0}}") == 0);

    assert(buddy::codex::encodeKeyEvent(
               buddy::codex::Key::DialClockwise, buddy::codex::KeyAction::Step,
               json, length) == buddy::codex::Result::Ok);
    assert(strcmp(json.data(),
                  "{\"method\":\"v.oai.hid\",\"params\":{\"k\":\"ENC_CW\",\"act\":2}}") == 0);

    assert(buddy::codex::encodeKeyEvent(
               buddy::codex::Key::Agent1, buddy::codex::KeyAction::Step,
               json, length) == buddy::codex::Result::InvalidArgument);
    assert(buddy::codex::encodeKeyEvent(
               buddy::codex::Key::Fast, buddy::codex::KeyAction::Step,
               json, length) == buddy::codex::Result::InvalidArgument);
    assert(buddy::codex::encodeKeyEvent(
               buddy::codex::Key::DialCounterClockwise, buddy::codex::KeyAction::Press,
               json, length) == buddy::codex::Result::InvalidArgument);
}

static void test_direction_encoding(void)
{
    static const char *const expected_angles[] = {"0.00", "0.25", "0.50", "0.75"};
    std::array<char, 128> json{};
    size_t length = 0;
    for (size_t i = 0; i < sizeof(expected_angles) / sizeof(expected_angles[0]); ++i) {
        assert(buddy::codex::encodeDirectionEvent(
                   static_cast<buddy::codex::Direction>(i), true, json, length) ==
               buddy::codex::Result::Ok);
        assert(strstr(json.data(), expected_angles[i]) != nullptr);
    }
    assert(buddy::codex::encodeDirectionEvent(
               buddy::codex::Direction::Up, true, json, length) ==
           buddy::codex::Result::Ok);
    assert(strcmp(json.data(),
                  "{\"method\":\"v.oai.rad\",\"params\":{\"a\":0.75,\"d\":1.0}}") == 0);
    assert(buddy::codex::encodeDirectionEvent(
               buddy::codex::Direction::Left, false, json, length) ==
           buddy::codex::Result::Ok);
    assert(strcmp(json.data(),
                  "{\"method\":\"v.oai.rad\",\"params\":{\"a\":0.50,\"d\":0.0}}") == 0);
    assert(buddy::codex::encodeDirectionEvent(
               static_cast<buddy::codex::Direction>(99), false, json, length) ==
           buddy::codex::Result::InvalidArgument);
}

static void test_fragmentation_and_reassembly(void)
{
    std::array<char, 192> json{};
    size_t json_length = 0;
    std::array<buddy::codex::Report, 4> reports{};
    size_t report_count = 0;
    assert(buddy::codex::encodeKeyEvent(
               buddy::codex::Key::Agent6, buddy::codex::KeyAction::Press,
               json, json_length) == buddy::codex::Result::Ok);
    assert(buddy::codex::encodeReports(
               std::string_view{json.data(), json_length}, reports, report_count) == buddy::codex::Result::Ok);
    assert(report_count == 1);
    assert(reports[0][0] == 2);
    assert(reports[0][1] == json_length + 1);
    assert(memcmp(reports[0].data() + 2, json.data(), json_length) == 0);
    assert(reports[0][2 + reports[0][1] - 1] == '\n');

    buddy::codex::Decoder decoder;
    decoder.reset();
    assert(decoder.push(reports[0]) ==
           buddy::codex::Result::Complete);
    assert((decoder.json() == std::string_view{json.data(), json_length}));

    const char long_json[] =
        "{\"method\":\"v.oai.thstatus\",\"params\":[{\"id\":0,\"c\":255,\"b\":1.0,\"e\":\"breath\",\"s\":0.5}],\"id\":7}";
    assert(buddy::codex::encodeReports(
               long_json, reports, report_count) ==
           buddy::codex::Result::Ok);
    assert(report_count == 2);
    decoder.reset();
    assert(decoder.push(reports[0]) ==
           buddy::codex::Result::Incomplete);
    assert(decoder.push(reports[1]) ==
           buddy::codex::Result::Complete);
    assert(decoder.json() == long_json);

    assert(buddy::codex::encodeReports(
               long_json, std::span{reports}.first(1), report_count) ==
           buddy::codex::Result::NoSpace);
}

static void test_decoder_validation_and_report_id(void)
{
    const char request[] =
        "{\"method\":\"v.oai.thstatus\",\"params\":[{\"id\":0,\"e\":\"breath\"}],\"id\":7}";
    std::array<buddy::codex::Report, 3> reports{};
    size_t report_count = 0;
    assert(buddy::codex::encodeReports(
               request, reports, report_count) ==
           buddy::codex::Result::Ok);

    buddy::codex::Decoder decoder;
    decoder.reset();
    assert(decoder.push(reports[0]) ==
           buddy::codex::Result::Incomplete);

    uint8_t raw_report[buddy::codex::ReportBodySize + 1] = {0};
    raw_report[0] = buddy::codex::ReportId;
    memcpy(raw_report + 1, reports[1].data(), buddy::codex::ReportBodySize);
    assert(decoder.push(raw_report) ==
           buddy::codex::Result::Complete);
    assert(decoder.json() == request);

    decoder.reset();
    reports[0][1] = buddy::codex::PayloadSize + 1;
    assert(decoder.push(reports[0]) ==
           buddy::codex::Result::InvalidReport);
}

static void test_decoder_resynchronizes(void)
{
    buddy::codex::Decoder decoder;
    decoder.reset();
    const char incomplete_request[] = "{\"method\":\"broken\"";
    uint8_t dropped[buddy::codex::ReportBodySize] = {
        2, sizeof(incomplete_request) - 1};
    memcpy(dropped + 2, incomplete_request, sizeof(incomplete_request) - 1);
    assert(decoder.push(dropped) ==
           buddy::codex::Result::Incomplete);

    const char replacement[] = "{\"method\":\"sys.version\",\"id\":1}";
    uint8_t replacement_report[buddy::codex::ReportBodySize] = {2, sizeof(replacement) - 1};
    memcpy(replacement_report + 2, replacement, sizeof(replacement) - 1);
    assert(decoder.push(replacement_report) ==
           buddy::codex::Result::Complete);
    assert(decoder.json() == replacement);
}

int main(void)
{
    test_identity_and_descriptor();
    test_key_encoding();
    test_direction_encoding();
    test_fragmentation_and_reassembly();
    test_decoder_validation_and_report_id();
    test_decoder_resynchronizes();
    puts("codex_protocol host tests passed");
    return 0;
}
