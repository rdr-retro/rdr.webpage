#ifndef TREE_LIB_HPP
#define TREE_LIB_HPP

#include <string>
#include <opencv2/opencv.hpp>
#include <random>

namespace tree_lib {

class Generator {
public:
    Generator();
    ~Generator();

    std::string generate();
    std::string getLast() const;

private:
    uint64_t frameEntropy(const cv::Mat& frame) const;
    std::string randomDigits1024(std::mt19937_64& rng);

    std::string lastDigits;
};

// nueva duncion
bool hayCamaraDisponible();

} // namespace tree_lib

#endif // TREE_LIB_HPP
