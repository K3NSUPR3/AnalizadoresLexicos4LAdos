#include "almalili/tabla_simbolos.hpp"

#include <algorithm>
#include <cstring>

namespace almalili {

TablaSimbolos::TablaSimbolos() : ranuras_(kRanurasIniciales, kSinSimbolo) {}

char* TablaSimbolos::reservar(std::size_t bytes) {
    if (bytes > libre_) {
        const std::size_t tamano = std::max(bytes, kTamanoBloque);
        bloques_.push_back(std::make_unique<char[]>(tamano));
        cursor_ = bloques_.back().get();
        libre_ = tamano;
    }
    char* r = cursor_;
    cursor_ += bytes;
    libre_ -= bytes;
    return r;
}

std::string_view TablaSimbolos::internar(std::string_view texto) {
    if (const auto it = cadenas_.find(texto); it != cadenas_.end()) return *it;
    if (texto.empty()) return *cadenas_.insert(std::string_view("", 0)).first;
    char* copia = reservar(texto.size());
    std::memcpy(copia, texto.data(), texto.size());
    return *cadenas_.insert(std::string_view(copia, texto.size())).first;
}

void TablaSimbolos::crecer() {
    std::vector<IdSimbolo> nuevas(ranuras_.size() * 2, kSinSimbolo);
    const std::size_t mascara = nuevas.size() - 1;
    for (IdSimbolo id = 0; id < simbolos_.size(); ++id) {
        std::size_t i = ranura_inicial(dispersiones_[id], mascara);
        while (nuevas[i] != kSinSimbolo) i = (i + 1) & mascara;
        nuevas[i] = id;
    }
    ranuras_ = std::move(nuevas);
}

IdSimbolo TablaSimbolos::registrar(const char* nombre, std::size_t longitud, const Posicion& pos,
                                   std::uint64_t dispersion) {
    std::size_t mascara = ranuras_.size() - 1;
    std::size_t i = ranura_inicial(dispersion, mascara);
    while (ranuras_[i] != kSinSimbolo) {
        const IdSimbolo id = ranuras_[i];
        Simbolo& s = simbolos_[id];
        if (dispersiones_[id] == dispersion && s.nombre.size() == longitud &&
            std::memcmp(s.nombre.data(), nombre, longitud) == 0) {
            ++s.apariciones;
            return id;
        }
        i = (i + 1) & mascara;
    }

    if ((simbolos_.size() + 1) * 2 > ranuras_.size()) {
        crecer();
        mascara = ranuras_.size() - 1;
        i = ranura_inicial(dispersion, mascara);
        while (ranuras_[i] != kSinSimbolo) i = (i + 1) & mascara;
    }
    const auto id = static_cast<IdSimbolo>(simbolos_.size());
    simbolos_.push_back({internar({nombre, longitud}), pos, 1});
    dispersiones_.push_back(dispersion);
    ranuras_[i] = id;
    return id;
}

std::optional<IdSimbolo> TablaSimbolos::buscar(std::string_view nombre) const noexcept {
    const std::uint64_t dispersion = dispersar(nombre);
    const std::size_t mascara = ranuras_.size() - 1;
    for (std::size_t i = ranura_inicial(dispersion, mascara); ranuras_[i] != kSinSimbolo;
         i = (i + 1) & mascara) {
        const IdSimbolo id = ranuras_[i];
        if (dispersiones_[id] == dispersion && simbolos_[id].nombre == nombre) return id;
    }
    return std::nullopt;
}

}  // namespace almalili
