//C++ port of preprocess.py (same folder), used by the cv_classifier node.
//the model was trained on data processed by preprocess.py, so this MUST give the same numbers.
//cv_classifier/test/test_preprocess_parity.cpp checks that. if preprocess.py changes,
//change this to match and regenerate the cases with cv_gesture/test/gen_parity_cases.py
#pragma once

#include <array>
#include <deque>
#include <optional>

namespace cv_gesture {

constexpr double TARGET_HZ  = 15.0;
constexpr double WINDOW_SEC = 2.0;
constexpr int WINDOW_LEN    = static_cast<int>(TARGET_HZ * WINDOW_SEC);  // 30 frames
constexpr int NUM_KP        = 17;
constexpr int NUM_FEATURES  = NUM_KP * 3;  // x, y, confidence <--per keypoint

constexpr int L_SHOULDER = 5, R_SHOULDER = 6;
constexpr int L_HIP = 11, R_HIP = 12;
constexpr float KP_CONF_MIN        = 0.3f;
constexpr float TORSO_PER_SHOULDER = 1.6f;  // typical, use if hips are not visible
constexpr float CLIP               = 5.0f;

//keypoints the model may see = USED_PARTS ('arms', 'torso') in preprocess.py
//face and legs are zeroed so the model can't use framing/distance shortcuts
constexpr std::array<bool, NUM_KP> USED_KP{
    false, false, false, false, false,   // 0-4   face
    true, true, true, true, true, true,  // 5-10  arms (shoulders, elbows, wrists)
    true, true,                          // 11-12 torso (hips)
    false, false, false, false};         // 13-16 legs

using Keypoints17 = std::array<float, NUM_KP>;
using Frame       = std::array<float, NUM_FEATURES>;               // [17][3] flattened: k*3 + {x, y, conf}
using Window      = std::array<float, WINDOW_LEN * NUM_FEATURES>;  // [30][51] flattened

//raw pixel keypoints -> body-relative features, all zeros if no usable person
Frame normalize_frame(const Keypoints17 &x, const Keypoints17 &y, const Keypoints17 &conf, bool detected);

//resample variable-rate frames onto a fixed TARGET_HZ grid covering the WINDOW_SEC ending at
//end_time (default: newest frame). nullopt if there isn't enough history yet
std::optional<Window> resample_window(const std::deque<double> &times, const std::deque<Frame> &frames,
                                      std::optional<double> end_time = std::nullopt);

//true if normalize_frame found a usable person (didn't return all zeros)
bool frame_valid(const Frame &frame);

}  // namespace cv_gesture
