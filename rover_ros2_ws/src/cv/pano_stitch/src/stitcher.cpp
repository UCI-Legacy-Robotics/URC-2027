#include "stitcher.hpp"

#include <opencv2/stitching.hpp>

namespace pano_stitch {

cv::Rect PanoStitcher::tolerantCrop(const cv::Mat& mask, float minValidFrac) const {
    int rows = mask.rows, cols = mask.cols;

    // Bounding box of all valid pixels
    int bbL = cols, bbR = 0, bbT = rows, bbB = 0;
    for (int r = 0; r < rows; r++)
        for (int c = 0; c < cols; c++)
            if (mask.at<uchar>(r, c)) {
                bbL = std::min(bbL, c); bbR = std::max(bbR, c);
                bbT = std::min(bbT, r); bbB = std::max(bbB, r);
            }

    int bbW = bbR - bbL + 1;

    // Keep rows where >= minValidFrac of the bounding box width is valid
    int top = bbB, bottom = bbT;
    for (int r = bbT; r <= bbB; r++) {
        int valid = 0;
        for (int c = bbL; c <= bbR; c++)
            if (mask.at<uchar>(r, c)) valid++;
        if ((float)valid / bbW >= minValidFrac) {
            top    = std::min(top, r);
            bottom = std::max(bottom, r);
        }
    }

    // Keep columns where >= minValidFrac of the kept rows are valid
    int left = bbR, right = bbL;
    int keptH = bottom - top + 1;
    for (int c = bbL; c <= bbR; c++) {
        int valid = 0;
        for (int r = top; r <= bottom; r++)
            if (mask.at<uchar>(r, c)) valid++;
        if ((float)valid / keptH >= minValidFrac) {
            left  = std::min(left, c);
            right = std::max(right, c);
        }
    }

    return {left, top, right - left + 1, bottom - top + 1};
}

cv::Mat PanoStitcher::stitch(const std::vector<cv::Mat>& images) const {
    if (images.size() < 2) return {};

    auto stitcher = cv::Stitcher::create(cv::Stitcher::PANORAMA);
    cv::Mat pano;
    if (stitcher->stitch(images, pano) != cv::Stitcher::OK) return {};

    cv::Mat gray, mask;
    cv::cvtColor(pano, gray, cv::COLOR_BGR2GRAY);
    cv::threshold(gray, mask, 1, 255, cv::THRESH_BINARY);

    cv::Rect crop = tolerantCrop(mask);
    return pano(crop).clone();
}

}  // namespace pano_stitch
