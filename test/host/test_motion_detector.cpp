#include <cassert>
#include <cstdio>

#include "motion_detector.hpp"

using namespace buddy::motion;

static void test_face_down_requires_hold(void)
{
    Detector detector;
    init(detector);
    (void)update(detector, 0.0f, 0.0f, 1.0f, 1);
    Events event = update(detector, 0.05f, 0.04f, -0.95f, 100);
    assert(!event.faceDown);
    event = update(detector, 0.05f, 0.04f, -0.95f, 1199);
    assert(!event.faceDown);
    event = update(detector, 0.05f, 0.04f, -0.95f, 1200);
    assert(event.faceDown && detector.isFaceDown);
    event = update(detector, 0.0f, 0.0f, 0.2f, 1250);
    assert(event.faceUp && !detector.isFaceDown);
}

static void test_tilt_does_not_count_as_face_down(void)
{
    Detector detector;
    init(detector);
    (void)update(detector, 0.0f, 0.0f, 1.0f, 1);
    (void)update(detector, 0.8f, 0.0f, -0.8f, 100);
    const Events event = update(detector, 0.8f, 0.0f, -0.8f, 1500);
    assert(!event.faceDown);
}

static void test_opposite_mounting_sign_calibrates(void)
{
    Detector detector;
    init(detector);
    (void)update(detector, 0.0f, 0.0f, -1.0f, 1);
    (void)update(detector, 0.0f, 0.0f, 1.0f, 100);
    const Events event = update(detector, 0.0f, 0.0f, 1.0f, 1200);
    assert(event.faceDown);
}

static void test_shake_and_cooldown(void)
{
    Detector detector;
    init(detector);
    (void)update(detector, 0.0f, 0.0f, 1.0f, 1);
    Events event = update(detector, 1.4f, 0.0f, 0.0f, 100);
    assert(event.shake && event.moved);
    event = update(detector, -1.4f, 0.0f, 0.0f, 200);
    assert(!event.shake && event.moved);
    (void)update(detector, 0.0f, 0.0f, 1.0f, 1800);
    event = update(detector, 1.4f, 0.0f, 0.0f, 2000);
    assert(event.shake);
}

int main(void)
{
    test_face_down_requires_hold();
    test_tilt_does_not_count_as_face_down();
    test_opposite_mounting_sign_calibrates();
    test_shake_and_cooldown();
    puts("motion_detector tests passed");
    return 0;
}
