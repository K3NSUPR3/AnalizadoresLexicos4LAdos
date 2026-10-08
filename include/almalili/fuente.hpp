// Texto fuente de un programa y su tabla de líneas.
#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace almalili {

// Ubicación dentro de una Fuente.
struct Posicion {
    std::uint32_t desplazamiento = 0;  // en bytes desde el inicio del texto
    std::uint32_t linea = 1;           // desde 1
    std::uint32_t columna = 1;         // desde 1, en puntos de código
};

// Dueña del texto de un archivo. Los tokens guardan vistas (string_view) a su
// contenido, por eso no se puede copiar ni mover.
class Fuente {
public:
    // `contenido` debe estar en UTF-8; si trae BOM, se omite.
    Fuente(std::string nombre, std::string contenido);

    Fuente(const Fuente&) = delete;
    Fuente& operator=(const Fuente&) = delete;

    // Lee un archivo completo. Si falla (no existe, es demasiado grande o está
    // en UTF-16) devuelve false y deja la explicación en `error`.
    static bool leer_archivo(const std::filesystem::path& ruta, std::string& contenido,
                             std::string& error);

    [[nodiscard]] const std::string& nombre() const noexcept { return nombre_; }

    // Texto sin BOM. Se garantiza que texto().data()[texto().size()] == '\0',
    // lo que el analizador léxico aprovecha como centinela.
    [[nodiscard]] std::string_view texto() const noexcept { return texto_; }

    [[nodiscard]] bool tenia_bom() const noexcept { return texto_.size() != contenido_.size(); }

    [[nodiscard]] std::uint32_t cantidad_lineas() const noexcept {
        return static_cast<std::uint32_t>(inicios_linea_.size());
    }

    // Desplazamiento del primer byte de la línea `numero` (desde 1).
    [[nodiscard]] std::uint32_t inicio_linea(std::uint32_t numero) const noexcept;

    // Texto de la línea `numero` (desde 1) sin su terminador.
    [[nodiscard]] std::string_view linea(std::uint32_t numero) const noexcept;

private:
    std::string nombre_;
    std::string contenido_;
    std::string_view texto_;
    std::vector<std::uint32_t> inicios_linea_;
};

}  // namespace almalili
