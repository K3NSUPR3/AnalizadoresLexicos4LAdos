// Decodificación y codificación estricta de UTF-8.
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace almalili::utf8 {

inline constexpr char32_t kReemplazo = 0xFFFD;
inline constexpr char32_t kMaximo = 0x10FFFF;

// Resultado de decodificar una unidad: un punto de código válido o una
// subsecuencia inválida máxima (en cuyo caso `punto` es U+FFFD).
struct Decodificado {
    char32_t punto;
    std::uint8_t longitud;  // bytes consumidos, siempre >= 1
    bool valido;
};

// Decodifica la unidad que comienza en `p`. Requiere p < fin. Rechaza formas
// sobrelargas, sustitutos UTF-16, valores mayores que U+10FFFF y secuencias
// truncadas.
[[nodiscard]] Decodificado decodificar(const char* p, const char* fin) noexcept;

// Escribe `punto` en `salida` (hasta 4 bytes) y devuelve los bytes escritos.
std::size_t codificar(char32_t punto, char* salida) noexcept;

void anexar(std::string& destino, char32_t punto);

// Cantidad de unidades (puntos de código o subsecuencias inválidas) del texto.
// Es la medida que se usa para las columnas.
[[nodiscard]] inline std::size_t contar(const char* p, const char* fin) noexcept {
    std::size_t n = 0;
    while (p < fin) {
        if (static_cast<unsigned char>(*p) < 0x80) {
            ++p;
        } else {
            p += decodificar(p, fin).longitud;
        }
        ++n;
    }
    return n;
}

[[nodiscard]] inline std::size_t contar(std::string_view texto) noexcept {
    return contar(texto.data(), texto.data() + texto.size());
}

[[nodiscard]] bool es_valido(std::string_view texto) noexcept;

// Copia del texto con cada subsecuencia inválida sustituida por U+FFFD.
[[nodiscard]] std::string sanear(std::string_view texto);

[[nodiscard]] constexpr bool es_escalar(char32_t c) noexcept {
    return c <= kMaximo && !(c >= 0xD800 && c <= 0xDFFF);
}

// Longitud en bytes del terminador de línea que comienza en `p`, o 0 si no hay
// ninguno. Requiere p < fin. Reconoce LF, CR, CRLF, NEL (U+0085), U+2028 y
// U+2029.
[[nodiscard]] inline std::size_t longitud_salto(const char* p, const char* fin) noexcept {
    const auto c = static_cast<unsigned char>(*p);
    if (c == '\n') return 1;
    if (c == '\r') return (p + 1 < fin && p[1] == '\n') ? 2 : 1;
    if (c == 0xC2) return (p + 1 < fin && static_cast<unsigned char>(p[1]) == 0x85) ? 2 : 0;
    if (c == 0xE2) {
        if (p + 2 < fin && static_cast<unsigned char>(p[1]) == 0x80) {
            const auto c2 = static_cast<unsigned char>(p[2]);
            if (c2 == 0xA8 || c2 == 0xA9) return 3;
        }
    }
    return 0;
}

}  // namespace almalili::utf8
