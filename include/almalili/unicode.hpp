// Propiedades Unicode que necesita el analizador léxico.
#pragma once

#include <string_view>

namespace almalili::unicode {

// Versión de Unicode de las tablas generadas.
[[nodiscard]] std::string_view version() noexcept;

// Identificadores según UAX #31: XID_Start (más «_») y XID_Continue. Cubre la
// ñ, las vocales acentuadas, la ü y las letras de las lenguas originarias.
[[nodiscard]] bool es_inicio_identificador(char32_t c) noexcept;
[[nodiscard]] bool es_continuacion_identificador(char32_t c) noexcept;

// Espacios horizontales fuera de ASCII (U+00A0, U+2000–U+200A, U+3000…).
[[nodiscard]] bool es_espacio(char32_t c) noexcept;

// Terminadores de línea fuera de ASCII (U+0085, U+2028, U+2029).
[[nodiscard]] bool es_salto_linea(char32_t c) noexcept;

// Controles de formato bidireccional que permiten ocultar código a la vista
// («Trojan Source»): U+202A–U+202E y U+2066–U+2069.
[[nodiscard]] bool es_control_bidi(char32_t c) noexcept;

// Caracteres sin representación visible (ancho cero, guion opcional, BOM).
[[nodiscard]] bool es_invisible(char32_t c) noexcept;

// Composición canónica de un par: componer('a', U+0301) == 'á'. Devuelve 0 si
// el par no tiene forma precompuesta.
[[nodiscard]] char32_t componer(char32_t base, char32_t marca) noexcept;

// Sugerencia para un carácter que suele colarse por error (comillas
// tipográficas sueltas, acentos sin vocal, signos parecidos a los de ASCII).
// Vacía si no hay nada que sugerir.
[[nodiscard]] std::string_view sugerencia(char32_t c) noexcept;

}  // namespace almalili::unicode
