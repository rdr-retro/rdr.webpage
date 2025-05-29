#include <SFML/Graphics.hpp>
#include <SFML/Window.hpp>
#include <opencv2/opencv.hpp>
#include <iostream>
#include <string>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <thread>
#include <future>
#include <vector>
#include <algorithm>
#include <random>
#include "tree_lib.hpp"

namespace fs = std::filesystem;

// --- Utilidades de cifrado XOR ---
std::string cifrarXOR(const std::string& texto, const std::string& clave) {
    std::string resultado = texto;
    for (size_t i = 0; i < texto.size(); ++i)
        resultado[i] ^= clave[i % clave.size()];
    return resultado;
}

// --- Utilidad para listar bóvedas ---
std::vector<std::string> listarUsuarios() {
    std::vector<std::string> usuarios;
    fs::path bovedas("bovedas");
    if (fs::exists(bovedas) && fs::is_directory(bovedas)) {
        for (const auto& entry : fs::directory_iterator(bovedas)) {
            if (entry.is_directory())
                usuarios.push_back(entry.path().filename().string());
        }
    }
    return usuarios;
}

// --- Estados del programa ---
enum class Estado {
    MenuPrincipal,
    Registro,
    RegistroMuestraClave,
    LoginSeleccionUsuario,
    LoginPidePassword,
    Boveda,
    ErrorCamara
};

int main() {
    sf::RenderWindow window(sf::VideoMode(800, 600), L"Raíz - Gestor de Contraseñas");
    sf::Font font;
    if (!font.loadFromFile("Product Sans Regular.ttf")) {
        std::cerr << "Error al cargar la fuente.\n";
        return -1;
    }

    // --- Cargar imágenes de cámara ---
    sf::Texture texturaNoCamara, texturaSiCamara;
    if (!texturaNoCamara.loadFromFile("nocamera.png") || !texturaSiCamara.loadFromFile("sicamera.png")) {
        std::cerr << "Error: no se pudieron cargar las imágenes de cámara.\n";
        return -1;
    }
    sf::Sprite spriteCamara;
    spriteCamara.setScale(0.4f, 0.4f);
    spriteCamara.setPosition(20, 20);

    // --- Elementos gráficos comunes ---
    sf::RectangleShape fondo(sf::Vector2f(800, 600));
    fondo.setFillColor(sf::Color(70, 30, 120));
    sf::Text titulo;
    titulo.setFont(font);
    titulo.setCharacterSize(32);
    titulo.setFillColor(sf::Color(220, 200, 255));
    titulo.setString(L"Raíz - Gestor de Contraseñas");
    titulo.setPosition(60, 20);

    // --- Cámara y generador ---
    tree_lib::Generator gen;
    bool camaraDisponible = false;
    try {
        camaraDisponible = gen.hayCamaraDisponible();
    } catch (...) {
        camaraDisponible = false;
    }
    spriteCamara.setTexture(camaraDisponible ? texturaSiCamara : texturaNoCamara);

    // --- Estados y variables de sesión ---
    Estado estado = camaraDisponible ? Estado::MenuPrincipal : Estado::ErrorCamara;
    std::string mensajeSistema;
    std::string usuarioActual;
    std::string claveActual;
    std::string passwordInput;

    // --- Elementos de registro/login ---
    std::string registroUsuario;
    bool escribiendoRegistroUsuario = false;
    std::string registroMensaje;
    std::string claveGeneradaRegistro;

    // --- Lista de usuarios para login ---
    std::vector<std::string> listaUsuarios;
    int usuarioSeleccionado = 0;
    std::vector<sf::FloatRect> usuariosBounds; // Para clic en login

    // --- Bucle principal ---
    while (window.isOpen()) {
        sf::Event event;
        while (window.pollEvent(event)) {
            if (event.type == sf::Event::Closed)
                window.close();

            // --- Comprobación de cámara en cada paso ---
            if (event.type == sf::Event::KeyPressed && event.key.code == sf::Keyboard::F5) {
                try {
                    camaraDisponible = gen.hayCamaraDisponible();
                } catch (...) {
                    camaraDisponible = false;
                }
                spriteCamara.setTexture(camaraDisponible ? texturaSiCamara : texturaNoCamara);
                if (!camaraDisponible)
                    estado = Estado::ErrorCamara;
                else if (estado == Estado::ErrorCamara)
                    estado = Estado::MenuPrincipal;
            }

            // --- Menú principal ---
            if (estado == Estado::MenuPrincipal && event.type == sf::Event::MouseButtonPressed) {
                sf::Vector2f mouse = (sf::Vector2f)sf::Mouse::getPosition(window);
                // Botón registro
                if (mouse.x > 250 && mouse.x < 550 && mouse.y > 220 && mouse.y < 270)
                    estado = Estado::Registro;
                // Botón login
                if (mouse.x > 250 && mouse.x < 550 && mouse.y > 300 && mouse.y < 350) {
                    listaUsuarios = listarUsuarios();
                    usuarioSeleccionado = 0;
                    estado = Estado::LoginSeleccionUsuario;
                }
            }

            // --- Registro ---
            if (estado == Estado::Registro) {
                if (event.type == sf::Event::MouseButtonPressed) {
                    sf::Vector2f mouse = (sf::Vector2f)sf::Mouse::getPosition(window);
                    escribiendoRegistroUsuario = (mouse.x > 250 && mouse.x < 550 && mouse.y > 160 && mouse.y < 200);
                    // Botón crear
                    if (mouse.x > 250 && mouse.x < 550 && mouse.y > 250 && mouse.y < 290) {
                        if (!camaraDisponible) {
                            registroMensaje = "Cámara no disponible.";
                        } else if (registroUsuario.empty()) {
                            registroMensaje = "Introduce un nombre de usuario.";
                        } else if (fs::exists(fs::path("bovedas") / registroUsuario)) {
                            registroMensaje = "El usuario ya existe.";
                        } else {
                            // Generar clave con la cámara y mostrarla
                            sf::Image img = gen.captureImageFromCamera();
                            if (img.getSize().x == 0) {
                                registroMensaje = "Error capturando imagen.";
                            } else {
                                claveGeneradaRegistro = gen.generateKeyFromImage(img, 8);
                                estado = Estado::RegistroMuestraClave;
                            }
                        }
                    }
                    // Botón volver
                    if (mouse.x > 250 && mouse.x < 550 && mouse.y > 320 && mouse.y < 360) {
                        registroUsuario.clear();
                        registroMensaje.clear();
                        estado = Estado::MenuPrincipal;
                    }
                }
                if (event.type == sf::Event::TextEntered) {
                    if (escribiendoRegistroUsuario && event.text.unicode < 128 && std::isalnum(event.text.unicode))
                        registroUsuario += static_cast<char>(event.text.unicode);
                    if (event.text.unicode == 8) { // backspace
                        if (escribiendoRegistroUsuario && !registroUsuario.empty()) registroUsuario.pop_back();
                    }
                }
            }

            // --- Registro: muestra clave generada ---
            if (estado == Estado::RegistroMuestraClave && event.type == sf::Event::MouseButtonPressed) {
                sf::Vector2f mouse = (sf::Vector2f)sf::Mouse::getPosition(window);
                // Botón "He apuntado mi contraseña"
                if (mouse.x > 250 && mouse.x < 550 && mouse.y > 330 && mouse.y < 370) {
                    fs::path bovedaDir = fs::path("bovedas") / registroUsuario;
                    fs::create_directories(bovedaDir);
                    // Guardar archivo cifrado con clave
                    std::string contenido = "Bienvenido a tu bóveda, " + registroUsuario + "!";
                    std::string cifrado = cifrarXOR(contenido, claveGeneradaRegistro);
                    std::ofstream dat(bovedaDir / (registroUsuario + ".dat"), std::ios::binary);
                    dat.write(cifrado.c_str(), cifrado.size());
                    dat.close();
                    registroMensaje = "Usuario creado. Puedes iniciar sesión.";
                    registroUsuario.clear();
                    claveGeneradaRegistro.clear();
                    estado = Estado::MenuPrincipal;
                }
            }

            // --- Selección de usuario para login (clic y teclado) ---
            if (estado == Estado::LoginSeleccionUsuario) {
                if (event.type == sf::Event::KeyPressed) {
                    if (event.key.code == sf::Keyboard::Up && usuarioSeleccionado > 0)
                        usuarioSeleccionado--;
                    if (event.key.code == sf::Keyboard::Down && usuarioSeleccionado + 1 < (int)listaUsuarios.size())
                        usuarioSeleccionado++;
                    if (event.key.code == sf::Keyboard::Enter && !listaUsuarios.empty()) {
                        usuarioActual = listaUsuarios[usuarioSeleccionado];
                        passwordInput.clear();
                        estado = Estado::LoginPidePassword;
                        mensajeSistema.clear();
                    }
                    if (event.key.code == sf::Keyboard::Escape)
                        estado = Estado::MenuPrincipal;
                }
                // --- Selección por clic ---
                if (event.type == sf::Event::MouseButtonPressed) {
                    sf::Vector2f mouse = (sf::Vector2f)sf::Mouse::getPosition(window);
                    for (size_t i = 0; i < usuariosBounds.size(); ++i) {
                        if (usuariosBounds[i].contains(mouse)) {
                            usuarioSeleccionado = i;
                            usuarioActual = listaUsuarios[usuarioSeleccionado];
                            passwordInput.clear();
                            estado = Estado::LoginPidePassword;
                            mensajeSistema.clear();
                            break;
                        }
                    }
                }
            }

            // --- Login: pedir password (la generada aleatoriamente) ---
            if (estado == Estado::LoginPidePassword) {
                if (event.type == sf::Event::TextEntered) {
                    if (event.text.unicode < 128 && std::isprint(event.text.unicode))
                        passwordInput += static_cast<char>(event.text.unicode);
                    if (event.text.unicode == 8 && !passwordInput.empty())
                        passwordInput.pop_back();
                }
                if (event.type == sf::Event::KeyPressed) {
                    if (event.key.code == sf::Keyboard::Return) {
                        if (!camaraDisponible) {
                            mensajeSistema = "Cámara no disponible.";
                        } else {
                            fs::path archivo = fs::path("bovedas") / usuarioActual / (usuarioActual + ".dat");
                            std::ifstream dat(archivo, std::ios::binary);
                            std::ostringstream ss;
                            ss << dat.rdbuf();
                            std::string cifrado = ss.str();
                            std::string descifrado = cifrarXOR(cifrado, passwordInput);
                            if (descifrado.find("Bienvenido a tu bóveda") != std::string::npos) {
                                claveActual = passwordInput;
                                estado = Estado::Boveda;
                                mensajeSistema = "¡Bienvenido, " + usuarioActual + "!";
                            } else {
                                mensajeSistema = "Contraseña incorrecta.";
                            }
                        }
                    }
                    if (event.key.code == sf::Keyboard::Escape)
                        estado = Estado::MenuPrincipal;
                }
            }

            // --- Bóveda abierta: cierre de sesión ---
            if (estado == Estado::Boveda && event.type == sf::Event::KeyPressed && event.key.code == sf::Keyboard::Escape) {
                usuarioActual.clear();
                claveActual.clear();
                passwordInput.clear();
                estado = Estado::MenuPrincipal;
            }
        }

        // --- Renderizado ---
        window.clear();
        window.draw(fondo);
        window.draw(titulo);
        window.draw(spriteCamara);

        if (!camaraDisponible) {
            sf::Text aviso;
            aviso.setFont(font);
            aviso.setString(L"Se necesita una cámara conectada.\nPulsa F5 para reintentar.");
            aviso.setCharacterSize(22);
            aviso.setFillColor(sf::Color(255, 120, 120));
            aviso.setPosition(180, 300);
            window.draw(aviso);
        }

        if (estado == Estado::MenuPrincipal && camaraDisponible) {
            sf::RectangleShape boton1(sf::Vector2f(300, 50)), boton2(sf::Vector2f(300, 50));
            boton1.setPosition(250, 220); boton2.setPosition(250, 300);
            boton1.setFillColor(sf::Color(130, 50, 220)); boton2.setFillColor(sf::Color(100, 100, 200));
            window.draw(boton1); window.draw(boton2);
            sf::Text t1("Crear usuario", font, 24), t2("Iniciar sesión", font, 24);
            t1.setPosition(310, 230); t2.setPosition(310, 310);
            window.draw(t1); window.draw(t2);
            sf::Text m(registroMensaje, font, 16); m.setFillColor(sf::Color::Yellow); m.setPosition(250, 370); window.draw(m);
        }

        if (estado == Estado::Registro && camaraDisponible) {
            sf::Text t("Registro de usuario", font, 28); t.setPosition(230, 100); window.draw(t);
            sf::RectangleShape campo1(sf::Vector2f(300, 40));
            campo1.setPosition(250, 160);
            campo1.setFillColor(sf::Color(220, 200, 240));
            window.draw(campo1);
            sf::Text l1("Usuario:", font, 18);
            l1.setPosition(120, 170);
            window.draw(l1);
            sf::Text v1(registroUsuario, font, 18);
            v1.setPosition(260, 170);
            window.draw(v1);
            sf::RectangleShape botonCrear(sf::Vector2f(300, 40)); botonCrear.setPosition(250, 250); botonCrear.setFillColor(sf::Color(130, 50, 220)); window.draw(botonCrear);
            sf::Text tCrear("Crear usuario", font, 20); tCrear.setPosition(320, 260); window.draw(tCrear);
            sf::RectangleShape botonVolver(sf::Vector2f(300, 40)); botonVolver.setPosition(250, 320); botonVolver.setFillColor(sf::Color(100, 100, 200)); window.draw(botonVolver);
            sf::Text tVolver("Volver", font, 20); tVolver.setPosition(360, 330); window.draw(tVolver);
            sf::Text m(registroMensaje, font, 16); m.setFillColor(sf::Color::Yellow); m.setPosition(250, 370); window.draw(m);
        }

        if (estado == Estado::RegistroMuestraClave && camaraDisponible) {
            sf::Text t("¡Apunta tu contraseña!", font, 28); t.setPosition(180, 120); window.draw(t);
            sf::Text clave("Contraseña generada: " + claveGeneradaRegistro, font, 26);
            clave.setFillColor(sf::Color(255, 255, 100));
            clave.setPosition(140, 200);
            window.draw(clave);
            sf::Text aviso("Guárdala bien. Es la ÚNICA vez que la verás.", font, 18);
            aviso.setPosition(140, 250); window.draw(aviso);
            sf::RectangleShape botonOk(sf::Vector2f(300, 40)); botonOk.setPosition(250, 330); botonOk.setFillColor(sf::Color(130, 50, 220)); window.draw(botonOk);
            sf::Text tOk("He apuntado mi contraseña", font, 20); tOk.setPosition(270, 340); window.draw(tOk);
        }

        if (estado == Estado::LoginSeleccionUsuario && camaraDisponible) {
            sf::Text t("Selecciona usuario:", font, 24); t.setPosition(250, 120); window.draw(t);
            usuariosBounds.clear();
            for (size_t i = 0; i < listaUsuarios.size(); ++i) {
                sf::Text user(listaUsuarios[i], font, 22);
                user.setPosition(300, 180 + 40 * i);
                if ((int)i == usuarioSeleccionado)
                    user.setFillColor(sf::Color(220, 200, 255));
                else
                    user.setFillColor(sf::Color::White);
                window.draw(user);
                usuariosBounds.push_back(user.getGlobalBounds());
            }
            sf::Text tEsc("ESC para volver", font, 16); tEsc.setPosition(20, 560); window.draw(tEsc);
        }

        if (estado == Estado::LoginPidePassword && camaraDisponible) {
            sf::Text t("Introduce la contraseña de " + usuarioActual + ":", font, 22); t.setPosition(120, 200); window.draw(t);
            sf::RectangleShape campo(sf::Vector2f(300, 40)); campo.setPosition(250, 260); campo.setFillColor(sf::Color(220, 200, 240)); window.draw(campo);
            sf::Text pass(std::string(passwordInput.size(), '*'), font, 22); pass.setPosition(260, 270); window.draw(pass);
            sf::Text tEsc("ESC para volver", font, 16); tEsc.setPosition(20, 560); window.draw(tEsc);
            sf::Text m(mensajeSistema, font, 16); m.setFillColor(sf::Color::Yellow); m.setPosition(250, 320); window.draw(m);
        }

        if (estado == Estado::Boveda && camaraDisponible) {
            sf::Text t("Bóveda de " + usuarioActual, font, 28); t.setPosition(200, 120); window.draw(t);
            sf::Text m("¡Bienvenido! Pulsa ESC para cerrar sesión.", font, 20); m.setPosition(200, 200); window.draw(m);
        }

        window.display();
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    return 0;
}
