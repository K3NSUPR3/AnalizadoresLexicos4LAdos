#include "almalili/diagnostico.hpp"

#include <algorithm>
#include <ostream>
#include <utility>

#include "almalili/unicode.hpp"
#include "almalili/utf8.hpp"

namespace almalili {

namespace {

constexpr std::string_view kReinicio = "\x1b[0m";
constexpr std::string_view kNegrita = "\x1b[1m";
constexpr std::string_view kAzul = "\x1b[1;34m";

std::string_view color_de(Gravedad g) noexcept {
    switch (g) {
        case Gravedad::Error: return "\x1b[1;31m";
        case Gravedad::Advertencia: return "\x1b[1;33m";
        case Gravedad::Nota: return "\x1b[1;36m";
    }
    return {};
}

// Prepara una línea de código para mostrarla: los bytes inválidos y los
// caracteres de control (incluidos los bidireccionales, que reordenarían la
// salida de la terminal) se cambian por U+FFFD. `relleno` y `marcas` forman el
// renglón que subraya el tramo [desde, hasta) medido en bytes de la línea.
struct LineaMostrada {
    std::string texto;
    std::string relleno;
    std::string marcas;
};

LineaMostrada preparar_linea(std::string_view linea, std::size_t desde, std::size_t hasta) {
    LineaMostrada r;
    const char* base = linea.data();
    const char* fin = base + linea.size();
    for (const char* p = base; p < fin;) {
        const utf8::Decodificado d = utf8::decodificar(p, fin);
        const auto i = static_cast<std::size_t>(p - base);
        const bool tabulador = d.valido && d.punto == '\t';
        const bool oculto = !d.valido || (d.punto < 0x20 && !tabulador) || d.punto == 0x7F ||
                            unicode::es_control_bidi(d.punto);
        if (oculto) {
            utf8::anexar(r.texto, utf8::kReemplazo);
        } else {
            r.texto.append(p, d.longitud);
        }
        if (i < desde) {
            r.relleno.push_back(tabulador ? '\t' : ' ');
        } else if (i < hasta) {
            r.marcas.push_back('^');
        }
        p += d.longitud;
    }
    if (r.marcas.empty()) r.marcas = "^";
    return r;
}

std::string plural(std::size_t n, std::string_view singular, std::string_view plural_) {
    return std::to_string(n) + " " + std::string(n == 1 ? singular : plural_);
}

}  // namespace

std::string_view nombre(Gravedad g) noexcept {
    switch (g) {
        case Gravedad::Error: return "error";
        case Gravedad::Advertencia: return "advertencia";
        case Gravedad::Nota: return "nota";
    }
    return {};
}

void Diagnosticos::reportar(Gravedad gravedad, std::string_view codigo, Posicion pos,
                            std::uint32_t longitud, std::string mensaje, std::string ayuda) {
    if (gravedad == Gravedad::Error) ++errores_;
    if (gravedad == Gravedad::Advertencia) ++advertencias_;
    if (lista_.size() >= kMaximoAlmacenados) return;
    lista_.push_back({gravedad, std::string(codigo), pos, longitud, std::move(mensaje), std::move(ayuda)});
}

void Diagnosticos::imprimir(std::ostream& salida, const Fuente& fuente, bool color) const {
    const auto pintar = [color](std::string_view codigo) { return color ? codigo : std::string_view{}; };

    for (const Diagnostico& d : lista_) {
        const std::string numero = std::to_string(d.pos.linea);
        const std::string margen(numero.size(), ' ');

        salida << pintar(color_de(d.gravedad)) << nombre(d.gravedad) << '[' << d.codigo << ']'
               << pintar(kReinicio) << pintar(kNegrita) << ": " << d.mensaje << pintar(kReinicio) << '\n';
        salida << margen << pintar(kAzul) << "--> " << pintar(kReinicio) << fuente.nombre() << ':'
               << d.pos.linea << ':' << d.pos.columna << '\n';

        const std::string_view linea = fuente.linea(d.pos.linea);
        const std::uint32_t inicio = fuente.inicio_linea(d.pos.linea);
        const std::size_t desde = d.pos.desplazamiento >= inicio ? d.pos.desplazamiento - inicio : 0;
        const std::size_t hasta = desde + std::max<std::uint32_t>(d.longitud, 1);
        const LineaMostrada m = preparar_linea(linea, desde, hasta);

        salida << margen << pintar(kAzul) << " |" << pintar(kReinicio) << '\n';
        salida << pintar(kAzul) << numero << " | " << pintar(kReinicio) << m.texto << '\n';
        salida << margen << pintar(kAzul) << " | " << pintar(kReinicio) << m.relleno
               << pintar(color_de(d.gravedad)) << m.marcas << pintar(kReinicio) << '\n';
        if (!d.ayuda.empty()) {
            salida << margen << pintar(kAzul) << " = " << pintar(kReinicio) << pintar(kNegrita)
                   << "ayuda: " << pintar(kReinicio) << d.ayuda << '\n';
        }
        salida << '\n';
    }

    const std::size_t total = errores_ + advertencias_;
    if (total > lista_.size() && lista_.size() >= kMaximoAlmacenados) {
        salida << "… y " << (total - lista_.size()) << " diagnósticos más que no se muestran\n\n";
    }
}

std::string Diagnosticos::resumen() const {
    std::string r;
    if (errores_ != 0) r += plural(errores_, "error", "errores");
    if (advertencias_ != 0) {
        if (!r.empty()) r += ", ";
        r += plural(advertencias_, "advertencia", "advertencias");
    }
    return r;
}

}  // namespace almalili
