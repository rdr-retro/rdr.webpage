#ifndef TREE_LIB_HPP
#define TREE_LIB_HPP

#include <SFML/Graphics.hpp>
#include <opencv2/opencv.hpp>
#include <string>
#include <vector>
#include <random>
#include <thread>
#include <mutex>
#include <atomic>
#include <condition_variable>
#include <chrono>
#include <functional>
#include <stdexcept>

namespace tree_lib {

class Generator {
public:
    // 1. Constructor: inicializa variables y abre cámara si es posible
    Generator();

    // 2. Destructor: detiene hilo y libera cámara
    ~Generator();

    // 3. Comprueba si la cámara está disponible
    bool hayCamaraDisponible();

    // 4. Convierte cv::Mat a sf::Image
    sf::Image cvMatToSfImage(const cv::Mat& mat);

    // 5. Captura imagen desde la cámara y la devuelve como sf::Image
    sf::Image captureImageFromCamera();

    // 6. Genera clave aleatoria a partir de una imagen SFML
    std::string generateKeyFromImage(const sf::Image& image, size_t length = 1024);

    // 7. Genera una clave única (modo pausado)
    std::string generateSingleKey(const sf::Image& image, size_t length = 1024);

    // 8. Inicia generación indefinida de claves (modo background)
    void startIndefiniteGeneration(std::function<void(const std::string&)> callback, size_t length = 1024, unsigned int intervalMs = 1000);

    // 9. Pausa la generación indefinida
    void pauseGeneration();

    // 10. Reanuda la generación indefinida
    void resumeGeneration();

    // 11. Detiene la generación indefinida y espera a que termine
    void stopGeneration();

    // 12. Cifra/descifra un texto con XOR usando la clave
    std::string cifrarXOR(const std::string& texto, const std::string& clave);

    // 13. (Extra) Obtiene el estado de la generación indefinida
    bool isGenerating() const;

    // 14. (Extra) Obtiene el estado de pausa
    bool isPaused() const;

    // 15. (Extra) Cambia longitud de la clave indefinida
    void setKeyLength(size_t length);

    // 16. (Extra) Cambia intervalo de generación indefinida
    void setGenerationInterval(unsigned int intervalMs);

private:
    // Función interna para el hilo de generación indefinida
    void generationThreadFunc();

    std::thread generationThread;
    std::atomic<bool> running;
    std::atomic<bool> paused;
    std::mutex mtx;
    std::condition_variable cv;
    std::function<void(const std::string&)> callbackFunc;
    size_t keyLength;
    unsigned int generationIntervalMs;
    std::mutex cameraMutex;
    cv::VideoCapture cap;
};

} // namespace tree_lib

#endif // TREE_LIB_HPP
