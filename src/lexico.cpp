#include "almalili/lexico.hpp"

#include <array>
#include <charconv>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <utility>

#include "almalili/unicode.hpp"

namespace almalili {

namespace {

// ── Clasificación de bytes ──────────────────────────────────────────────────

enum Clase : std::uint8_t {
    kOtro,
    kEspacio,    // espacio, tabulador, tabulador vertical, avance de página
    kSalto,      // LF o CR
    kLetra,      // a-z, A-Z, _
    kDigito,     // 0-9
    kComillas,   // "
    kApostrofo,  // '
    kNoAscii,    // byte >= 0x80
};

constexpr std::array<std::uint8_t, 256> construir_clases() {
    std::array<std::uint8_t, 256> t{};
    for (std::size_t c = 0; c < 256; ++c) {
        std::uint8_t k = kOtro;
        if (c >= 0x80) k = kNoAscii;
        else if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_') k = kLetra;
        else if (c >= '0' && c <= '9') k = kDigito;
        else if (c == ' ' || c == '\t' || c == '\v' || c == '\f') k = kEspacio;
        else if (c == '\n' || c == '\r') k = kSalto;
        else if (c == '"') k = kComillas;
        else if (c == '\'') k = kApostrofo;
        t[c] = k;
    }
    return t;
}

constexpr std::array<std::uint8_t, 256> kClases = construir_clases();

inline std::uint8_t clase(char c) noexcept {
    return kClases[static_cast<unsigned char>(c)];
}

inline bool continua_identificador_ascii(char c) noexcept {
    const std::uint8_t k = clase(c);
    return k == kLetra || k == kDigito;
}

inline bool es_digito(char c) noexcept {
    return c >= '0' && c <= '9';
}

// Valor de cada byte como dígito en bases hasta 16; 0xFF si no es un dígito.
constexpr std::array<std::uint8_t, 256> construir_valores_digito() {
    std::array<std::uint8_t, 256> t{};
    for (std::size_t c = 0; c < 256; ++c) {
        std::uint8_t v = 0xFF;
        if (c >= '0' && c <= '9') v = static_cast<std::uint8_t>(c - '0');
        else if (c >= 'a' && c <= 'f') v = static_cast<std::uint8_t>(c - 'a' + 10);
        else if (c >= 'A' && c <= 'F') v = static_cast<std::uint8_t>(c - 'A' + 10);
        t[c] = v;
    }
    return t;
}

constexpr std::array<std::uint8_t, 256> kValorDigito = construir_valores_digito();

// Valor del dígito en bases hasta 16, o -1 si no es un dígito.
inline int valor_digito(char c) noexcept {
    const std::uint8_t v = kValorDigito[static_cast<unsigned char>(c)];
    return v == 0xFF ? -1 : v;
}

// Bytes que dentro de una cadena no requieren ningún tratamiento: todo ASCII
// salvo las comillas rectas, la diagonal invertida, los saltos de línea y el
// '\0' (que puede ser el centinela del final).
constexpr std::array<bool, 256> construir_corrientes() {
    std::array<bool, 256> t{};
    for (std::size_t c = 1; c < 0x80; ++c) {
        t[c] = c != '"' && c != '\\' && c != '\n' && c != '\r';
    }
    return t;
}

constexpr std::array<bool, 256> kCorrienteEnCadena = construir_corrientes();

// ── Delimitadores de cadena ─────────────────────────────────────────────────

constexpr std::string_view kComillasRectas = "\"";
constexpr std::string_view kComillasTriples = "\"\"\"";
constexpr std::string_view kAngularAbre = "\xC2\xAB";          // «
constexpr std::string_view kAngularCierra = "\xC2\xBB";        // »
constexpr std::string_view kTipograficaAbre = "\xE2\x80\x9C";  // “
constexpr std::string_view kTipograficaCierra = "\xE2\x80\x9D";  // ”

// ── Texto para mensajes ─────────────────────────────────────────────────────

std::string codigo_unicode(char32_t c) {
    char bufer[16];
    std::snprintf(bufer, sizeof bufer, "U+%04X", static_cast<unsigned>(c));
    return bufer;
}

// «ñ» (U+00F1), o solo el código si el carácter no se puede mostrar.
std::string describir_caracter(char32_t c) {
    const bool mostrable = c > 0x20 && c != 0x7F && !(c >= 0x80 && c < 0xA0) &&
                           !unicode::es_control_bidi(c) && !unicode::es_invisible(c) &&
                           !unicode::es_espacio(c);
    if (!mostrable) return codigo_unicode(c);
    std::string r = "«";
    utf8::anexar(r, c);
    r += "» (" + codigo_unicode(c) + ")";
    return r;
}

std::string bytes_en_hex(const char* p, std::size_t n) {
    std::string r;
    char bufer[8];
    for (std::size_t i = 0; i < n; ++i) {
        std::snprintf(bufer, sizeof bufer, "%s0x%02X", i ? " " : "", static_cast<unsigned char>(p[i]));
        r += bufer;
    }
    return r;
}

}  // namespace

// ── Construcción y utilidades ───────────────────────────────────────────────

Lexico::Lexico(const Fuente& fuente, Diagnosticos& diagnosticos, TablaSimbolos& simbolos,
               OpcionesLexico opciones)
    : diagnosticos_(diagnosticos),
      simbolos_(simbolos),
      opciones_(opciones),
      inicio_(fuente.texto().data()),
      fin_(inicio_ + fuente.texto().size()),
      p_(inicio_),
      inicio_linea_(inicio_),
      marca_columna_(inicio_) {}

// Nota de rendimiento: en el camino que se recorre por cada token se evita
// pasar o devolver por valor estructuras pequeñas (string_view, Posicion). MSVC
// las arma en la pila con varias escrituras y luego las copia con una sola
// lectura más ancha, y esa combinación impide el reenvío de escritura a lectura
// del procesador. Por eso `ubicar` escribe campo por campo en su destino y las
// búsquedas reciben puntero y longitud.

void Lexico::ubicar(const char* q, Posicion& destino) noexcept {
    if (q < marca_columna_) {
        marca_columna_ = inicio_linea_;
        columna_marca_ = 1;
    }
    columna_marca_ += static_cast<std::uint32_t>(utf8::contar(marca_columna_, q));
    marca_columna_ = q;
    destino.desplazamiento = static_cast<std::uint32_t>(q - inicio_);
    destino.linea = linea_;
    destino.columna = columna_marca_;
}

Posicion Lexico::posicion_en(const char* q) noexcept {
    Posicion pos;
    ubicar(q, pos);
    return pos;
}

void Lexico::registrar_salto(const char* despues) noexcept {
    ++linea_;
    inicio_linea_ = despues;
    marca_columna_ = despues;
    columna_marca_ = 1;
}

void Lexico::error(std::string_view codigo, Posicion pos, std::size_t longitud, std::string mensaje,
                   std::string ayuda) {
    diagnosticos_.reportar(Gravedad::Error, codigo, pos, static_cast<std::uint32_t>(longitud),
                           std::move(mensaje), std::move(ayuda));
}

void Lexico::advertencia(std::string_view codigo, Posicion pos, std::size_t longitud,
                         std::string mensaje, std::string ayuda) {
    diagnosticos_.reportar(Gravedad::Advertencia, codigo, pos, static_cast<std::uint32_t>(longitud),
                           std::move(mensaje), std::move(ayuda));
}

bool Lexico::empieza_con(std::string_view prefijo) const noexcept {
    return static_cast<std::size_t>(fin_ - p_) >= prefijo.size() &&
           std::memcmp(p_, prefijo.data(), prefijo.size()) == 0;
}

// Valida la unidad no ASCII que está en `q` dentro de un comentario o de un
// literal, donde cualquier carácter es admisible pero los bytes inválidos y los
// controles bidireccionales no deben pasar inadvertidos.
utf8::Decodificado Lexico::revisar_no_ascii(const char* q) {
    const utf8::Decodificado d = utf8::decodificar(q, fin_);
    if (!d.valido) {
        std::string ayuda;
        if (!aviso_codificacion_) {
            aviso_codificacion_ = true;
            ayuda = "el archivo debe estar en UTF-8; si se guardó como Windows-1252 o Latin-1 "
                    "(«ANSI»), vuelve a guardarlo como UTF-8";
        }
        error("L0001", posicion_en(q), d.longitud,
              "secuencia de bytes inválida en UTF-8 (" + bytes_en_hex(q, d.longitud) + ")",
              std::move(ayuda));
    } else if (unicode::es_control_bidi(d.punto)) {
        advertencia("L0009", posicion_en(q), d.longitud,
                    "control de formato bidireccional " + codigo_unicode(d.punto) +
                        ": puede hacer que el código se vea distinto de como se ejecuta",
                    "bórralo o, dentro de una cadena, escríbelo como «\\u{" +
                        codigo_unicode(d.punto).substr(2) + "}»");
    }
    return d;
}

// ── Blancos y comentarios ───────────────────────────────────────────────────

// En los bucles que recorren el texto byte a byte el cursor se lleva en una
// variable local y solo al final se guarda en p_: así el compilador lo mantiene
// en un registro en vez de leerlo y escribirlo en memoria en cada paso.

void Lexico::saltar_comentario_linea() {
    const char* p = p_ + 2;
    while (true) {
        const auto c = static_cast<unsigned char>(*p);
        if (c > '\r' && c < 0x80) {
            ++p;
        } else if (c < 0x80) {
            if (c == '\n' || c == '\r' || p >= fin_) break;
            ++p;
        } else {
            if (utf8::longitud_salto(p, fin_) != 0) break;
            p += revisar_no_ascii(p).longitud;
        }
    }
    p_ = p;
}

// Los comentarios de bloque se pueden anidar: /* a /* b */ c */.
void Lexico::saltar_comentario_bloque(Posicion inicio) {
    const char* p = p_ + 2;
    int profundidad = 1;
    while (true) {
        const char c = *p;
        if (c == '*' && p[1] == '/') {
            p += 2;
            if (--profundidad == 0) break;
        } else if (c == '/' && p[1] == '*') {
            p += 2;
            ++profundidad;
        } else if (static_cast<unsigned char>(c) > '\r' && static_cast<unsigned char>(c) < 0x80) {
            ++p;
        } else if (p >= fin_) {
            error("L0004", inicio, 2, "comentario de bloque sin cerrar",
                  "agrega «*/» para cerrarlo; recuerda que los comentarios de bloque se anidan");
            break;
        } else if (const std::size_t n = utf8::longitud_salto(p, fin_)) {
            p += n;
            registrar_salto(p);
        } else if (static_cast<unsigned char>(c) >= 0x80) {
            p += revisar_no_ascii(p).longitud;
        } else {
            ++p;
        }
    }
    p_ = p;
}

// Avanza sobre blancos y comentarios. Devuelve true si, por estar activa la
// opción de conservarlos, dejó un comentario en `comentario`.
bool Lexico::saltar_relleno(Token& comentario, bool& inicia_linea, bool& espacio) {
    const char* p = p_;
    while (true) {
        // Al llegar al final, el '\0' centinela cae en `default` y termina.
        const char c = *p;
        switch (clase(c)) {
            case kEspacio:
                do {
                    ++p;
                } while (clase(*p) == kEspacio);
                espacio = true;
                continue;

            case kSalto:
                p += utf8::longitud_salto(p, fin_);
                registrar_salto(p);
                inicia_linea = true;
                espacio = true;
                continue;

            case kNoAscii: {
                const utf8::Decodificado d = utf8::decodificar(p, fin_);
                if (d.valido && unicode::es_espacio(d.punto)) {
                    p += d.longitud;
                    espacio = true;
                    continue;
                }
                if (d.valido && unicode::es_salto_linea(d.punto)) {
                    p += d.longitud;
                    registrar_salto(p);
                    inicia_linea = true;
                    espacio = true;
                    continue;
                }
                if (d.valido && d.punto == 0xFEFF) {
                    advertencia("L0010", posicion_en(p), d.longitud,
                                "marca de orden de bytes (BOM) a mitad del archivo",
                                "suele aparecer al unir archivos; bórrala");
                    p += d.longitud;
                    continue;
                }
                p_ = p;
                return false;
            }

            default: {
                p_ = p;
                if (c != '/' || (p[1] != '/' && p[1] != '*')) return false;
                const char* inicio = p_;
                const Posicion pos = posicion_en(inicio);
                TipoToken tipo = TipoToken::Comentario;
                if (p_[1] == '/') {
                    // «///» es documentación; «////» ya es un separador.
                    if (p_[2] == '/' && p_[3] != '/') tipo = TipoToken::ComentarioDoc;
                    saltar_comentario_linea();
                } else {
                    // «/**» es documentación; «/***» y «/**/» no.
                    if (p_[2] == '*' && p_[3] != '*' && p_[3] != '/') tipo = TipoToken::ComentarioDoc;
                    saltar_comentario_bloque(pos);
                }
                if (opciones_.conservar_comentarios) {
                    comentario.tipo = tipo;
                    comentario.pos = pos;
                    comentario.lexema = {inicio, static_cast<std::size_t>(p_ - inicio)};
                    comentario.longitud = static_cast<std::uint32_t>(comentario.lexema.size());
                    return true;
                }
                p = p_;
                espacio = true;
                continue;
            }
        }
    }
}

// ── Punto de entrada ────────────────────────────────────────────────────────

void Lexico::siguiente(Token& t) {
    t.tipo = TipoToken::FinArchivo;
    t.simbolo = kSinSimbolo;
    t.longitud = 0;
    t.lexema = {};
    t.texto = {};
    t.entero = 0;

    bool inicia_linea = (p_ == inicio_);
    bool espacio = false;
    // La mayoría de los tokens van pegados al anterior o tras un solo espacio:
    // esos casos se resuelven aquí sin llamar a saltar_relleno.
    if (*p_ == ' ') {
        ++p_;
        espacio = true;
    }
    const std::uint8_t k = clase(*p_);
    const bool hay_relleno = k == kEspacio || k == kSalto || k == kNoAscii || *p_ == '/';
    const bool es_comentario = hay_relleno && saltar_relleno(t, inicia_linea, espacio);
    t.inicia_linea = inicia_linea;
    t.precedido_de_espacio = espacio;
    if (es_comentario) return;

    const char* inicio = p_;
    ubicar(inicio, t.pos);
    if (p_ >= fin_) return;

    switch (clase(*p_)) {
        case kLetra:
            leer_identificador(t);
            break;
        case kDigito:
            leer_numero(t);
            break;
        case kComillas:
            if (empieza_con(kComillasTriples)) {
                leer_cadena(t, kComillasTriples, kComillasTriples, true, false);
            } else {
                leer_cadena(t, kComillasRectas, kComillasRectas, false, false);
            }
            break;
        case kApostrofo:
            leer_caracter(t);
            break;
        case kNoAscii:
            leer_no_ascii(t);
            break;
        default:
            leer_signo(t);
            break;
    }

    t.lexema = {inicio, static_cast<std::size_t>(p_ - inicio)};
    t.longitud = static_cast<std::uint32_t>(p_ - inicio);
}

std::vector<Token> Lexico::analizar_todo() {
    std::vector<Token> tokens;
    tokens.reserve(static_cast<std::size_t>(fin_ - p_) / 5 + 1);
    // Cada token se construye directamente dentro del vector.
    do {
        siguiente(tokens.emplace_back());
    } while (tokens.back().tipo != TipoToken::FinArchivo);
    return tokens;
}

// ── Identificadores y palabras reservadas ───────────────────────────────────

void Lexico::leer_identificador(Token& t) {
    const char* inicio = p_;
    const char* p = p_;
    // Las marcas combinantes empiezan en U+0300: si no aparece ninguna, el
    // nombre ya está en su forma normal y no hay nada que componer.
    bool hay_marcas = false;
    while (true) {
        // El '\0' centinela que sigue al texto detiene este bucle.
        while (continua_identificador_ascii(*p)) ++p;
        if (static_cast<unsigned char>(*p) < 0x80) break;
        const utf8::Decodificado d = utf8::decodificar(p, fin_);
        if (!d.valido || !unicode::es_continuacion_identificador(d.punto)) break;
        p += d.longitud;
        hay_marcas |= d.punto >= 0x0300;
    }
    p_ = p;

    const char* nombre = inicio;
    auto longitud = static_cast<std::size_t>(p - inicio);
    if (hay_marcas) {
        const std::string_view normal = normalizar({inicio, longitud});
        nombre = normal.data();
        longitud = normal.size();
    }

    // La dispersión se calcula una vez y sirve para las dos tablas.
    const std::uint64_t dispersion = dispersar({nombre, longitud});

    t.tipo = buscar_reservada(nombre, longitud, dispersion);
    if (t.tipo == TipoToken::Identificador) {
        t.simbolo = simbolos_.registrar(nombre, longitud, t.pos, dispersion);
        t.texto = simbolos_[t.simbolo].nombre;
    }
}

// Une cada letra con la marca combinante que la sigue cuando existe la forma
// precompuesta, para que «á» escrita como a + U+0301 sea el mismo identificador
// que «á» escrita con un solo carácter. Coincide con la forma NFC en el caso
// habitual de una marca por letra; no reordena marcas múltiples.
std::string_view Lexico::normalizar(std::string_view nombre) {
    const char* p = nombre.data();
    const char* fin = p + nombre.size();
    temporal_.clear();
    char32_t previo = 0;
    std::size_t inicio_previo = 0;
    bool cambio = false;
    while (p < fin) {
        const utf8::Decodificado d = utf8::decodificar(p, fin);
        p += d.longitud;
        const char32_t compuesto = previo != 0 ? unicode::componer(previo, d.punto) : 0;
        if (compuesto != 0) {
            temporal_.resize(inicio_previo);
            utf8::anexar(temporal_, compuesto);
            previo = compuesto;
            cambio = true;
        } else {
            inicio_previo = temporal_.size();
            utf8::anexar(temporal_, d.punto);
            previo = d.punto;
        }
    }
    return cambio ? simbolos_.internar(temporal_) : nombre;
}

// ── Números ─────────────────────────────────────────────────────────────────

void Lexico::leer_numero(Token& t) {
    constexpr std::uint64_t kMaximo = std::numeric_limits<std::uint64_t>::max();
    const char* inicio = p_;
    const char* p = p_;
    bool separador_mal_puesto = false;

    // Consume dígitos de la base indicada y separadores «_», acumula su valor
    // en `acumulado` (marcando `excede` si no cabe en 64 bits) y devuelve
    // cuántos dígitos fueron.
    const auto leer_digitos = [&](std::uint64_t base, std::uint64_t& acumulado, bool& excede) {
        const std::uint64_t limite = kMaximo / base;
        const std::uint64_t resto = kMaximo % base;
        std::size_t cantidad = 0;
        bool tras_separador = false;
        for (;; ++p) {
            const std::uint64_t d = kValorDigito[static_cast<unsigned char>(*p)];
            if (d < base) {
                if (acumulado > limite || (acumulado == limite && d > resto)) excede = true;
                acumulado = acumulado * base + d;
                ++cantidad;
                tras_separador = false;
            } else if (*p == '_') {
                if (cantidad == 0 || tras_separador) separador_mal_puesto = true;
                tras_separador = true;
            } else {
                break;
            }
        }
        if (tras_separador) separador_mal_puesto = true;
        return cantidad;
    };

    std::uint64_t base = 10;
    if (*p == '0') {
        switch (p[1]) {
            case 'x': case 'X': base = 16; break;
            case 'b': case 'B': base = 2; break;
            case 'o': case 'O': base = 8; break;
            default: break;
        }
    }

    // En un real, `valor` reúne los dígitos de la parte entera y de la
    // fraccionaria: es la mantisa decimal.
    std::uint64_t valor = 0;
    bool desborda = false;
    std::size_t digitos_fraccion = 0;
    std::uint64_t exponente = 0;
    bool exponente_negativo = false;
    bool exponente_desborda = false;
    bool es_real = false;
    bool valido = true;

    if (base != 10) {
        p += 2;
        if (leer_digitos(base, valor, desborda) == 0) {
            valido = false;
            error("L0007", t.pos, static_cast<std::size_t>(p - inicio),
                  "faltan dígitos después del prefijo «" + std::string(inicio, 2) + "»");
        }
    } else {
        leer_digitos(10, valor, desborda);
        // El punto solo pertenece al número si le sigue un dígito; así «1..5»
        // es un rango y «1.método» es un acceso a miembro.
        if (*p == '.' && es_digito(p[1])) {
            ++p;
            digitos_fraccion = leer_digitos(10, valor, desborda);
            es_real = true;
        }
        if (*p == 'e' || *p == 'E') {
            const char* q = p + 1;
            if (*q == '+' || *q == '-') ++q;
            if (es_digito(*q)) {
                exponente_negativo = (p[1] == '-');
                p = q;
                leer_digitos(10, exponente, exponente_desborda);
                es_real = true;
            }
        }
    }

    // Letras o dígitos pegados al número: «12abc», «0b102», «0xFG».
    const char* sufijo = p;
    while (true) {
        while (continua_identificador_ascii(*p)) ++p;
        if (static_cast<unsigned char>(*p) < 0x80) break;
        const utf8::Decodificado d = utf8::decodificar(p, fin_);
        if (!d.valido || !unicode::es_continuacion_identificador(d.punto)) break;
        p += d.longitud;
    }
    p_ = p;
    const auto largo = static_cast<std::size_t>(p - inicio);

    if (p != sufijo) {
        // Si ya se reportó que faltan dígitos, un segundo error sobre lo mismo
        // solo estorba.
        if (valido) {
            const Posicion pos = posicion_en(sufijo);
            error("L0007", pos, static_cast<std::size_t>(p - sufijo),
                  "«" + std::string(sufijo, static_cast<std::size_t>(p - sufijo)) +
                      "» no puede ir pegado a un literal numérico",
                  base == 10 ? "separa el número del nombre con un espacio o un operador"
                             : "revisa que todos los dígitos sean válidos en la base del prefijo");
        }
        valido = false;
    } else if (separador_mal_puesto) {
        valido = false;
        error("L0007", t.pos, largo, "separador «_» mal colocado en el literal numérico",
              "el guion bajo solo puede ir entre dos dígitos, como en «1_000_000»");
    }

    if (!es_real) {
        t.tipo = TipoToken::LiteralEntero;
        if (!valido) return;
        if (desborda) {
            error("L0008", t.pos, largo, "el literal entero no cabe en 64 bits",
                  "el valor máximo es 18446744073709551615");
            return;
        }
        t.entero = valor;
        return;
    }

    t.tipo = TipoToken::LiteralReal;
    t.real = 0.0;
    if (!valido) return;

    // Ruta rápida (Clinger): si la mantisa cabe exacta en un double (hasta
    // 2^53) y la potencia de diez también (hasta 10^22), una sola
    // multiplicación o división da el resultado correctamente redondeado. Casi
    // todos los literales de un programa caen aquí.
    static constexpr double kPotenciasDeDiez[] = {1e0,  1e1,  1e2,  1e3,  1e4,  1e5,  1e6,  1e7,
                                                  1e8,  1e9,  1e10, 1e11, 1e12, 1e13, 1e14, 1e15,
                                                  1e16, 1e17, 1e18, 1e19, 1e20, 1e21, 1e22};
    constexpr std::uint64_t kMantisaExacta = std::uint64_t{1} << 53;
    if (!desborda && valor <= kMantisaExacta && !exponente_desborda && exponente <= 1000 &&
        digitos_fraccion <= 1000) {
        const long long e10 = (exponente_negativo ? -static_cast<long long>(exponente)
                                                  : static_cast<long long>(exponente)) -
                              static_cast<long long>(digitos_fraccion);
        if (e10 >= -22 && e10 <= 22) {
            const auto mantisa = static_cast<double>(valor);
            t.real = e10 < 0 ? mantisa / kPotenciasDeDiez[-e10] : mantisa * kPotenciasDeDiez[e10];
            return;
        }
    }

    // Ruta general: se entrega el literal, sin separadores, a la biblioteca.
    temporal_.clear();
    for (const char* q = inicio; q < p; ++q) {
        if (*q != '_') temporal_.push_back(*q);
    }
    // strtod depende de la configuración regional, pero el compilador nunca la
    // cambia de la «C» inicial; aun así se prefiere from_chars, que es más
    // rápida, y strtod queda para distinguir un desbordamiento (infinito) de
    // un valor demasiado pequeño (cero).
#if defined(__cpp_lib_to_chars) && __cpp_lib_to_chars >= 201611L
    double v = 0.0;
    const auto r = std::from_chars(temporal_.data(), temporal_.data() + temporal_.size(), v);
    if (r.ec == std::errc::result_out_of_range) v = std::strtod(temporal_.c_str(), nullptr);
#else
    const double v = std::strtod(temporal_.c_str(), nullptr);
#endif
    if (std::isinf(v)) {
        error("L0008", t.pos, largo, "el literal real es demasiado grande para un número de 64 bits");
        return;
    }
    t.real = v;
}

// ── Cadenas y caracteres ────────────────────────────────────────────────────

// Lee la secuencia de escape que comienza en la diagonal invertida y anexa su
// valor a `salida`.
void Lexico::leer_escape(std::string& salida) {
    const char* inicio = p_;
    ++p_;
    if (p_ >= fin_ || utf8::longitud_salto(p_, fin_) != 0) {
        error("L0005", posicion_en(inicio), 1, "diagonal invertida «\\» al final de la línea",
              "para escribir una diagonal invertida usa «\\\\»");
        return;
    }

    const char c = *p_;
    switch (c) {
        case 'n': salida.push_back('\n'); ++p_; return;
        case 't': salida.push_back('\t'); ++p_; return;
        case 'r': salida.push_back('\r'); ++p_; return;
        case '0': salida.push_back('\0'); ++p_; return;
        case '\\': case '"': case '\'':
            salida.push_back(c);
            ++p_;
            return;

        case 'x': {
            ++p_;
            int valor = 0;
            int digitos = 0;
            while (digitos < 2 && valor_digito(*p_) >= 0) {
                valor = valor * 16 + valor_digito(*p_);
                ++p_;
                ++digitos;
            }
            if (digitos != 2) {
                error("L0005", posicion_en(inicio), static_cast<std::size_t>(p_ - inicio),
                      "«\\x» debe ir seguido de dos dígitos hexadecimales");
            } else if (valor > 0x7F) {
                error("L0005", posicion_en(inicio), static_cast<std::size_t>(p_ - inicio),
                      "«\\x» solo admite valores de 00 a 7F",
                      "para un carácter fuera de ASCII usa «\\u{…}», por ejemplo «\\u{F1}» es «ñ»");
            } else {
                salida.push_back(static_cast<char>(valor));
            }
            return;
        }

        case 'u': {
            ++p_;
            if (*p_ != '{') {
                error("L0005", posicion_en(inicio), static_cast<std::size_t>(p_ - inicio),
                      "se esperaba «{» después de «\\u»", "por ejemplo, «\\u{F1}» es «ñ»");
                return;
            }
            ++p_;
            std::uint32_t valor = 0;
            int digitos = 0;
            while (valor_digito(*p_) >= 0) {
                if (digitos < 8) valor = valor * 16 + static_cast<std::uint32_t>(valor_digito(*p_));
                ++digitos;
                ++p_;
            }
            const bool cerrado = (*p_ == '}');
            if (cerrado) ++p_;
            const auto largo = static_cast<std::size_t>(p_ - inicio);
            if (!cerrado) {
                error("L0005", posicion_en(inicio), largo, "falta «}» para cerrar el escape «\\u{»");
            } else if (digitos == 0 || digitos > 6 || !utf8::es_escalar(valor)) {
                error("L0005", posicion_en(inicio), largo,
                      "«" + std::string(inicio, largo) + "» no es un punto de código Unicode válido",
                      "se admiten de 1 a 6 dígitos hexadecimales, hasta 10FFFF y sin el rango D800–DFFF");
            } else {
                utf8::anexar(salida, valor);
            }
            return;
        }

        default:
            break;
    }

    // Las comillas no ASCII se pueden escapar para incluirlas en su propia cadena.
    const utf8::Decodificado d = utf8::decodificar(p_, fin_);
    p_ += d.longitud;
    if (d.valido && (d.punto == 0x00AB || d.punto == 0x00BB || d.punto == 0x201C || d.punto == 0x201D)) {
        utf8::anexar(salida, d.punto);
        return;
    }
    error("L0005", posicion_en(inicio), static_cast<std::size_t>(p_ - inicio),
          "secuencia de escape desconocida «" + utf8::sanear({inicio, static_cast<std::size_t>(p_ - inicio)}) + "»",
          "las válidas son \\n \\t \\r \\0 \\\\ \\\" \\' \\xHH y \\u{H…}");
    utf8::anexar(salida, d.punto);
}

// Lee una cadena delimitada por `apertura` y `cierre`. Si es `anidable`, cada
// apertura interior exige su propio cierre («uno «dos» tres»). Mientras no
// aparezca un escape, el valor es una vista directa al texto fuente.
void Lexico::leer_cadena(Token& t, std::string_view apertura, std::string_view cierre,
                         bool multilinea, bool anidable) {
    const char* inicio = p_;
    const char* p = p_ + apertura.size();
    const char* contenido = p;
    const char* fin_contenido = nullptr;
    std::string valor;
    bool usa_valor = false;
    int profundidad = 1;
    t.tipo = TipoToken::LiteralCadena;

    const auto empieza = [&](std::string_view prefijo) {
        return static_cast<std::size_t>(fin_ - p) >= prefijo.size() &&
               std::memcmp(p, prefijo.data(), prefijo.size()) == 0;
    };
    // A partir del primer escape el valor deja de coincidir con la fuente.
    const auto materializar = [&] {
        if (!usa_valor) {
            valor.assign(contenido, static_cast<std::size_t>(p - contenido));
            usa_valor = true;
        }
    };
    const auto sin_cerrar = [&] {
        fin_contenido = p;
        error("L0003", t.pos, static_cast<std::size_t>(p - inicio), "cadena sin cerrar",
              "falta «" + std::string(cierre) + "»" +
                  (multilinea ? "" : "; una cadena no puede continuar en otra línea (para eso usa "
                                     "comillas triples)"));
    };

    while (true) {
        // Los tramos de bytes corrientes se recorren de una sola vez.
        const char* tramo = p;
        while (kCorrienteEnCadena[static_cast<unsigned char>(*p)]) ++p;
        if (usa_valor) valor.append(tramo, static_cast<std::size_t>(p - tramo));

        if (p >= fin_) {
            sin_cerrar();
            break;
        }
        const char c = *p;
        if (c == cierre.front() && empieza(cierre)) {
            if (--profundidad == 0) {
                fin_contenido = p;
                p += cierre.size();
                break;
            }
            if (usa_valor) valor.append(cierre);
            p += cierre.size();
        } else if (anidable && c == apertura.front() && empieza(apertura)) {
            ++profundidad;
            if (usa_valor) valor.append(apertura);
            p += apertura.size();
        } else if (c == '\\') {
            materializar();
            p_ = p;
            leer_escape(valor);
            p = p_;
        } else if (const std::size_t n = utf8::longitud_salto(p, fin_)) {
            if (!multilinea) {
                sin_cerrar();
                break;
            }
            // En el valor todo salto de línea es '\n', sin importar cómo se
            // haya guardado el archivo.
            materializar();
            valor.push_back('\n');
            p += n;
            registrar_salto(p);
        } else if (static_cast<unsigned char>(c) >= 0x80) {
            const utf8::Decodificado d = revisar_no_ascii(p);
            if (!d.valido) {
                materializar();
                utf8::anexar(valor, utf8::kReemplazo);
            } else if (usa_valor) {
                valor.append(p, d.longitud);
            }
            p += d.longitud;
        } else {
            // Comillas rectas dentro de otro tipo de cadena, o un '\0' suelto.
            if (usa_valor) valor.push_back(c);
            ++p;
        }
    }

    p_ = p;
    t.texto = usa_valor ? simbolos_.internar(valor)
                        : std::string_view(contenido, static_cast<std::size_t>(fin_contenido - contenido));
}

void Lexico::leer_caracter(Token& t) {
    const char* inicio = p_;
    ++p_;
    std::string valor;
    bool cerrado = false;
    while (p_ < fin_ && utf8::longitud_salto(p_, fin_) == 0) {
        const char c = *p_;
        if (c == '\'') {
            ++p_;
            cerrado = true;
            break;
        }
        if (c == '\\') {
            leer_escape(valor);
        } else if (static_cast<unsigned char>(c) >= 0x80) {
            const utf8::Decodificado d = revisar_no_ascii(p_);
            utf8::anexar(valor, d.punto);
            p_ += d.longitud;
        } else {
            valor.push_back(c);
            ++p_;
        }
    }

    t.tipo = TipoToken::LiteralCaracter;
    t.caracter = 0;
    const auto largo = static_cast<std::size_t>(p_ - inicio);
    if (!cerrado) {
        error("L0006", t.pos, largo, "literal de carácter sin cerrar", "falta el apóstrofo de cierre «'»");
        return;
    }

    // Una letra seguida de su marca combinante cuenta como un solo carácter si
    // existe la forma precompuesta.
    std::size_t cantidad = 0;
    char32_t primero = 0;
    const char* q = valor.data();
    const char* fin_valor = q + valor.size();
    while (q < fin_valor) {
        const utf8::Decodificado d = utf8::decodificar(q, fin_valor);
        q += d.longitud;
        if (cantidad == 1) {
            if (const char32_t compuesto = unicode::componer(primero, d.punto)) {
                primero = compuesto;
                continue;
            }
        }
        if (cantidad == 0) primero = d.punto;
        ++cantidad;
    }

    if (cantidad == 0) {
        error("L0006", t.pos, largo, "literal de carácter vacío",
              "debe contener exactamente un carácter, como 'a' o 'ñ'");
    } else if (cantidad > 1) {
        error("L0006", t.pos, largo,
              "el literal de carácter contiene " + std::to_string(cantidad) + " caracteres",
              "para texto de más de un carácter usa una cadena entre comillas dobles");
    }
    t.caracter = primero;
}

// ── Signos y caracteres fuera de ASCII ──────────────────────────────────────

void Lexico::leer_signo(Token& t) {
    std::size_t longitud = 0;
    t.tipo = casar_signo(p_, fin_, longitud);
    if (t.tipo != TipoToken::Error) {
        p_ += longitud;
        return;
    }
    caracter_inesperado(t);
}

// Aparte de leer_signo para que el camino normal no cargue con el costo de
// preparar los mensajes.
void Lexico::caracter_inesperado(Token& t) {
    const utf8::Decodificado d = utf8::decodificar(p_, fin_);
    error("L0002", t.pos, d.longitud, "carácter inesperado " + describir_caracter(d.punto),
          std::string(unicode::sugerencia(d.punto)));
    p_ += d.longitud;
}

void Lexico::leer_no_ascii(Token& t) {
    const utf8::Decodificado d = utf8::decodificar(p_, fin_);
    if (!d.valido) {
        t.tipo = TipoToken::Error;
        p_ += revisar_no_ascii(p_).longitud;
        return;
    }
    if (unicode::es_inicio_identificador(d.punto)) {
        leer_identificador(t);
    } else if (d.punto == 0x00AB) {
        leer_cadena(t, kAngularAbre, kAngularCierra, false, true);
    } else if (d.punto == 0x201C) {
        leer_cadena(t, kTipograficaAbre, kTipograficaCierra, false, false);
    } else if (unicode::es_control_bidi(d.punto)) {
        t.tipo = TipoToken::Error;
        error("L0009", t.pos, d.longitud,
              "control de formato bidireccional " + codigo_unicode(d.punto) + " fuera de una cadena o comentario",
              "puede hacer que el código se vea distinto de como se ejecuta; bórralo");
        p_ += d.longitud;
    } else {
        leer_signo(t);
    }
}

}  // namespace almalili
