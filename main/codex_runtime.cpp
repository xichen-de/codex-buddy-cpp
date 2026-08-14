#include "codex_runtime.hpp"

#include <algorithm>

#include "codex_rpc.hpp"
#include "esp_log.h"
#include "esp_system.h"
#include "platform_core_s3.hpp"
#include "runtime_clock.hpp"
#include "codex_ui.hpp"

namespace buddy::runtime {
namespace {

constexpr char Tag[] = "codex_runtime";

}  // namespace

bool CodexRuntime::sendJson(std::string_view json) noexcept
{
    const esp_err_t error = codex::transport::sendJson(json);
    if (error != ESP_OK) {
        ESP_LOGW(Tag, "Control report could not be sent: %s",
                 esp_err_to_name(error));
        return false;
    }
    return true;
}

void CodexRuntime::render() noexcept
{
    const codex::Action *active = input_.active ? &input_.action : nullptr;
    codex::ui::render(model_, active, uptimeMs(), platform::framebuffer());
    const esp_err_t error = platform::present();
    if (error != ESP_OK)
        ESP_LOGE(Tag, "Display update failed: %s", esp_err_to_name(error));
}

void CodexRuntime::onConnectionChanged(bool connected) noexcept
{
    Event event{.type = EventType::Connection, .connected = connected};
    if (!queue_.send(event))
        ESP_LOGW(Tag, "Runtime queue full; connection event dropped");
}

void CodexRuntime::onReport(std::span<const std::uint8_t> report) noexcept
{
    if (report.empty() || report.size() > Event{}.report.size()) {
        ESP_LOGW(Tag, "Rejected output report of length %u",
                 static_cast<unsigned>(report.size()));
        return;
    }
    Event event{.type = EventType::Report, .reportLength = report.size()};
    std::ranges::copy(report, event.report.begin());
    if (!queue_.send(event))
        ESP_LOGW(Tag, "Runtime queue full; output report dropped");
}

bool CodexRuntime::applyConnection(bool connected) noexcept
{
    if (!connected) {
        const codex::Action cancelled = codex::cancel(input_);
        if (cancelled.type != codex::ActionType::None)
            (void)codex::handleAction(model_, cancelled,
                                      codex::ActionPhase::Cancel, this);
    }
    const bool changed = codex::applyEvent(
        model_, codex::Event{codex::ConnectionChanged{
                    connected ? codex::Connection::Connected
                              : codex::Connection::Disconnected}});
    if (changed)
        ESP_LOGI(Tag, "Codex connection state: %s",
                 connected ? "connected" : "disconnected");
    if (!connected) decoder_.reset();
    return changed;
}

bool CodexRuntime::processCompleteRequest() noexcept
{
    codex::RequestContext context{.model = &model_};
    if (const auto battery = platform::battery()) {
        context.batteryKnown = true;
        context.batteryPercent = battery->percent;
        context.charging = battery->charging;
    }
    codex::RequestResult result;
    const codex::Result handled =
        codex::handleRequest(decoder_.json(), context, result);
    if (handled != codex::Result::Ok) {
        ESP_LOGW(Tag, "RPC request rejected: %d", static_cast<int>(handled));
        decoder_.reset();
        return false;
    }

    bool changed = false;
    for (std::size_t index = 0; index < result.eventCount; ++index)
        changed |= codex::applyEvent(model_, result.events[index]);
    const esp_err_t error = codex::transport::sendJson(result.responseView());
    if (error != ESP_OK)
        ESP_LOGW(Tag, "RPC response could not be sent: %s",
                 esp_err_to_name(error));
    decoder_.reset();
    return changed;
}

bool CodexRuntime::processReport(
    std::span<const std::uint8_t> report) noexcept
{
    const codex::Result decoded = decoder_.push(report);
    switch (decoded) {
        case codex::Result::Incomplete: return false;
        case codex::Result::Complete: return processCompleteRequest();
        default:
            ESP_LOGW(Tag, "Malformed output report rejected: %d",
                     static_cast<int>(decoded));
            decoder_.reset();
            return false;
    }
}

bool CodexRuntime::processTouch() noexcept
{
    platform::TouchEvent touch;
    const esp_err_t error = platform::pollTouch(touch);
    if (error != ESP_OK) {
        ESP_LOGW(Tag, "Touch read failed: %s", esp_err_to_name(error));
        return false;
    }

    codex::Action action;
    codex::ActionPhase phase{codex::ActionPhase::Press};
    switch (touch.type) {
        case platform::TouchType::Pressed:
            action = codex::press(input_, model_, touch.x, touch.y);
            phase = codex::ActionPhase::Press;
            break;
        case platform::TouchType::Released:
            action = codex::release(input_);
            phase = codex::ActionPhase::Release;
            break;
        case platform::TouchType::Moved:
            action = codex::drag(input_, model_, touch.x, touch.y);
            phase = codex::ActionPhase::Cancel;
            break;
        case platform::TouchType::None:
            return false;
    }
    if (action.type == codex::ActionType::None) return false;
    if (action.type == codex::ActionType::SwitchMode) {
        esp_restart();
        return false;
    }
    (void)codex::handleAction(model_, action, phase, this);
    return true;
}

bool CodexRuntime::animationDue() noexcept
{
    if (model_.page != codex::Page::Agents ||
        codex::overlay(model_) != codex::Overlay::None) return false;
    const bool breathing = std::ranges::any_of(model_.slots, [](const auto &slot) {
        return slot.breathing || slot.status == codex::SlotStatus::Thinking;
    });
    const std::uint32_t now = uptimeMs();
    if (!breathing || now - lastAnimationMs_ < AnimationPeriodMs) return false;
    lastAnimationMs_ = now;
    return true;
}

void CodexRuntime::run() noexcept
{
    ESP_LOGI(Tag, "Codex Buddy %s starting on M5Stack CoreS3",
             codex::FirmwareVersion);
    ESP_LOGI(Tag, "Protocol VID=%04X PID=%04X report=%u body=%u",
             codex::VendorId, codex::ProductId, codex::ReportId,
             static_cast<unsigned>(codex::ReportBodySize));

    codex::init(model_);
    decoder_.reset();
    codex::init(input_);
    if (!queue_.initialize()) {
        ESP_LOGE(Tag, "Could not allocate Codex runtime queue");
        return;
    }
    (void)codex::applyEvent(
        model_, codex::Event{codex::ConnectionChanged{
                    codex::Connection::Connecting}});
    render();
    ESP_ERROR_CHECK(codex::transport::initialize(*this));

    for (;;) {
        Event event;
        bool redraw = false;
        if (queue_.receive(event, pdMS_TO_TICKS(8))) {
            if (event.type == EventType::Connection)
                redraw |= applyConnection(event.connected);
            else
                redraw |= processReport(
                    {event.report.data(), event.reportLength});
        }
        redraw |= processTouch();
        redraw |= animationDue();
        if (redraw) render();
    }
}

}  // namespace buddy::runtime
