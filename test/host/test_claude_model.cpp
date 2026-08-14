#include <cassert>
#include <cstdio>
#include <cstring>

#include "claude_model.hpp"

using namespace buddy::claude;

int main(void)
{
    Model model;
    init(model);
    assert(model.page == Page::Pet);
    assert(petState(model, 0) == PetState::Sleep);

    setConnection(model, Connection::Connected);
    assert(petState(model, 10) == PetState::Idle);
    model.runningSessions = 1;
    assert(petState(model, 10) == PetState::Busy);
    model.promptActive = true;
    assert(petState(model, 10) == PetState::Attention);
    recordDecision(model, true, 100);
    assert(model.approvals == 1);
    assert(!model.promptActive);
    assert(petState(model, 101) == PetState::Heart);
    assert(petState(model, 2000) == PetState::Busy);

    for (size_t index = 0; index < ModelEntryCount; ++index)
        snprintf(model.entries[index].data(), model.entries[index].size(),
                 "entry %u", (unsigned)index);
    assert(activityCount(model) == 8);
    scrollActivity(model, 1);
    scrollActivity(model, 1);
    assert(model.activityOffset == 2);
    scrollActivity(model, -1);
    assert(model.activityOffset == 1);

    int64_t local_seconds = 0;
    assert(!clockSeconds(model, 2000, local_seconds));
    setClock(model, 1775731234ULL, -25200, 2000);
    assert(clockSeconds(model, 5000, local_seconds));
    assert(local_seconds == 1775706037LL);

    triggerDizzy(model, 5000);
    assert(petState(model, 5001) == PetState::Dizzy);
    setFaceDown(model, true);
    assert(petState(model, 5001) == PetState::Sleep);
    setFaceDown(model, false);

    model.lastSnapshotMs = 100;
    expireConnection(model, 30101, 30000);
    assert(model.connection == Connection::Disconnected);
    puts("claude_model tests passed");
    return 0;
}
