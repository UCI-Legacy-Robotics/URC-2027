#pragma once

#include <opencv2/opencv.hpp>
#include <vector>

namespace pano_stitch {

class PanoStitcher {
public:
    cv::Mat stitch(const std::vector<cv::Mat>& images) const;

private:
    cv::Rect tolerantCrop(const cv::Mat& mask, float minValidFrac = 0.90f) const;
};

}  // namespace pano_stitch
