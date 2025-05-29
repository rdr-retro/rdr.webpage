#include "tree_lib.hpp"
#include <opencv2/opencv.hpp>
#include <random>
#include <sstream>

namespace tree_lib {

Generator::Generator() {}
Generator::~Generator() {}

std::string Generator::generate() {
    cv::VideoCapture cap(0);
    if (!cap.isOpened()) return "";

    cv::Mat frame;
    cap >> frame;
    if (frame.empty()) return "";

    uint64_t entropy = frameEntropy(frame);
    std::mt19937_64 rng(entropy);

    lastDigits = randomDigits1024(rng);
    return lastDigits;
}

std::string Generator::getLast() const {
    return lastDigits;
}

uint64_t Generator::frameEntropy(const cv::Mat& frame) const {
    uint64_t sum = 0;
    for (int y = 0; y < frame.rows; ++y) {
        for (int x = 0; x < frame.cols; ++x) {
            cv::Vec3b pixel = frame.at<cv::Vec3b>(y, x);
            sum += pixel[0] + pixel[1] + pixel[2];
        }
    }
    return sum;
}

std::string Generator::randomDigits1024(std::mt19937_64& rng) {
    std::ostringstream oss;
    std::uniform_int_distribution<int> dist(0, 9);
    for (int i = 0; i < 1024; ++i) {
        oss << dist(rng);
    }
    return oss.str();
}

bool hayCamaraDisponible() {
    cv::VideoCapture cap(0);
    return cap.isOpened();
}

} // namespace tree_lib
