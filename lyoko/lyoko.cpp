// keyboard_utf8_replay_clean.cpp
//
// Lee un fichero UTF-8 y, al pulsar cualquier tecla física, inyecta
// el siguiente carácter del fichero en la ventana con foco. Cada '\n'
// en el texto se traduce en una pulsación de ENTER, de modo que los
// saltos de línea quedan exactamente replicados.
//
// Compilar:
//   g++ keyboard_utf8_replay_clean.cpp -o keyboard_utf8_replay \
//       -lX11 -lXtst -lpthread
//
// Ejecutar (como root):
//   sudo ./keyboard_utf8_replay archivo.txt
//
// Precaución: bloquea el teclado físico mientras que corre.

#include <fcntl.h>
#include <unistd.h>
#include <linux/input.h>
#include <sys/ioctl.h>
#include <errno.h>
#include <string.h>

#include <X11/Xlib.h>
#include <X11/extensions/XTest.h>
#include <X11/keysym.h>
#include <X11/Xutil.h>

#include <iostream>
#include <fstream>
#include <string>
#include <thread>
#include <atomic>
#include <mutex>
#include <functional>
#include <memory>
#include <chrono>

// -----------------------------------------------------------------------------
// Clase que bloquea el teclado físico (EVIOCGRAB) y dispara un callback
// cada vez que se detecta un key press real. Antes de comenzar a escuchar,
// vacía cualquier evento residual para evitar pulsaciones fantasma.
// -----------------------------------------------------------------------------
class KeyboardBlocker {
public:
    // Callback que se invoca cuando el usuario presiona una tecla física
    std::function<void()> onKeyPressed;

    KeyboardBlocker(const std::string& devPath)
        : devPath(devPath), fd(-1), running(false) {}

    ~KeyboardBlocker() {
        stop();
    }

    bool start() {
        fd = open(devPath.c_str(), O_RDONLY | O_NONBLOCK);
        if (fd < 0) {
            std::cerr << "Error abriendo dispositivo " << devPath
                      << ": " << strerror(errno) << "\n";
            return false;
        }
        // Tomar posesión exclusiva del teclado
        if (ioctl(fd, EVIOCGRAB, 1) < 0) {
            std::cerr << "Error bloqueando teclado con EVIOCGRAB: "
                      << strerror(errno) << "\n";
            close(fd);
            return false;
        }

        // "Drain": descartar cualquier evento que esté en el buffer
        // para que no se produzcan pulsaciones falsas al arrancar.
        struct input_event ev;
        ssize_t rd;
        while ((rd = read(fd, &ev, sizeof(ev))) == sizeof(ev)) {
            // Descartamos sin hacer nada
        }
        // Si rd < 0 y errno == EAGAIN, significa que no hay más eventos.

        running = true;
        threadHandle = std::thread(&KeyboardBlocker::loop, this);
        return true;
    }

    void stop() {
        running = false;
        if (threadHandle.joinable())
            threadHandle.join();
        if (fd >= 0) {
            ioctl(fd, EVIOCGRAB, 0);
            close(fd);
            fd = -1;
        }
    }

private:
    std::string devPath;
    int fd;
    std::atomic<bool> running;
    std::thread threadHandle;

    void loop() {
        struct input_event ev;
        ssize_t n;
        while (running) {
            n = read(fd, &ev, sizeof(ev));
            if (n == sizeof(ev)) {
                // Solo reaccionar a key press (ev.value == 1)
                if (ev.type == EV_KEY && ev.value == 1) {
                    if (onKeyPressed)
                        onKeyPressed();
                }
            } else {
                // No hay evento, dormir un poco para no saturar CPU
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
            }
        }
    }
};

// -----------------------------------------------------------------------------
// next_utf8_char: obtiene el siguiente carácter UTF-8 completo desde la posición
// 'pos' dentro de 'text'. Devuelve true si pudo extraerlo, y deja en 'outChar'
// el byte(s) correspondientes. Avanza 'pos' en consecuencia.
// -----------------------------------------------------------------------------
bool next_utf8_char(const std::string& text, size_t& pos, std::string& outChar) {
    if (pos >= text.size())
        return false;

    unsigned char c = static_cast<unsigned char>(text[pos]);
    size_t char_len = 1;

    if ((c & 0x80) == 0x00) {
        // ASCII (1 byte)
        char_len = 1;
    }
    else if ((c & 0xE0) == 0xC0) {
        // 2 bytes
        char_len = 2;
    }
    else if ((c & 0xF0) == 0xE0) {
        // 3 bytes
        char_len = 3;
    }
    else if ((c & 0xF8) == 0xF0) {
        // 4 bytes
        char_len = 4;
    }
    else {
        // Byte inválido en inicio de secuencia UTF-8
        return false;
    }

    // Comprobar que no nos pasamos del final
    if (pos + char_len > text.size())
        return false;

    // Verificar que cada byte de continuación empiece con 0b10xxxxxx
    for (size_t i = 1; i < char_len; ++i) {
        unsigned char nc = static_cast<unsigned char>(text[pos + i]);
        if ((nc & 0xC0) != 0x80) {
            return false;
        }
    }

    outChar = text.substr(pos, char_len);
    pos += char_len;
    return true;
}

// -----------------------------------------------------------------------------
// send_utf8_string: envía cada byte de 'utf8str' al foco activo en X11.
// Si el byte es '\n', genera una pulsación de ENTER (XK_Return).
// Para ASCII, dead keys y acentos españoles, traduce a KeySym/KeyCode.
// -----------------------------------------------------------------------------
void send_utf8_string(Display* display, const std::string& utf8str) {
    for (size_t i = 0; i < utf8str.size(); ++i) {
        unsigned char c = static_cast<unsigned char>(utf8str[i]);

        // Caso especial: replicar saltos de línea exactamente
        if (c == '\n') {
            KeySym enterSym = XK_Return;
            KeyCode enterCode = XKeysymToKeycode(display, enterSym);
            if (enterCode != 0) {
                // Presionar ENTER
                XTestFakeKeyEvent(display, enterCode, True, 0);
                // Soltar ENTER
                XTestFakeKeyEvent(display, enterCode, False, 0);
                XFlush(display);
                std::this_thread::sleep_for(std::chrono::milliseconds(15));
            }
            continue;
        }

        KeySym ks = 0;
        bool shift = false;
        bool dead_key = false;
        KeySym dead_sym = 0;

        // ASCII básico (a–z, A–Z, 0–9)
        if (c >= 'a' && c <= 'z') {
            ks = XStringToKeysym(std::string(1, c).c_str());
        }
        else if (c >= 'A' && c <= 'Z') {
            ks = XStringToKeysym(std::string(1, c).c_str());
            shift = true;
        }
        else if (c >= '0' && c <= '9') {
            ks = XStringToKeysym(std::string(1, c).c_str());
        }
        else {
            switch (c) {
                case ' ': ks = XK_space; break;
                case '.': ks = XK_period; break;
                case ',': ks = XK_comma; break;
                case ';': ks = XK_semicolon; break;
                case ':': ks = XK_colon; shift = true; break;
                case '!': ks = XK_1; shift = true; break;
                case '?': ks = XK_slash; shift = true; break;
                case '-': ks = XK_minus; break;
                case '_': ks = XK_minus; shift = true; break;
                case '+': ks = XK_plus; shift = true; break;
                case '=': ks = XK_equal; break;
                case '(': ks = XK_9; shift = true; break;
                case ')': ks = XK_0; shift = true; break;
                case '\'': ks = XK_apostrophe; break;
                case '\"': ks = XK_quotedbl; shift = true; break;
                case '/': ks = XK_slash; break;
                case '\\': ks = XK_backslash; break;
                case '#': ks = XK_3; shift = true; break;
                case '$': ks = XK_4; shift = true; break;
                case '%': ks = XK_5; shift = true; break;
                case '&': ks = XK_7; shift = true; break;
                case '*': ks = XK_8; shift = true; break;
                case '@': ks = XK_2; shift = true; break;
                case '^': ks = XK_6; shift = true; break;
                case '~': ks = XK_grave; shift = true; break;
                case '`': ks = XK_grave; break;
                default: {
                    // No es ASCII simple: comprobar si es multibyte UTF-8 (acentos, eñes...)
                    size_t start = i;
                    std::string seq;
                    if (!next_utf8_char(utf8str, start, seq)) {
                        // Secuencia inválida, ignorar este byte
                        break;
                    }
                    uint32_t codepoint = 0;
                    if (seq.size() == 2) {
                        codepoint = ((static_cast<uint32_t>(static_cast<unsigned char>(seq[0])) & 0x1F) << 6)
                                    | (static_cast<uint32_t>(static_cast<unsigned char>(seq[1])) & 0x3F);
                    } else if (seq.size() == 3) {
                        codepoint = ((static_cast<uint32_t>(static_cast<unsigned char>(seq[0])) & 0x0F) << 12)
                                    | ((static_cast<uint32_t>(static_cast<unsigned char>(seq[1])) & 0x3F) << 6)
                                    | (static_cast<uint32_t>(static_cast<unsigned char>(seq[2])) & 0x3F);
                    } else if (seq.size() == 4) {
                        codepoint = ((static_cast<uint32_t>(static_cast<unsigned char>(seq[0])) & 0x07) << 18)
                                    | ((static_cast<uint32_t>(static_cast<unsigned char>(seq[1])) & 0x3F) << 12)
                                    | ((static_cast<uint32_t>(static_cast<unsigned char>(seq[2])) & 0x3F) << 6)
                                    | (static_cast<uint32_t>(static_cast<unsigned char>(seq[3])) & 0x3F);
                    }
                    // Mapear acentos y ñ en español usando dead keys
                    switch (codepoint) {
                        case 0xC3A1: // á
                            dead_key = true;
                            dead_sym = XK_dead_acute;
                            ks = XK_a;
                            break;
                        case 0xC3A9: // é
                            dead_key = true;
                            dead_sym = XK_dead_acute;
                            ks = XK_e;
                            break;
                        case 0xC3AD: // í
                            dead_key = true;
                            dead_sym = XK_dead_acute;
                            ks = XK_i;
                            break;
                        case 0xC3B3: // ó
                            dead_key = true;
                            dead_sym = XK_dead_acute;
                            ks = XK_o;
                            break;
                        case 0xC3BA: // ú
                            dead_key = true;
                            dead_sym = XK_dead_acute;
                            ks = XK_u;
                            break;
                        case 0xC3B1: // ñ
                            dead_key = true;
                            dead_sym = XK_dead_tilde;
                            ks = XK_n;
                            break;
                        case 0xC391: // Ñ
                            dead_key = true;
                            dead_sym = XK_dead_tilde;
                            ks = XK_N;
                            shift = true;
                            break;
                        case 0xC3BC: // ü
                            dead_key = true;
                            dead_sym = XK_dead_diaeresis;
                            ks = XK_u;
                            break;
                        default:
                            // No soportado, ignoramos
                            break;
                    }
                    // Ajustamos i para no procesar bytes sueltos dentro de seq
                    i = start - 1;
                } break;
            }
        }

        // Si hay una dead key (acento, tilde, diéresis), enviarla primero
        if (dead_key && dead_sym != 0) {
            KeyCode dk = XKeysymToKeycode(display, dead_sym);
            if (dk != 0) {
                XTestFakeKeyEvent(display, dk, True, 0);
                XTestFakeKeyEvent(display, dk, False, 0);
                XFlush(display);
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }
        }

        if (ks == 0) {
            // No hay KeySym válido para este byte, continuar
            continue;
        }

        KeyCode kc = XKeysymToKeycode(display, ks);
        if (kc == 0) {
            continue;
        }

        if (shift) {
            KeyCode shift_kc = XKeysymToKeycode(display, XK_Shift_L);
            if (shift_kc != 0) {
                XTestFakeKeyEvent(display, shift_kc, True, 0);
            }
        }
        XTestFakeKeyEvent(display, kc, True, 0);
        XTestFakeKeyEvent(display, kc, False, 0);
        if (shift) {
            KeyCode shift_kc = XKeysymToKeycode(display, XK_Shift_L);
            if (shift_kc != 0) {
                XTestFakeKeyEvent(display, shift_kc, False, 0);
            }
        }

        XFlush(display);
        std::this_thread::sleep_for(std::chrono::milliseconds(15));
    }
}

// -----------------------------------------------------------------------------
// UTF8Replayer: gestiona el estado del texto en memoria y, ante cada pulsación
// física, envía el siguiente carácter completo (incluyendo '\n').
// -----------------------------------------------------------------------------
class UTF8Replayer {
public:
    UTF8Replayer(Display* dpy, const std::string& texto)
        : display(dpy), contenido(texto), pos(0) {}

    void onKey() {
        std::lock_guard<std::mutex> lock(mtx);
        if (pos >= contenido.size()) {
            std::cout << "[Fin de archivo]\n";
            running = false;
            return;
        }
        std::string utf8char;
        if (next_utf8_char(contenido, pos, utf8char)) {
            send_utf8_string(display, utf8char);
        }
    }

    void startBlocking(const std::string& devPath) {
        blocker = std::make_unique<KeyboardBlocker>(devPath);
        blocker->onKeyPressed = [this]() { this->onKey(); };
        if (!blocker->start()) {
            throw std::runtime_error("No se pudo iniciar bloqueo del teclado.");
        }
    }

    void stopBlocking() {
        if (blocker) {
            blocker->stop();
        }
    }

    bool isRunning() const {
        return running;
    }

private:
    Display* display;
    std::string contenido;
    size_t pos;
    std::mutex mtx;
    std::unique_ptr<KeyboardBlocker> blocker;
    std::atomic<bool> running{true};
};

// -----------------------------------------------------------------------------
// Función principal: abre el archivo UTF-8, arranca el bloqueo del teclado
// y espera a que el usuario presione teclas para inyectar carácter a carácter.
// -----------------------------------------------------------------------------
int main(int argc, char* argv[]) {
    std::cout << "=== Keyboard UTF-8 Replay: Inicio ===\n";
    if (argc < 2) {
        std::cerr << "Uso: sudo " << argv[0]
                  << " archivo_utf8.txt\n";
        return 1;
    }

    // Leer todo el contenido del archivo en memoria (modo binario)
    std::ifstream archivo(argv[1], std::ios::binary);
    if (!archivo) {
        std::cerr << "Error abriendo archivo " << argv[1] << "\n";
        return 1;
    }
    std::string contenido((std::istreambuf_iterator<char>(archivo)),
                          std::istreambuf_iterator<char>());
    archivo.close();

    // Conectar al servidor X
    Display* display = XOpenDisplay(NULL);
    if (!display) {
        std::cerr << "No se pudo abrir display X.\n";
        return 1;
    }

    try {
        UTF8Replayer replayer(display, contenido);
        // Ajusta según tu sistema: /dev/input/event2, event3, etc.
        const std::string devInput = "/dev/input/event2";
        replayer.startBlocking(devInput);

        std::cout << "Presiona cualquier tecla para escribir el texto UTF-8; "
                  << "Ctrl+C para salir.\n";

        // Esperar hasta que se termine de reproducir
        while (replayer.isRunning()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }

        replayer.stopBlocking();
        XCloseDisplay(display);

        std::cout << "Programa terminado.\n";
        return 0;
    }
    catch (const std::exception& ex) {
        std::cerr << "Excepción: " << ex.what() << "\n";
        if (display) {
            XCloseDisplay(display);
        }
        return 1;
    }
}


