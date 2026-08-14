#include <cassert>
#include <cstdio>
#include <cstring>

#include "claude_protocol.hpp"

using namespace buddy::claude;

static Action handle(Model &model, const char *json)
{
    const Context context{
        .nowMs = 1234,
        .uptimeSeconds = 42,
        .secure = true,
        .batteryKnown = true,
        .batteryPercent = 87,
        .batteryMv = 4012,
        .batteryMa = -120,
        .usbPowered = true,
    };
    Action action;
    assert(handleLine(json, model, context, action) == Result::Complete);
    return action;
}

static void test_decoder(void)
{
    Decoder decoder;
    const uint8_t first[] = "{\"total\":";
    size_t consumed = 0;
    assert(decoder.push({first, sizeof(first) - 1}, consumed) ==
           Result::Incomplete);
    assert(consumed == sizeof(first) - 1);
    const uint8_t second[] = "0,\"running\":0,\"waiting\":0}\nnext";
    assert(decoder.push({second, sizeof(second) - 1}, consumed) ==
           Result::Complete);
    assert(decoder.line() == "{\"total\":0,\"running\":0,\"waiting\":0}");
    assert(consumed < sizeof(second) - 1);
}

static void test_snapshot(void)
{
    Model model;
    init(model);
    setConnection(model, Connection::Connected);
    const char *json =
        "{\"total\":3,\"running\":1,\"waiting\":1,"
        "\"msg\":\"approve: Bash\",\"entries\":[\"10:42 git push\","
        "\"10:41 test\"],\"tokens\":184502,\"tokens_today\":31200,"
        "\"prompt\":{\"id\":\"req_abc123\",\"tool\":\"Bash\","
        "\"hint\":\"rm -rf /tmp/foo\"}}";
    const Action action = handle(model, json);
    assert(action.modelChanged && !action.clockChanged &&
           !action.sendResponse);
    assert(model.totalSessions == 3 && model.runningSessions == 1);
    assert(model.tokens == 184502 && model.tokensToday == 31200);
    assert(strcmp(model.entries[0].data(), "10:42 git push") == 0);
    assert(model.promptActive);
    assert(strcmp(model.promptId.data(), "req_abc123") == 0);
    assert(strcmp(model.promptHint.data(), "rm -rf /tmp/foo") == 0);
    assert(model.lastSnapshotMs == 1234);

    model.runningSessions = 1;
    handle(model, "{\"total\":1,\"running\":0,\"waiting\":0}");
    assert(model.transientState == PetState::Celebrate);
    assert(model.transientUntilMs == 3034);
}

static void test_time_sync(void)
{
    Model model;
    init(model);
    const Action action = handle(model, "{\"time\":[1775731234,-25200]}");
    assert(action.modelChanged && action.clockChanged &&
           !action.sendResponse);
    assert(model.clockValid);
    assert(model.clockEpochSeconds == 1775731234ULL);
    assert(model.clockTimezoneOffset == -25200);
    int64_t local = 0;
    assert(clockSeconds(model, 2234, local));
    assert(local == 1775706035LL);
}

static void test_commands(void)
{
    Model model;
    init(model);
    Action action = handle(model, "{\"cmd\":\"owner\",\"name\":\"Xi\"}");
    assert(action.sendResponse && action.modelChanged && action.persistModel);
    assert(strcmp(model.owner.data(), "Xi") == 0);
    assert(action.responseView() == "{\"ack\":\"owner\",\"ok\":true}");

    action = handle(model, "{\"cmd\":\"status\"}");
    assert(action.sendResponse);
    assert(action.responseView().find("\"sec\":true") != std::string_view::npos);
    assert(action.responseView().find("\"pct\":87") != std::string_view::npos);

    action = handle(model, "{\"cmd\":\"unpair\"}");
    assert(action.sendResponse && action.forgetBondAfterResponse);
}

static void test_permission(void)
{
    char json[160];
    size_t length = 0;
    assert(encodePermission("req_abc123", true, json, length) == Result::Complete);
    assert(length == strlen(json));
    assert(strcmp(json,
        "{\"cmd\":\"permission\",\"id\":\"req_abc123\","
        "\"decision\":\"once\"}") == 0);
    assert(encodePermission("quoted\"id", false, json, length) == Result::Complete);
    assert(strstr(json, "quoted\\\"id") != nullptr);
    assert(strstr(json, "\"decision\":\"deny\"") != nullptr);
}

int main(void)
{
    test_decoder();
    test_snapshot();
    test_time_sync();
    test_commands();
    test_permission();
    puts("claude_protocol tests passed");
    return 0;
}
