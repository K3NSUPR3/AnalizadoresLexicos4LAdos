// Tipos de token y vocabulario del lenguaje (generados desde tokens.def).
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

#include "almalili/fuente.hpp"
#include "almalili/tabla_simbolos.hpp"

namespace almalili {

enum class TipoToken : std::uint8_t {
#define TOKEN(nombre, categoria, descripcion) nombre,
#define RESERVADA(nombre, grafia) nombre,
#define SIGNO(nombre, grafia) nombre,
#include "almalili/tokens.def"
    Cantidad_
};

inline constexpr std::size_t kCantidadTipos = static_cast<std::size_t>(TipoToken::Cantidad_);

enum class Categoria : std::uint8_t { Especial, Identificador, Literal, Comentario, Reservada, Signo };

struct Token {
    TipoToken tipo = TipoToken::FinArchivo;
    bool inicia_linea = false;          // es el primer token de su línea
    bool precedido_de_espacio = false;  // hay blancos o comentarios antes
    IdSimbolo simbolo = kSinSimbolo;    // solo en identificadores
    Posicion pos;                       // donde comienza
    std::uint32_t longitud = 0;         // en bytes
    std::string_view lexema;            // texto tal como aparece en la fuente
    // Identificador: nombre normalizado. LiteralCadena: contenido ya sin
    // comillas y con los escapes resueltos.
    std::string_view texto;
    union {
        std::uint64_t entero = 0;  // LiteralEntero
        double real;               // LiteralReal
        char32_t caracter;         // LiteralCaracter
    };
};

// Una forma de escribir una palabra reservada o un signo.
struct Grafia {
    std::string_view texto;
    TipoToken tipo;
    bool alias;  // false para la grafía canónica
};

struct ListaGrafias {
    const Grafia* primero;
    const Grafia* ultimo;
    [[nodiscard]] const Grafia* begin() const noexcept { return primero; }
    [[nodiscard]] const Grafia* end() const noexcept { return ultimo; }
};

// Nombre del enumerador («PrFuncion», «MenorIgual»…).
[[nodiscard]] std::string_view nombre(TipoToken tipo) noexcept;
[[nodiscard]] std::string_view nombre(Categoria categoria) noexcept;
[[nodiscard]] Categoria categoria(TipoToken tipo) noexcept;

// Grafía canónica de una palabra reservada o de un signo; vacía en los demás.
[[nodiscard]] std::string_view grafia(TipoToken tipo) noexcept;

// Texto para mensajes: «palabra reservada «función»», «literal entero»…
[[nodiscard]] std::string describir(TipoToken tipo);

// Todas las grafías del vocabulario, alias incluidos, en el orden de tokens.def.
[[nodiscard]] ListaGrafias grafias() noexcept;

// Tipo de la palabra reservada `palabra`, o TipoToken::Identificador si no lo
// es. La primera forma es la que usa el analizador léxico: recibe el texto como
// puntero y longitud, y el valor de dispersar() ya calculado.
[[nodiscard]] TipoToken buscar_reservada(const char* palabra, std::size_t longitud,
                                         std::uint64_t dispersion) noexcept;
[[nodiscard]] inline TipoToken buscar_reservada(std::string_view palabra) noexcept {
    return buscar_reservada(palabra.data(), palabra.size(), dispersar(palabra));
}

// Signo más largo que comienza en `p` (requiere p < fin). Devuelve
// TipoToken::Error si ninguno coincide; si coincide, deja su tamaño en `longitud`.
[[nodiscard]] TipoToken casar_signo(const char* p, const char* fin, std::size_t& longitud) noexcept;

}  // namespace almalili
