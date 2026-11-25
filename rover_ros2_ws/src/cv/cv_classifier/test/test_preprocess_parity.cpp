//checks the C++ preprocessing (cv_gesture/cv_gesture/preprocess.cpp) matches preprocess.py.
//cases come from cv_gesture/test/gen_parity_cases.py, regenerate them if preprocess.py changes
#include <gtest/gtest.h>

#include <fstream>
#include <string>

#include "preprocess.hpp"

using namespace cv_gesture;

constexpr float TOL = 1e-5f;

class Parity : public ::testing::Test {
protected:
    void SetUp() override {
        in_.open(PARITY_FILE);
        ASSERT_TRUE(in_.is_open()) << "missing " << PARITY_FILE << " (run cv_gesture/test/gen_parity_cases.py)";
    }

    void expect_section(const std::string &name, int &count) {
        std::string tag;
        in_ >> tag >> count;
        ASSERT_EQ(tag, name);
    }

    template <typename A>
    void read(A &arr) {
        for (auto &v : arr) in_ >> v;
    }

    std::ifstream in_;
};

TEST_F(Parity, NormalizeAndResampleMatchPython) {
    int count = 0;
    expect_section("NORMALIZE", count);
    ASSERT_GT(count, 0);
    for (int c = 0; c < count; c++) {
        int detected;
        Keypoints17 x, y, conf;
        Frame expected;
        in_ >> detected;
        read(x); read(y); read(conf); read(expected);
        const Frame got = normalize_frame(x, y, conf, detected != 0);
        for (int i = 0; i < NUM_FEATURES; i++) {
            ASSERT_NEAR(got[i], expected[i], TOL) << "normalize case " << c << " feature " << i;
        }
    }

    expect_section("RESAMPLE", count);
    ASSERT_GT(count, 0);
    int full = 0;
    for (int c = 0; c < count; c++) {
        int n, has_end, ok;
        double end;
        in_ >> n >> has_end >> end >> ok;
        std::deque<double> times(n);
        std::deque<Frame> frames(n);
        for (auto &t : times) in_ >> t;
        for (auto &f : frames) read(f);

        const auto got = resample_window(times, frames, has_end ? std::optional<double>(end) : std::nullopt);
        ASSERT_EQ(got.has_value(), ok != 0) << "resample case " << c << " availability";
        if (!ok) continue;
        full++;
        Window expected;
        read(expected);
        for (size_t i = 0; i < expected.size(); i++) {
            ASSERT_NEAR((*got)[i], expected[i], TOL) << "resample case " << c << " value " << i;
        }
    }
    EXPECT_GT(full, 0) << "no full-window resample cases were tested";
}
