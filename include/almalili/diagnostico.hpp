// Recolección e impresión de errores y advertencias.
#pragma once

#include <cstddef>
#include <cstdint>
#include <iosfwd>
#include <string>
#include <string_view>
#include <vector>

#include "almalili/fuente.hpp"

namespace almalili {

enum class Gravedad : std::uint8_t { Nota, Advertencia, Error };

[[nodiscard]] std::string_view nombre(Gravedad g) noexcept;

struct Diagnostico {
    Gravedad gravedad = Gravedad::Error;
    std::string codigo;          // p. ej. «L0003»
    Posicion pos;
    std::uint32_t longitud = 0;  // bytes abarcados desde `pos`
    std::string mensaje;
    std::string ayuda;           // opcional
};

class Diagnosticos {
public:
    // A partir de este número de diagnósticos solo se llevan las cuentas.
    static constexpr std::size_t kMaximoAlmacenados = 200;

    void reportar(Gravedad gravedad, std::string_view codigo, Posicion pos, std::uint32_t longitud,
                  std::string mensaje, std::string ayuda = {});

    [[nodiscard]] bool hay_errores() const noexcept { return errores_ != 0; }
    [[nodiscard]] std::size_t errores() const noexcept { return errores_; }
    [[nodiscard]] std::size_t advertencias() const noexcept { return advertencias_; }
    [[nodiscard]] const std::vector<Diagnostico>& lista() const noexcept { return lista_; }

    // Imprime cada diagnóstico con la línea de código y una marca bajo el
    // tramo señalado.
    void imprimir(std::ostream& salida, const Fuente& fuente, bool color) const;

    // «2 errores, 1 advertencia», o cadena vacía si no hubo ninguno.
    [[nodiscard]] std::string resumen() const;

private:
    std::vector<Diagnostico> lista_;
    std::size_t errores_ = 0;
    std::size_t advertencias_ = 0;
};

}  // namespace almalili
