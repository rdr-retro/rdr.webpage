#include "tree_lib.hpp"

namespace tree_lib {

Generator::Generator()
    : running(false), paused(false), keyLength(1024), generationIntervalMs(1000), cap(0)
{
    if (!cap.isOpened()) {
        // No lanza error, solo estado interno
    }
}

Generator::~Generator() {
    stopGeneration();
    if (cap.isOpened()) cap.release();
}

bool Generator::hayCamaraDisponible() {
    std::lock_guard<std::mutex> lock(cameraMutex);
    if (!cap.isOpened()) {
        cap.open(0);
        if (!cap.isOpened()) return false;
    }
    return true;
}

sf::Image Generator::cvMatToSfImage(const cv::Mat& mat) {
    sf::Image img;
    if (mat.empty()) return img;

    cv::Mat matRGBA;
    if (mat.channels() == 3) {
        cv::cvtColor(mat, matRGBA, cv::COLOR_BGR2RGBA);
    } else if (mat.channels() == 4) {
        cv::cvtColor(mat, matRGBA, cv::COLOR_BGRA2RGBA);
    } else {
        return img;
    }

    img.create(matRGBA.cols, matRGBA.rows, matRGBA.data);
    return img;
}

sf::Image Generator::captureImageFromCamera() {
    std::lock_guard<std::mutex> lock(cameraMutex);
    if (!cap.isOpened()) cap.open(0);
    cv::Mat frame;
    if (!cap.isOpened() || !cap.read(frame) || frame.empty()) {
        return sf::Image(); // Imagen vacía
    }
    return cvMatToSfImage(frame);
}

std::string Generator::generateKeyFromImage(const sf::Image& image, size_t length) {
    unsigned int width = image.getSize().x;
    unsigned int height = image.getSize().y;
    const int regionSize = 10;
    unsigned int centerX = width / 2;
    unsigned int centerY = height / 2;

    std::vector<uint32_t> seeds;
    for (int y = -regionSize / 2; y < regionSize / 2; ++y) {
        for (int x = -regionSize / 2; x < regionSize / 2; ++x) {
            sf::Color pixel = image.getPixel(centerX + x, centerY + y);
            uint32_t value = (pixel.r << 16) | (pixel.g << 8) | pixel.b;
            seeds.push_back(value);
        }
    }

    std::seed_seq seed(seeds.begin(), seeds.end());
    std::mt19937 rng(seed);

    const std::string chars = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789";
    std::uniform_int_distribution<> dist(0, static_cast<int>(chars.size()) - 1);

    std::string result;
    result.reserve(length);
    for (size_t i = 0; i < length; ++i) {
        result += chars[dist(rng)];
    }
    return result;
}

std::string Generator::generateSingleKey(const sf::Image& image, size_t length) {
    return generateKeyFromImage(image, length);
}

void Generator::startIndefiniteGeneration(std::function<void(const std::string&)> callback, size_t length, unsigned int intervalMs) {
    if (running.load()) return; // Ya corriendo
    callbackFunc = callback;
    keyLength = length;
    generationIntervalMs = intervalMs;
    running.store(true);
    paused.store(false);
    generationThread = std::thread(&Generator::generationThreadFunc, this);
}

void Generator::pauseGeneration() {
    paused.store(true);
}

void Generator::resumeGeneration() {
    paused.store(false);
    cv.notify_all();
}

void Generator::stopGeneration() {
    running.store(false);
    cv.notify_all();
    if (generationThread.joinable()) generationThread.join();
}

void Generator::generationThreadFunc() {
    while (running.load()) {
        {
            std::unique_lock<std::mutex> lock(mtx);
            cv.wait(lock, [this]() { return !paused.load() || !running.load(); });
            if (!running.load()) break;
        }

        sf::Image img = captureImageFromCamera();
        if (img.getSize().x == 0 || img.getSize().y == 0) {
            std::this_thread::sleep_for(std::chrono::milliseconds(generationIntervalMs));
            continue;
        }

        std::string key = generateKeyFromImage(img, keyLength);
        if (callbackFunc) callbackFunc(key);

        std::this_thread::sleep_for(std::chrono::milliseconds(generationIntervalMs));
    }
}

std::string Generator::cifrarXOR(const std::string& texto, const std::string& clave) {
    std::string resultado = texto;
    size_t keyLen = clave.size();
    for (size_t i = 0; i < texto.size(); ++i) {
        resultado[i] ^= clave[i % keyLen];
    }
    return resultado;
}

bool Generator::isGenerating() const {
    return running.load();
}

bool Generator::isPaused() const {
    return paused.load();
}

void Generator::setKeyLength(size_t length) {
    keyLength = length;
}

void Generator::setGenerationInterval(unsigned int intervalMs) {
    generationIntervalMs = intervalMs;
}

} // namespace tree_lib
