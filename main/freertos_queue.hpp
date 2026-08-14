#pragma once

#include <cstddef>
#include <type_traits>

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

namespace buddy::runtime {

template <typename Event, std::size_t Capacity>
class Queue final {
    static_assert(std::is_trivially_copyable_v<Event>,
                  "FreeRTOS queues copy events byte-for-byte");

public:
    Queue() = default;
    ~Queue()
    {
        if (handle_ != nullptr) vQueueDelete(handle_);
    }

    Queue(const Queue &) = delete;
    Queue &operator=(const Queue &) = delete;

    [[nodiscard]] bool initialize() noexcept
    {
        if (handle_ != nullptr) return false;
        handle_ = xQueueCreate(Capacity, sizeof(Event));
        return handle_ != nullptr;
    }

    [[nodiscard]] bool send(const Event &event,
                            TickType_t wait = 0) noexcept
    {
        return handle_ != nullptr && xQueueSend(handle_, &event, wait) == pdTRUE;
    }

    [[nodiscard]] bool receive(Event &event, TickType_t wait) noexcept
    {
        return handle_ != nullptr &&
               xQueueReceive(handle_, &event, wait) == pdTRUE;
    }

private:
    QueueHandle_t handle_{};
};

}  // namespace buddy::runtime
