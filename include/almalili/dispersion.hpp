// Función de dispersión (hash) compartida por las tablas del compilador.
#pragma once

#include <cstddef>
#include <cstdint>
#include <string_view>

namespace almalili {

// FNV-1a de 64 bits. Para nombres cortos, que son casi todos, es más rápida
// que las alternativas que procesan por bloques.
[[nodiscard]] inline std::uint64_t dispersar(std::string_view texto) noexcept {
    std::uint64_t h = 14695981039346656037ull;
    for (const char c : texto) {
        h ^= static_cast<unsigned char>(c);
        h *= 1099511628211ull;
    }
    return h;
}

// Índice inicial dentro de una tabla cuyo tamaño es potencia de dos.
[[nodiscard]] inline std::size_t ranura_inicial(std::uint64_t h, std::size_t mascara) noexcept {
    return static_cast<std::size_t>(h ^ (h >> 29)) & mascara;
}

}  // namespace almalili
