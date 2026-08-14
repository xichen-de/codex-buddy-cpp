#include "motion_detector.hpp"

namespace buddy::motion {
namespace {

constexpr float FaceFlatZG = 0.78f;
constexpr float FaceDownMaxSideG = 0.58f;
constexpr std::uint32_t FaceDownHoldMs = 1100U;
constexpr float FaceUpZG = -0.48f;
constexpr float ShakeJerkSquared = 1.20f;
constexpr std::uint32_t ShakeCooldownMs = 1800U;
constexpr float WakeJerkSquared = 0.10f;

}  // namespace

/* Resets all sample history, calibration, hold timers, and debounce state. */
void init(Detector &detector) noexcept
{
    detector = Detector{};
}

/* Applies orientation hysteresis and shake thresholds to one acceleration sample. */
Events update(Detector &detector, float xG, float yG, float zG,
             std::uint32_t nowMs) noexcept
{
    Events events{};

    float jerkSquared = 0.0f;
    if (detector.initialized) {
        const float dx = xG - detector.previousX;
        const float dy = yG - detector.previousY;
        const float dz = zG - detector.previousZ;
        jerkSquared = dx * dx + dy * dy + dz * dz;
    }
    detector.previousX = xG;
    detector.previousY = yG;
    detector.previousZ = zG;

    const float sideSquared = xG * xG + yG * yG;
    if (!detector.orientationCalibrated &&
        (zG > FaceFlatZG || zG < -FaceFlatZG) &&
        sideSquared < FaceDownMaxSideG * FaceDownMaxSideG) {
        /* Claude mode is selected while the screen is visible, so the first
           stable flat pose defines screen-up. This avoids relying on a sensor
           mounting sign that can differ between CoreS3 revisions. */
        detector.faceUpSign = zG >= 0.0f ? 1 : -1;
        detector.orientationCalibrated = true;
        detector.initialized = true;
        return events;
    }
    if (!detector.initialized) {
        detector.initialized = true;
        return events;
    }
    const float orientedZ = zG * detector.faceUpSign;
    const bool faceDownCandidate = detector.orientationCalibrated &&
        orientedZ < -FaceFlatZG && sideSquared < FaceDownMaxSideG * FaceDownMaxSideG;
    if (!detector.isFaceDown) {
        if (faceDownCandidate) {
            if (detector.faceDownSinceMs == 0)
                detector.faceDownSinceMs = nowMs;
            else if (nowMs - detector.faceDownSinceMs >= FaceDownHoldMs) {
                detector.isFaceDown = true;
                detector.faceDownSinceMs = 0;
                events.faceDown = true;
            }
        } else {
            detector.faceDownSinceMs = 0;
        }
    } else if (orientedZ > FaceUpZG ||
               sideSquared > FaceDownMaxSideG * FaceDownMaxSideG) {
        detector.isFaceDown = false;
        events.faceUp = true;
    }

    if (!detector.isFaceDown && !events.faceDown) {
        events.moved = jerkSquared >= WakeJerkSquared;
        if (jerkSquared >= ShakeJerkSquared &&
            (detector.lastShakeMs == 0 ||
             nowMs - detector.lastShakeMs >= ShakeCooldownMs)) {
            detector.lastShakeMs = nowMs;
            events.shake = true;
        }
    }
    return events;
}

}  // namespace buddy::motion
