#include "almalili/fuente.hpp"

#include <fstream>
#include <limits>
#include <utility>

#include "almalili/utf8.hpp"

namespace almalili {

namespace {

constexpr std::string_view kBomUtf8 = "\xEF\xBB\xBF";

bool empieza_con(std::string_view texto, std::string_view prefijo) noexcept {
    return texto.size() >= prefijo.size() && texto.compare(0, prefijo.size(), prefijo) == 0;
}

}  // namespace

Fuente::Fuente(std::string nombre, std::string contenido)
    : nombre_(std::move(nombre)), contenido_(std::move(contenido)), texto_(contenido_) {
    if (empieza_con(texto_, kBomUtf8)) texto_.remove_prefix(kBomUtf8.size());

    const char* base = texto_.data();
    const char* fin = base + texto_.size();
    inicios_linea_.push_back(0);
    for (const char* p = base; p < fin;) {
        if (const std::size_t n = utf8::longitud_salto(p, fin)) {
            p += n;
            inicios_linea_.push_back(static_cast<std::uint32_t>(p - base));
        } else {
            ++p;
        }
    }
}

bool Fuente::leer_archivo(const std::filesystem::path& ruta, std::string& contenido,
                          std::string& error) {
    std::ifstream archivo(ruta, std::ios::binary);
    if (!archivo) {
        error = "no se pudo abrir el archivo";
        return false;
    }
    archivo.seekg(0, std::ios::end);
    const std::streamoff tamano = archivo.tellg();
    archivo.seekg(0, std::ios::beg);
    if (tamano < 0) {
        error = "no se pudo determinar el tamaño del archivo";
        return false;
    }
    // Los desplazamientos de los tokens son de 32 bits.
    if (static_cast<std::uintmax_t>(tamano) >= std::numeric_limits<std::uint32_t>::max()) {
        error = "el archivo es demasiado grande (el límite es 4 GiB)";
        return false;
    }
    contenido.resize(static_cast<std::size_t>(tamano));
    archivo.read(contenido.data(), tamano);
    if (!archivo && !archivo.eof()) {
        error = "ocurrió un error al leer el archivo";
        return false;
    }
    contenido.resize(static_cast<std::size_t>(archivo.gcount()));

    if (empieza_con(contenido, "\xFF\xFE") || empieza_con(contenido, "\xFE\xFF")) {
        error = "el archivo está codificado en UTF-16; guárdalo como UTF-8";
        return false;
    }
    return true;
}

std::uint32_t Fuente::inicio_linea(std::uint32_t numero) const noexcept {
    if (numero == 0) numero = 1;
    if (numero > inicios_linea_.size()) return static_cast<std::uint32_t>(texto_.size());
    return inicios_linea_[numero - 1];
}

std::string_view Fuente::linea(std::uint32_t numero) const noexcept {
    if (numero == 0 || numero > inicios_linea_.size()) return {};
    const std::uint32_t inicio = inicios_linea_[numero - 1];
    const std::uint32_t fin = numero < inicios_linea_.size()
                                  ? inicios_linea_[numero]
                                  : static_cast<std::uint32_t>(texto_.size());
    std::string_view vista = texto_.substr(inicio, fin - inicio);
    for (std::string_view terminador : {"\r\n", "\n", "\r", "\xC2\x85", "\xE2\x80\xA8", "\xE2\x80\xA9"}) {
        if (vista.size() >= terminador.size() &&
            vista.compare(vista.size() - terminador.size(), terminador.size(), terminador) == 0) {
            vista.remove_suffix(terminador.size());
            break;
        }
    }
    return vista;
}

}  // namespace almalili
