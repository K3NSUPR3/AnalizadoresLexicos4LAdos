#include "almalili/unicode.hpp"

#include <algorithm>
#include <iterator>

namespace almalili::unicode {

namespace {

struct Rango {
    char32_t primero;
    char32_t ultimo;
};

struct Composicion {
    char32_t base;
    char32_t marca;
    char32_t compuesto;
};

#include "unicode_tablas.inc"

template <std::size_t N>
bool en_rangos(const Rango (&tabla)[N], char32_t c) noexcept {
    const Rango* it = std::upper_bound(std::begin(tabla), std::end(tabla), c,
                                       [](char32_t v, const Rango& r) { return v < r.primero; });
    return it != std::begin(tabla) && c <= (it - 1)->ultimo;
}

constexpr bool es_letra_ascii(char32_t c) noexcept {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
}

// Bloque Latin-1: de À a ÿ todo son letras salvo × y ÷. Es el caso habitual en
// español y evita la búsqueda binaria.
constexpr bool es_letra_latin1(char32_t c) noexcept {
    return c >= 0xC0 && c <= 0xFF && c != 0xD7 && c != 0xF7;
}

}  // namespace

std::string_view version() noexcept {
    return kVersionUnicode;
}

bool es_inicio_identificador(char32_t c) noexcept {
    if (c < 0x80) return es_letra_ascii(c);
    if (es_letra_latin1(c)) return true;
    return en_rangos(kXidInicio, c);
}

bool es_continuacion_identificador(char32_t c) noexcept {
    if (c < 0x80) return es_letra_ascii(c) || (c >= '0' && c <= '9');
    if (es_letra_latin1(c)) return true;
    return en_rangos(kXidContinuacion, c);
}

bool es_espacio(char32_t c) noexcept {
    return c == 0x00A0 || c == 0x1680 || (c >= 0x2000 && c <= 0x200A) || c == 0x202F ||
           c == 0x205F || c == 0x3000;
}

bool es_salto_linea(char32_t c) noexcept {
    return c == 0x0085 || c == 0x2028 || c == 0x2029;
}

bool es_control_bidi(char32_t c) noexcept {
    return (c >= 0x202A && c <= 0x202E) || (c >= 0x2066 && c <= 0x2069);
}

bool es_invisible(char32_t c) noexcept {
    return c == 0x00AD || (c >= 0x200B && c <= 0x200D) || c == 0x2060 || c == 0xFEFF;
}

char32_t componer(char32_t base, char32_t marca) noexcept {
    if (marca < 0x0300) return 0;
    const Composicion* it = std::lower_bound(
        std::begin(kComposiciones), std::end(kComposiciones), Composicion{base, marca, 0},
        [](const Composicion& a, const Composicion& b) {
            return a.base != b.base ? a.base < b.base : a.marca < b.marca;
        });
    if (it != std::end(kComposiciones) && it->base == base && it->marca == marca) {
        return it->compuesto;
    }
    return 0;
}

std::string_view sugerencia(char32_t c) noexcept {
    switch (c) {
        case 0x037E:
            return "es el signo de interrogación griego: se ve igual que «;» pero es otro carácter";
        case 0x2018:
        case 0x2019:
            return "es una comilla simple tipográfica; para un literal de carácter usa el apóstrofo recto «'»";
        case 0x201D:
            return "es una comilla tipográfica de cierre sin su comilla de apertura";
        case 0x00BB:
            return "es una comilla angular de cierre sin su comilla de apertura";
        case 0x2013:
        case 0x2014:
            return "es una raya; para restar usa el guion «-»";
        case 0x00B4:
        case 0x0060:
        case 0x00A8:
            return "es un acento suelto: la tecla de acento no se combinó con una vocal";
        default:
            break;
    }
    if (es_invisible(c)) return "es un carácter invisible; bórralo";
    if (c >= 0xFF01 && c <= 0xFF5E) return "es un carácter de ancho completo; usa su equivalente ASCII";
    if (c >= 0x0300 && c <= 0x036F) return "es una marca combinante sin una letra antes";
    return {};
}

}  // namespace almalili::unicode
