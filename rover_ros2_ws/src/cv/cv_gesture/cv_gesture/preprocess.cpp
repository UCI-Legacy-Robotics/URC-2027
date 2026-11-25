//C++ port of preprocess.py, see preprocess.hpp
#include "preprocess.hpp"

#include <algorithm>
#include <cmath>
#include <vector>

namespace cv_gesture {

Frame normalize_frame(const Keypoints17 &x, const Keypoints17 &y, const Keypoints17 &conf, bool detected) {
    Frame out{};  // all zeros
    if (!detected) return out;
    if (std::min(conf[L_SHOULDER], conf[R_SHOULDER]) < KP_CONF_MIN) return out;

    const float cx = (x[L_SHOULDER] + x[R_SHOULDER]) / 2.0f;
    const float cy = (y[L_SHOULDER] + y[R_SHOULDER]) / 2.0f;
    const float shoulder_w = std::hypot(x[L_SHOULDER] - x[R_SHOULDER], y[L_SHOULDER] - y[R_SHOULDER]);

    float scale;
    if (std::min(conf[L_HIP], conf[R_HIP]) >= KP_CONF_MIN) {
        // torso length doesn't shrink when you turn, unlike shoulder width
        const float hx = (x[L_HIP] + x[R_HIP]) / 2.0f;
        const float hy = (y[L_HIP] + y[R_HIP]) / 2.0f;
        scale = std::hypot(hx - cx, hy - cy);
    } else {
        // hips out of frame (close up): estimate torso length from shoulders
        scale = shoulder_w * TORSO_PER_SHOULDER;
    }
    if (scale < 1e-3f) return out;

    for (int k = 0; k < NUM_KP; k++) {
        if (!USED_KP[k]) continue;  // face + legs stay zero
        const bool valid = conf[k] >= KP_CONF_MIN;
        out[k * 3 + 0] = valid ? std::clamp((x[k] - cx) / scale, -CLIP, CLIP) : 0.0f;
        out[k * 3 + 1] = valid ? std::clamp((y[k] - cy) / scale, -CLIP, CLIP) : 0.0f;
        out[k * 3 + 2] = conf[k];
    }
    return out;
}

//same as numpy.interp(x, xp, fp) for increasing xp (clamps outside the range)
static double interp(double x, const std::vector<double> &xp, const std::vector<double> &fp) {
    const size_t n = xp.size();
    if (x >= xp[n - 1]) return fp[n - 1];
    if (x < xp[0]) return fp[0];
    const size_t j = static_cast<size_t>(std::upper_bound(xp.begin(), xp.end(), x) - xp.begin()) - 1;
    if (xp[j] == x) return fp[j];
    const double slope = (fp[j + 1] - fp[j]) / (xp[j + 1] - xp[j]);
    return slope * (x - xp[j]) + fp[j];
}

std::optional<Window> resample_window(const std::deque<double> &times, const std::deque<Frame> &frames,
                                      std::optional<double> end_time) {
    const size_t n = times.size();
    if (n < 2) return std::nullopt;
    const double end = end_time.value_or(times.back());
    const double start = end - WINDOW_SEC;
    if (times.front() > start + 1e-6) return std::nullopt;  // not enough history yet

    //numpy.linspace(start, end, WINDOW_LEN)
    std::array<double, WINDOW_LEN> grid;
    const double step = (end - start) / (WINDOW_LEN - 1);
    for (int g = 0; g < WINDOW_LEN; g++) grid[g] = g * step + start;
    grid[WINDOW_LEN - 1] = end;

    const std::vector<double> xp(times.begin(), times.end());
    std::vector<double> fp(n);
    Window out;
    for (int j = 0; j < NUM_FEATURES; j++) {
        for (size_t i = 0; i < n; i++) fp[i] = frames[i][j];
        for (int g = 0; g < WINDOW_LEN; g++) out[g * NUM_FEATURES + j] = static_cast<float>(interp(grid[g], xp, fp));
    }
    return out;
}

bool frame_valid(const Frame &frame) {
    return std::any_of(frame.begin(), frame.end(), [](float v) { return v != 0.0f; });
}

}  // namespace cv_gesture
