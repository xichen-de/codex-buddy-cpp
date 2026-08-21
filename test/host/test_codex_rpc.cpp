#include "codex_rpc.hpp"

#include <cassert>
#include <cstdio>
#include <cstring>

using buddy::codex::Event;
using buddy::codex::Model;
using buddy::codex::SlotStatus;

static void test_standard_responses(void)
{
    buddy::codex::RequestResult result;
    assert(buddy::codex::handleRequest("{\"method\":\"sys.version\",\"id\":1}",
                                       {}, result) == buddy::codex::Result::Ok);
    assert(result.responseView().find("1.0.0-cores3") != std::string_view::npos);
    assert(result.responseView().find("\"id\":1") != std::string_view::npos);

    assert(buddy::codex::handleRequest(
               "{\"note\":\"method\",\"method\":\"sys.version\",\"id\":2}",
               {}, result) == buddy::codex::Result::Ok);
    assert(result.responseView().find("1.0.0-cores3") != std::string_view::npos);

    buddy::codex::RequestContext context{
        .batteryPercent = 42, .batteryKnown = true, .charging = true};
    assert(buddy::codex::handleRequest("{\"method\":\"device.status\",\"id\":\"a\"}",
                                       context, result) == buddy::codex::Result::Ok);
    assert(result.responseView().find("\"battery\":42") != std::string_view::npos);
    assert(result.responseView().find("\"is_charging\":true") != std::string_view::npos);

    context.batteryKnown = false;
    assert(buddy::codex::handleRequest("{\"method\":\"device.status\",\"id\":2}",
                                       context, result) == buddy::codex::Result::Ok);
    assert(result.responseView().find("\"battery\":null") != std::string_view::npos);

    assert(buddy::codex::handleRequest("{\"method\":\"no.such.method\",\"id\":9}",
                                       {}, result) == buddy::codex::Result::Ok);
    assert(result.responseView().find("-32601") != std::string_view::npos);
}

static void test_thread_status_updates(void)
{
    Model model;
    init(model);
    Event connected = buddy::codex::ConnectionChanged{
        buddy::codex::Connection::Connected};
    assert(applyEvent(model, connected));
    buddy::codex::RequestContext context{.model = &model};
    buddy::codex::RequestResult result;
    const char json[] =
        "{\"method\":\"v.oai.thstatus\",\"params\":["
        "{\"id\":0,\"c\":16777215,\"b\":1,\"e\":\"solid\",\"s\":0.5},"
        "{\"id\":1,\"c\":255,\"b\":1,\"e\":\"breath\"},"
        "{\"id\":2,\"c\":65280,\"b\":1,\"e\":\"solid\"},"
        "{\"id\":3,\"c\":16744448,\"b\":1,\"e\":\"solid\"},"
        "{\"id\":4,\"c\":16711680,\"b\":1,\"e\":\"solid\"},"
        "{\"id\":99,\"c\":1}],\"id\":7}";
    assert(buddy::codex::handleRequest(json, context, result) == buddy::codex::Result::Ok);
    assert(result.eventCount == 5);
    assert(std::get<buddy::codex::SlotStatusChanged>(result.events[0]).value.status == SlotStatus::Idle);
    assert(std::get<buddy::codex::SlotStatusChanged>(result.events[1]).value.status == SlotStatus::Thinking);
    assert(std::get<buddy::codex::SlotStatusChanged>(result.events[2]).value.status == SlotStatus::Complete);
    assert(std::get<buddy::codex::SlotStatusChanged>(result.events[3]).value.status == SlotStatus::RequiresInput);
    assert(std::get<buddy::codex::SlotStatusChanged>(result.events[4]).value.status == SlotStatus::Error);
    assert(strcmp(std::get<buddy::codex::SlotStatusChanged>(result.events[1]).value.effect.data(),
                  "breath") == 0);
    assert(result.responseView().find("\"ok\":true") != std::string_view::npos);
}

static void test_validation(void)
{
    buddy::codex::RequestResult result;
    assert(buddy::codex::handleRequest("not json", {}, result) ==
           buddy::codex::Result::InvalidReport);
    assert(buddy::codex::handleRequest({}, {}, result) ==
           buddy::codex::Result::InvalidArgument);
}

static void test_lighting_config(void)
{
    Model model;
    init(model);
    buddy::codex::RequestContext context{.model = &model};
    buddy::codex::RequestResult result;
    const char json[] =
        "{\"method\":\"v.oai.rgbcfg\",\"params\":{"
        "\"ambient\":{\"c\":1122867,\"b\":0.5,\"e\":\"solid\",\"s\":2},"
        "\"keys\":{\"c\":16711680,\"b\":1,\"e\":\"breath\"}},\"id\":3}";
    assert(buddy::codex::handleRequest(json, context, result) == buddy::codex::Result::Ok);
    assert(result.eventCount == 1);
    const auto &lighting =
        std::get<buddy::codex::LightingConfigured>(result.events[0]);
    assert(lighting.ambient.color == 1122867U);
    assert(lighting.ambient.brightness == 0.5f);
    assert(strcmp(lighting.keys.effect.data(), "breath") == 0);
    assert(applyEvent(model, result.events[0]));
    assert(model.keyLight.color == 16711680U);
}

int main(void)
{
    test_standard_responses();
    test_thread_status_updates();
    test_lighting_config();
    test_validation();
    puts("codex_rpc host tests passed");
    return 0;
}
