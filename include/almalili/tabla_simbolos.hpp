// Tabla de símbolos léxica: internado de cadenas y registro de identificadores.
#pragma once

#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <optional>
#include <string_view>
#include <unordered_set>
#include <vector>

#include "almalili/dispersion.hpp"
#include "almalili/fuente.hpp"

namespace almalili {

using IdSimbolo = std::uint32_t;
inline constexpr IdSimbolo kSinSimbolo = std::numeric_limits<IdSimbolo>::max();

// Entrada de un identificador. Las fases posteriores (análisis sintáctico y
// semántico) parten de estos identificadores ya unificados para construir sus
// propios ámbitos.
struct Simbolo {
    std::string_view nombre;  // normalizado; vive mientras viva la tabla
    Posicion primera_aparicion;
    std::uint32_t apariciones = 0;
};

class TablaSimbolos {
public:
    TablaSimbolos();
    TablaSimbolos(const TablaSimbolos&) = delete;
    TablaSimbolos& operator=(const TablaSimbolos&) = delete;

    // Devuelve una copia única y estable de `texto`: dos cadenas iguales
    // siempre devuelven la misma vista.
    std::string_view internar(std::string_view texto);

    // Registra una aparición del identificador `nombre` y devuelve su id. El
    // mismo nombre siempre recibe el mismo id. La segunda forma es la que usa
    // el analizador léxico: recibe el nombre como puntero y longitud, y el
    // valor de dispersar() ya calculado.
    IdSimbolo registrar(std::string_view nombre, const Posicion& pos) {
        return registrar(nombre.data(), nombre.size(), pos, dispersar(nombre));
    }
    IdSimbolo registrar(const char* nombre, std::size_t longitud, const Posicion& pos,
                        std::uint64_t dispersion);

    [[nodiscard]] std::optional<IdSimbolo> buscar(std::string_view nombre) const noexcept;

    [[nodiscard]] const Simbolo& operator[](IdSimbolo id) const { return simbolos_[id]; }
    [[nodiscard]] std::size_t cantidad() const noexcept { return simbolos_.size(); }
    [[nodiscard]] auto begin() const noexcept { return simbolos_.begin(); }
    [[nodiscard]] auto end() const noexcept { return simbolos_.end(); }

private:
    static constexpr std::size_t kTamanoBloque = 64 * 1024;
    static constexpr std::size_t kRanurasIniciales = 1024;

    char* reservar(std::size_t bytes);
    void crecer();

    // Los bloques nunca se reubican, así que las vistas entregadas no caducan.
    std::vector<std::unique_ptr<char[]>> bloques_;
    char* cursor_ = nullptr;
    std::size_t libre_ = 0;

    std::unordered_set<std::string_view> cadenas_;

    // Tabla de direccionamiento abierto con sondeo lineal: cada ranura guarda
    // un id (o kSinSimbolo) y siempre queda al menos la mitad vacía.
    std::vector<IdSimbolo> ranuras_;
    std::vector<std::uint64_t> dispersiones_;  // paralelo a simbolos_
    std::vector<Simbolo> simbolos_;
};

}  // namespace almalili
