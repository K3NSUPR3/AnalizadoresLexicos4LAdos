#include "almalili/token.hpp"

#include <algorithm>
#include <array>
#include <cstring>
#include <iterator>
#include <vector>

namespace almalili {

namespace {

struct InfoTipo {
    std::string_view nombre;
    Categoria categoria;
    std::string_view grafia;
    std::string_view descripcion;
};

constexpr InfoTipo kInfo[] = {
#define TOKEN(nombre, categoria, descripcion) {#nombre, Categoria::categoria, {}, descripcion},
#define RESERVADA(nombre, grafia) {#nombre, Categoria::Reservada, grafia, {}},
#define SIGNO(nombre, grafia) {#nombre, Categoria::Signo, grafia, {}},
#include "almalili/tokens.def"
};

static_assert(std::size(kInfo) == kCantidadTipos);

constexpr Grafia kGrafias[] = {
#define RESERVADA(nombre, grafia) {grafia, TipoToken::nombre, false},
#define SIGNO(nombre, grafia) {grafia, TipoToken::nombre, false},
#define ALIAS(nombre, grafia) {grafia, TipoToken::nombre, true},
#include "almalili/tokens.def"
};

const InfoTipo& info(TipoToken tipo) noexcept {
    return kInfo[static_cast<std::size_t>(tipo)];
}

// ── Palabras reservadas ─────────────────────────────────────────────────────
//
// Tabla de direccionamiento abierto con sondeo lineal. Como el vocabulario es
// fijo y la tabla queda casi vacía, la búsqueda rara vez pasa de una ranura.

constexpr std::size_t kRanurasReservadas = 512;
static_assert(std::size(kGrafias) * 2 <= kRanurasReservadas);

struct TablaReservadas {
    struct Ranura {
        std::string_view texto;  // vacío si la ranura está libre
        std::uint64_t dispersion = 0;
        TipoToken tipo = TipoToken::Identificador;
    };
    std::array<Ranura, kRanurasReservadas> ranuras;
    std::size_t longitud_minima = static_cast<std::size_t>(-1);
    std::size_t longitud_maxima = 0;

    TablaReservadas() {
        for (const Grafia& g : kGrafias) {
            if (info(g.tipo).categoria != Categoria::Reservada) continue;
            const std::uint64_t h = dispersar(g.texto);
            std::size_t i = ranura_inicial(h, kRanurasReservadas - 1);
            while (!ranuras[i].texto.empty()) i = (i + 1) & (kRanurasReservadas - 1);
            ranuras[i] = {g.texto, h, g.tipo};
            longitud_minima = std::min(longitud_minima, g.texto.size());
            longitud_maxima = std::max(longitud_maxima, g.texto.size());
        }
    }
};

const TablaReservadas kReservadas;

// ── Signos ──────────────────────────────────────────────────────────────────
//
// Los signos se guardan contiguos, agrupados por su primer byte y, dentro de
// cada grupo, del más largo al más corto: la primera coincidencia es la más
// larga.

struct TablaSignos {
    std::vector<Grafia> signos;
    std::array<std::uint16_t, 257> inicio{};  // el grupo del byte b es [inicio[b], inicio[b+1])

    TablaSignos() {
        for (const Grafia& g : kGrafias) {
            if (info(g.tipo).categoria == Categoria::Signo) signos.push_back(g);
        }
        std::stable_sort(signos.begin(), signos.end(), [](const Grafia& a, const Grafia& b) {
            const auto pa = static_cast<unsigned char>(a.texto.front());
            const auto pb = static_cast<unsigned char>(b.texto.front());
            return pa != pb ? pa < pb : a.texto.size() > b.texto.size();
        });
        std::size_t k = 0;
        for (std::size_t b = 0; b < 256; ++b) {
            inicio[b] = static_cast<std::uint16_t>(k);
            while (k < signos.size() && static_cast<unsigned char>(signos[k].texto.front()) == b) ++k;
        }
        inicio[256] = static_cast<std::uint16_t>(k);
    }
};

const TablaSignos kSignos;

}  // namespace

std::string_view nombre(TipoToken tipo) noexcept {
    return info(tipo).nombre;
}

std::string_view nombre(Categoria categoria) noexcept {
    switch (categoria) {
        case Categoria::Especial: return "especial";
        case Categoria::Identificador: return "identificador";
        case Categoria::Literal: return "literal";
        case Categoria::Comentario: return "comentario";
        case Categoria::Reservada: return "reservada";
        case Categoria::Signo: return "signo";
    }
    return {};
}

Categoria categoria(TipoToken tipo) noexcept {
    return info(tipo).categoria;
}

std::string_view grafia(TipoToken tipo) noexcept {
    return info(tipo).grafia;
}

std::string describir(TipoToken tipo) {
    const InfoTipo& i = info(tipo);
    switch (i.categoria) {
        case Categoria::Reservada:
            return "palabra reservada «" + std::string(i.grafia) + "»";
        case Categoria::Signo:
            return "«" + std::string(i.grafia) + "»";
        default:
            return std::string(i.descripcion);
    }
}

ListaGrafias grafias() noexcept {
    return {std::begin(kGrafias), std::end(kGrafias)};
}

TipoToken buscar_reservada(const char* palabra, std::size_t longitud,
                           std::uint64_t dispersion) noexcept {
    const TablaReservadas& t = kReservadas;
    if (longitud < t.longitud_minima || longitud > t.longitud_maxima) return TipoToken::Identificador;
    for (std::size_t i = ranura_inicial(dispersion, kRanurasReservadas - 1);
         !t.ranuras[i].texto.empty(); i = (i + 1) & (kRanurasReservadas - 1)) {
        const TablaReservadas::Ranura& r = t.ranuras[i];
        if (r.dispersion == dispersion && r.texto.size() == longitud &&
            std::memcmp(r.texto.data(), palabra, longitud) == 0) {
            return r.tipo;
        }
    }
    return TipoToken::Identificador;
}

TipoToken casar_signo(const char* p, const char* fin, std::size_t& longitud) noexcept {
    const TablaSignos& t = kSignos;
    const auto primero = static_cast<unsigned char>(*p);
    const auto disponible = static_cast<std::size_t>(fin - p);
    const Grafia* g = t.signos.data() + t.inicio[primero];
    const Grafia* ultimo = t.signos.data() + t.inicio[primero + 1];
    for (; g != ultimo; ++g) {
        const std::size_t n = g->texto.size();
        if (n > disponible) continue;
        // El primer byte ya coincide por construcción del grupo.
        std::size_t i = 1;
        while (i < n && p[i] == g->texto[i]) ++i;
        if (i == n) {
            longitud = n;
            return g->tipo;
        }
    }
    return TipoToken::Error;
}

}  // namespace almalili
