#include "almalili/utf8.hpp"

namespace almalili::utf8 {

namespace {

constexpr Decodificado invalido(std::uint8_t longitud) noexcept {
    return {kReemplazo, longitud, false};
}

// Byte de continuación en p[i], o -1 si falta o no tiene la forma 10xxxxxx.
inline int continuacion(const char* p, const char* fin, int i) noexcept {
    if (p + i >= fin) return -1;
    const auto b = static_cast<unsigned char>(p[i]);
    return (b & 0xC0) == 0x80 ? (b & 0x3F) : -1;
}

}  // namespace

Decodificado decodificar(const char* p, const char* fin) noexcept {
    const auto b0 = static_cast<unsigned char>(*p);
    if (b0 < 0x80) return {b0, 1, true};

    if (b0 >= 0xC2 && b0 <= 0xDF) {
        const int c1 = continuacion(p, fin, 1);
        if (c1 < 0) return invalido(1);
        return {static_cast<char32_t>(((b0 & 0x1F) << 6) | c1), 2, true};
    }

    if (b0 >= 0xE0 && b0 <= 0xEF) {
        const int c1 = continuacion(p, fin, 1);
        if (c1 < 0) return invalido(1);
        const auto b1 = static_cast<unsigned char>(p[1]);
        if (b0 == 0xE0 && b1 < 0xA0) return invalido(1);  // forma sobrelarga
        if (b0 == 0xED && b1 > 0x9F) return invalido(1);  // sustituto UTF-16
        const int c2 = continuacion(p, fin, 2);
        if (c2 < 0) return invalido(2);
        return {static_cast<char32_t>(((b0 & 0x0F) << 12) | (c1 << 6) | c2), 3, true};
    }

    if (b0 >= 0xF0 && b0 <= 0xF4) {
        const int c1 = continuacion(p, fin, 1);
        if (c1 < 0) return invalido(1);
        const auto b1 = static_cast<unsigned char>(p[1]);
        if (b0 == 0xF0 && b1 < 0x90) return invalido(1);  // forma sobrelarga
        if (b0 == 0xF4 && b1 > 0x8F) return invalido(1);  // mayor que U+10FFFF
        const int c2 = continuacion(p, fin, 2);
        if (c2 < 0) return invalido(2);
        const int c3 = continuacion(p, fin, 3);
        if (c3 < 0) return invalido(3);
        return {static_cast<char32_t>(((b0 & 0x07) << 18) | (c1 << 12) | (c2 << 6) | c3), 4, true};
    }

    // Continuación suelta (0x80–0xBF) o byte que nunca aparece en UTF-8.
    return invalido(1);
}

std::size_t codificar(char32_t punto, char* salida) noexcept {
    if (!es_escalar(punto)) punto = kReemplazo;
    if (punto < 0x80) {
        salida[0] = static_cast<char>(punto);
        return 1;
    }
    if (punto < 0x800) {
        salida[0] = static_cast<char>(0xC0 | (punto >> 6));
        salida[1] = static_cast<char>(0x80 | (punto & 0x3F));
        return 2;
    }
    if (punto < 0x10000) {
        salida[0] = static_cast<char>(0xE0 | (punto >> 12));
        salida[1] = static_cast<char>(0x80 | ((punto >> 6) & 0x3F));
        salida[2] = static_cast<char>(0x80 | (punto & 0x3F));
        return 3;
    }
    salida[0] = static_cast<char>(0xF0 | (punto >> 18));
    salida[1] = static_cast<char>(0x80 | ((punto >> 12) & 0x3F));
    salida[2] = static_cast<char>(0x80 | ((punto >> 6) & 0x3F));
    salida[3] = static_cast<char>(0x80 | (punto & 0x3F));
    return 4;
}

void anexar(std::string& destino, char32_t punto) {
    char bytes[4];
    destino.append(bytes, codificar(punto, bytes));
}

bool es_valido(std::string_view texto) noexcept {
    const char* p = texto.data();
    const char* fin = p + texto.size();
    while (p < fin) {
        if (static_cast<unsigned char>(*p) < 0x80) {
            ++p;
            continue;
        }
        const Decodificado d = decodificar(p, fin);
        if (!d.valido) return false;
        p += d.longitud;
    }
    return true;
}

std::string sanear(std::string_view texto) {
    std::string salida;
    salida.reserve(texto.size());
    const char* p = texto.data();
    const char* fin = p + texto.size();
    while (p < fin) {
        const Decodificado d = decodificar(p, fin);
        if (d.valido) {
            salida.append(p, d.longitud);
        } else {
            anexar(salida, kReemplazo);
        }
        p += d.longitud;
    }
    return salida;
}

}  // namespace almalili::utf8
