#include <cassert>
#include <cstdio>
#include <vector>

#include "buddy_ui.hpp"

int main(void)
{
    std::vector<uint16_t> storage(buddy::display::PixelCount);
    buddy::display::renderSelector(storage);
    assert(buddy::display::selectorHit(20, 80) == buddy::display::Mode::Codex);
    assert(buddy::display::selectorHit(200, 80) == buddy::display::Mode::Claude);
    assert(buddy::display::selectorHit(160, 40) == buddy::display::Mode::None);

    buddy::claude::Model model;
    buddy::claude::init(model);
    buddy::display::renderClaude(model, 0, false, 0, true, 75, false, storage);
    assert(buddy::display::claudeHit(model, 20, 220) ==
           buddy::display::ClaudeAction::PagePet);
    assert(buddy::display::claudeHit(model, 100, 220) ==
           buddy::display::ClaudeAction::PageActivity);
    assert(buddy::display::claudeHit(model, 180, 220) ==
           buddy::display::ClaudeAction::PageClock);
    assert(buddy::display::claudeHit(model, 260, 220) ==
           buddy::display::ClaudeAction::PageInfo);

    model.page = buddy::claude::Page::Activity;
    assert(buddy::display::claudeHit(model, 290, 100) ==
           buddy::display::ClaudeAction::ActivityUp);
    assert(buddy::display::claudeHit(model, 290, 175) ==
           buddy::display::ClaudeAction::ActivityDown);

    model.page = buddy::claude::Page::Info;
    assert(buddy::display::claudeHit(model, 100, 130) ==
           buddy::display::ClaudeAction::ToggleMute);
    assert(buddy::display::claudeHit(model, 100, 175) ==
           buddy::display::ClaudeAction::SwitchMode);
    model.promptActive = true;
    assert(buddy::display::claudeHit(model, 50, 160) ==
           buddy::display::ClaudeAction::Approve);
    assert(buddy::display::claudeHit(model, 200, 160) ==
           buddy::display::ClaudeAction::Deny);
    puts("buddy_ui tests passed");
    return 0;
}
