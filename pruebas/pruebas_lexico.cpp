// Pruebas del analizador léxico. Sin dependencias externas: cada PRUEBA se
// registra sola y main() las ejecuta todas.

#include <cstdint>
#include <iostream>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "almalili/diagnostico.hpp"
#include "almalili/fuente.hpp"
#include "almalili/lexico.hpp"
#include "almalili/tabla_simbolos.hpp"
#include "almalili/token.hpp"
#include "almalili/unicode.hpp"
#include "almalili/utf8.hpp"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#endif

using namespace almalili;
using T = TipoToken;

namespace {

// ── Infraestructura mínima ──────────────────────────────────────────────────

struct Prueba {
    const char* nombre;
    void (*funcion)();
};

std::vector<Prueba>& pruebas() {
    static std::vector<Prueba> lista;
    return lista;
}

struct Registro {
    Registro(const char* nombre, void (*funcion)()) { pruebas().push_back({nombre, funcion}); }
};

int g_verificaciones = 0;
int g_fallos = 0;

#define PRUEBA(nombre)                                \
    void nombre();                                    \
    const Registro registro_##nombre(#nombre, nombre); \
    void nombre()

#define VERIFICAR(condicion)                                                              \
    do {                                                                                  \
        ++g_verificaciones;                                                               \
        if (!(condicion)) {                                                               \
            ++g_fallos;                                                                   \
            std::cerr << __FILE__ << '(' << __LINE__ << "): falló: " #condicion "\n";     \
        }                                                                                 \
    } while (false)

// Análisis completo de un texto, con todo lo que los tokens necesitan vivo.
struct Analisis {
    Fuente fuente;
    Diagnosticos diagnosticos;
    TablaSimbolos simbolos;
    std::vector<Token> tokens;

    explicit Analisis(std::string texto, OpcionesLexico opciones = {})
        : fuente("prueba.alma", std::move(texto)) {
        Lexico lexico(fuente, diagnosticos, simbolos, opciones);
        tokens = lexico.analizar_todo();
    }

    // Tipos de todos los tokens salvo el FinArchivo final.
    std::vector<T> tipos() const {
        std::vector<T> r;
        for (std::size_t i = 0; i + 1 < tokens.size(); ++i) r.push_back(tokens[i].tipo);
        return r;
    }

    const Token& operator[](std::size_t i) const { return tokens.at(i); }

    std::size_t cuenta(std::string_view codigo) const {
        std::size_t n = 0;
        for (const Diagnostico& d : diagnosticos.lista()) n += (d.codigo == codigo);
        return n;
    }

    bool limpio() const { return diagnosticos.lista().empty(); }
};

using Tipos = std::vector<T>;

// ── UTF-8 ───────────────────────────────────────────────────────────────────

PRUEBA(utf8_decodifica_y_codifica) {
    for (const char32_t c : {U'a', U'ñ', U'€', U'\U0001F335', U'\U0010FFFF'}) {
        std::string s;
        utf8::anexar(s, c);
        const auto d = utf8::decodificar(s.data(), s.data() + s.size());
        VERIFICAR(d.valido);
        VERIFICAR(d.punto == c);
        VERIFICAR(d.longitud == s.size());
    }
    VERIFICAR(utf8::contar("año") == 3);
    VERIFICAR(utf8::es_valido("pingüino → México"));
}

PRUEBA(utf8_rechaza_secuencias_invalidas) {
    const std::string_view invalidas[] = {
        "\xC0\xAF",          // forma sobrelarga de «/»
        "\xE0\x80\xAF",      // forma sobrelarga de tres bytes
        "\xED\xA0\x80",      // sustituto UTF-16
        "\xF4\x90\x80\x80",  // mayor que U+10FFFF
        "\xF5\x80\x80\x80",  // byte inicial imposible
        "\x80",              // continuación suelta
        "\xC3",              // secuencia truncada
        "\xE2\x82",          // secuencia truncada
        "\xF3",              // «ó» en Latin-1
    };
    for (const std::string_view s : invalidas) {
        VERIFICAR(!utf8::es_valido(s));
        const auto d = utf8::decodificar(s.data(), s.data() + s.size());
        VERIFICAR(!d.valido);
        VERIFICAR(d.longitud >= 1 && d.longitud <= s.size());
        VERIFICAR(d.punto == utf8::kReemplazo);
    }
    VERIFICAR(utf8::sanear("a\xFFz") == "a\xEF\xBF\xBDz");
}

PRUEBA(unicode_propiedades) {
    for (const char32_t c : {U'ñ', U'Ñ', U'á', U'ü', U'Á', U'_', U'x', U'λ', U'ß'}) {
        VERIFICAR(unicode::es_inicio_identificador(c));
        VERIFICAR(unicode::es_continuacion_identificador(c));
    }
    for (const char32_t c : {U'×', U'÷', U'¿', U'¡', U'«', U'→', U'€', U' ', U'+'}) {
        VERIFICAR(!unicode::es_inicio_identificador(c));
    }
    VERIFICAR(!unicode::es_inicio_identificador(U'7'));
    VERIFICAR(unicode::es_continuacion_identificador(U'7'));
    VERIFICAR(unicode::es_continuacion_identificador(0x0301));  // acento combinante
    VERIFICAR(!unicode::es_inicio_identificador(0x0301));
    VERIFICAR(unicode::componer(U'a', 0x0301) == U'á');
    VERIFICAR(unicode::componer(U'n', 0x0303) == U'ñ');
    VERIFICAR(unicode::componer(U'u', 0x0308) == U'ü');
    VERIFICAR(unicode::componer(U'x', 0x0301) == 0);
    VERIFICAR(unicode::es_espacio(0x00A0));
    VERIFICAR(unicode::es_control_bidi(0x202E));
}

// ── Vocabulario ─────────────────────────────────────────────────────────────

// Cada grafía de tokens.def, analizada sola, debe dar exactamente su token.
PRUEBA(cada_grafia_produce_su_token) {
    std::size_t cantidad = 0;
    for (const Grafia& g : grafias()) {
        const Analisis a{std::string(g.texto)};
        VERIFICAR(a.limpio());
        VERIFICAR(a.tokens.size() == 2);
        VERIFICAR(a[0].tipo == g.tipo);
        VERIFICAR(a[0].lexema == g.texto);
        if (a[0].tipo != g.tipo) std::cerr << "  grafía: " << g.texto << '\n';
        ++cantidad;
    }
    VERIFICAR(cantidad > 100);
}

PRUEBA(palabras_reservadas_con_y_sin_acento) {
    const Analisis a("función funcion según segun carácter caracter vacío lógico booleano");
    VERIFICAR((a.tipos() == Tipos{T::PrFuncion, T::PrFuncion, T::PrSegun, T::PrSegun, T::PrCaracter,
                                  T::PrCaracter, T::PrVacio, T::PrLogico, T::PrLogico}));
    VERIFICAR(a.limpio());
    VERIFICAR(a.simbolos.cantidad() == 0);
}

PRUEBA(sinonimos_de_devolver) {
    const Analisis a("devolver retornar regresar");
    VERIFICAR((a.tipos() == Tipos{T::PrDevolver, T::PrDevolver, T::PrDevolver}));
}

PRUEBA(reservadas_distinguen_mayusculas) {
    const Analisis a("Si SI si función Función");
    VERIFICAR((a.tipos() == Tipos{T::Identificador, T::Identificador, T::PrSi, T::PrFuncion, T::Identificador}));
}

PRUEBA(describir_tipos) {
    VERIFICAR(describir(T::PrFuncion) == "palabra reservada «función»");
    VERIFICAR(describir(T::MenorIgual) == "«<=»");
    VERIFICAR(describir(T::LiteralEntero) == "literal entero");
    VERIFICAR(categoria(T::PrSi) == Categoria::Reservada);
    VERIFICAR(categoria(T::Mas) == Categoria::Signo);
    VERIFICAR(nombre(T::FusionNula) == "FusionNula");
}

// ── Identificadores ─────────────────────────────────────────────────────────

PRUEBA(identificadores_en_espanol) {
    const Analisis a("año niño pingüino _privado camión2 MÁXIMO señal_útil ñandú");
    VERIFICAR(a.limpio());
    VERIFICAR(a.tokens.size() == 9);
    for (std::size_t i = 0; i + 1 < a.tokens.size(); ++i) VERIFICAR(a[i].tipo == T::Identificador);
    VERIFICAR(a[0].texto == "año");
    VERIFICAR(a[2].texto == "pingüino");
    VERIFICAR(a.simbolos.cantidad() == 8);
}

PRUEBA(identificadores_parecidos_a_reservadas) {
    const Analisis a("sí sin0 sinónimo funciones para_cada");
    for (std::size_t i = 0; i + 1 < a.tokens.size(); ++i) VERIFICAR(a[i].tipo == T::Identificador);
}

// «o» + U+0301 debe equivaler a «ó», tanto en reservadas como en identificadores.
PRUEBA(normaliza_acentos_combinantes) {
    const std::string funcion_nfd = std::string("funcio") + "\xCC\x81" + "n";
    const std::string anio_nfd = std::string("an") + "\xCC\x83" + "o";
    const Analisis a(funcion_nfd + " " + anio_nfd + " año");
    VERIFICAR(a.limpio());
    VERIFICAR(a[0].tipo == T::PrFuncion);
    VERIFICAR(a[1].tipo == T::Identificador);
    VERIFICAR(a[1].lexema == anio_nfd);
    VERIFICAR(a[1].texto == "año");
    VERIFICAR(a[1].simbolo == a[2].simbolo);
    VERIFICAR(a.simbolos.cantidad() == 1);
}

PRUEBA(tabla_de_simbolos_cuenta_apariciones) {
    const Analisis a("total = total + parcial;\ntotal");
    VERIFICAR(a.simbolos.cantidad() == 2);
    const auto total = a.simbolos.buscar("total");
    const auto parcial = a.simbolos.buscar("parcial");
    VERIFICAR(total && parcial);
    VERIFICAR(a.simbolos[*total].apariciones == 3);
    VERIFICAR(a.simbolos[*parcial].apariciones == 1);
    VERIFICAR(a.simbolos[*parcial].primera_aparicion.columna == 17);
    VERIFICAR(!a.simbolos.buscar("otro_nombre"));
    VERIFICAR(a[0].simbolo == *total && a[2].simbolo == *total);
}

// ── Números ─────────────────────────────────────────────────────────────────

PRUEBA(enteros_en_varias_bases) {
    const Analisis a("0 42 1_000_000 0xFF 0Xff 0b1010 0o17 007 18446744073709551615 0xFFFF_FFFF_FFFF_FFFF");
    VERIFICAR(a.limpio());
    const std::uint64_t esperados[] = {0, 42, 1000000, 255, 255, 10, 15, 7, 18446744073709551615ull,
                                       18446744073709551615ull};
    VERIFICAR(a.tokens.size() == 11);
    for (std::size_t i = 0; i < 10; ++i) {
        VERIFICAR(a[i].tipo == T::LiteralEntero);
        VERIFICAR(a[i].entero == esperados[i]);
    }
}

PRUEBA(reales) {
    const Analisis a("3.14 0.5 1e3 2.5E-3 1_000.25 6.02e+23");
    VERIFICAR(a.limpio());
    const double esperados[] = {3.14, 0.5, 1e3, 2.5e-3, 1000.25, 6.02e+23};
    VERIFICAR(a.tokens.size() == 7);
    for (std::size_t i = 0; i < 6; ++i) {
        VERIFICAR(a[i].tipo == T::LiteralReal);
        VERIFICAR(a[i].real == esperados[i]);
    }
}

// Los reales deben quedar redondeados exactamente igual que los del compilador
// de C++, tanto por la ruta rápida como por la general.
PRUEBA(reales_redondeados_correctamente) {
    const Analisis a(
        "0.1 0.3 123456.789 1e22 1e23 1E-22 1e-23 9007199254740992.0 9007199254740993.0 "
        "0.0000000000000000000001 123456789012345678901234567890.0 4.9e-324 1.7976931348623157e308 "
        "2.2250738585072014e-308 0.30000000000000004 1_0.2_5e0_1 5e-1 1e0 00.50");
    VERIFICAR(a.limpio());
    const double esperados[] = {0.1, 0.3, 123456.789, 1e22, 1e23, 1E-22, 1e-23, 9007199254740992.0,
                                9007199254740993.0, 0.0000000000000000000001,
                                123456789012345678901234567890.0, 4.9e-324, 1.7976931348623157e308,
                                2.2250738585072014e-308, 0.30000000000000004, 102.5, 5e-1, 1e0, 0.5};
    const std::size_t cantidad = sizeof esperados / sizeof esperados[0];
    VERIFICAR(a.tokens.size() == cantidad + 1);
    for (std::size_t i = 0; i < cantidad && i + 1 < a.tokens.size(); ++i) {
        VERIFICAR(a[i].tipo == T::LiteralReal);
        VERIFICAR(a[i].real == esperados[i]);
        if (a[i].real != esperados[i]) std::cerr << "  literal: " << a[i].lexema << '\n';
    }

    // Un valor demasiado pequeño se redondea a cero sin ser un error.
    const Analisis diminuto("1e-400");
    VERIFICAR(diminuto.limpio());
    VERIFICAR(diminuto[0].real == 0.0);
}

PRUEBA(el_punto_no_siempre_es_decimal) {
    const Analisis rango("1..5");
    VERIFICAR((rango.tipos() == Tipos{T::LiteralEntero, T::Rango, T::LiteralEntero}));
    const Analisis inclusivo("1..=n");
    VERIFICAR((inclusivo.tipos() == Tipos{T::LiteralEntero, T::RangoInclusivo, T::Identificador}));
    const Analisis miembro("7.texto()");
    VERIFICAR((miembro.tipos() ==
               Tipos{T::LiteralEntero, T::Punto, T::Identificador, T::ParentesisIzq, T::ParentesisDer}));
    const Analisis final_("5.");
    VERIFICAR((final_.tipos() == Tipos{T::LiteralEntero, T::Punto}));
    VERIFICAR(rango.limpio() && inclusivo.limpio() && miembro.limpio() && final_.limpio());
}

PRUEBA(numeros_mal_formados) {
    for (const char* texto : {"12abc", "0b102", "0xFG", "0x", "0b", "1__0", "1_", "0x_1", "3.5año", "1e5x"}) {
        const Analisis a(texto);
        VERIFICAR(a.cuenta("L0007") == 1);
        VERIFICAR(a.diagnosticos.errores() == 1);
        VERIFICAR(a.tokens.size() == 2);
        if (a.cuenta("L0007") != 1 || a.tokens.size() != 2) std::cerr << "  texto: " << texto << '\n';
    }
}

PRUEBA(numeros_fuera_de_rango) {
    const Analisis entero("18446744073709551616");
    VERIFICAR(entero.cuenta("L0008") == 1);
    VERIFICAR(entero[0].tipo == T::LiteralEntero);
    const Analisis hexadecimal("0x1_0000_0000_0000_0000");
    VERIFICAR(hexadecimal.cuenta("L0008") == 1);
    const Analisis real("1e999");
    VERIFICAR(real.cuenta("L0008") == 1);
    VERIFICAR(real[0].tipo == T::LiteralReal);
}

// ── Cadenas y caracteres ────────────────────────────────────────────────────

PRUEBA(cadenas_simples) {
    const Analisis a(R"("hola" "" "¡Órale, México!")");
    VERIFICAR(a.limpio());
    VERIFICAR((a.tipos() == Tipos{T::LiteralCadena, T::LiteralCadena, T::LiteralCadena}));
    VERIFICAR(a[0].texto == "hola");
    VERIFICAR(a[0].lexema == "\"hola\"");
    VERIFICAR(a[1].texto.empty());
    VERIFICAR(a[2].texto == "¡Órale, México!");
}

PRUEBA(secuencias_de_escape) {
    const Analisis a(R"("a\nb\t\"c\"\\ \x41 \u{F1} \u{1F335} \0.")");
    VERIFICAR(a.limpio());
    VERIFICAR(a[0].texto == std::string_view("a\nb\t\"c\"\\ A ñ 🌵 \0.", 21));
}

PRUEBA(escapes_invalidos) {
    // «\u00F1» va en un literal normal: MSVC lo sustituye incluso dentro de R"(...)".
    for (const char* texto : {R"("\q")", R"("\x4")", R"("\xFF")", "\"\\u00F1\"", R"("\u{}")",
                              R"("\u{110000}")", R"("\u{D800}")", R"("\u{41")", R"("\é")"}) {
        const Analisis a(texto);
        VERIFICAR(a.cuenta("L0005") == 1);
        VERIFICAR(a.diagnosticos.errores() == 1);
        VERIFICAR(a[0].tipo == T::LiteralCadena);
        VERIFICAR(a.tokens.size() == 2);
        if (a.diagnosticos.errores() != 1 || a.tokens.size() != 2) std::cerr << "  texto: " << texto << '\n';
    }
}

PRUEBA(comillas_angulares_anidadas) {
    const Analisis a("«uno «dos» tres» «con \"rectas\"» «escape \\» listo»");
    VERIFICAR(a.limpio());
    VERIFICAR((a.tipos() == Tipos{T::LiteralCadena, T::LiteralCadena, T::LiteralCadena}));
    VERIFICAR(a[0].texto == "uno «dos» tres");
    VERIFICAR(a[1].texto == "con \"rectas\"");
    VERIFICAR(a[2].texto == "escape » listo");
}

PRUEBA(comillas_tipograficas) {
    const Analisis a("“El «pingüino» dijo \"hola\"”;");
    VERIFICAR(a.limpio());
    VERIFICAR((a.tipos() == Tipos{T::LiteralCadena, T::PuntoYComa}));
    VERIFICAR(a[0].texto == "El «pingüino» dijo \"hola\"");
}

PRUEBA(cadenas_multilinea) {
    const Analisis a("\"\"\"uno\r\ndos \"citado\"\n\"\"\" fin");
    VERIFICAR(a.limpio());
    VERIFICAR((a.tipos() == Tipos{T::LiteralCadena, T::Identificador}));
    VERIFICAR(a[0].texto == "uno\ndos \"citado\"\n");
    VERIFICAR(a[1].pos.linea == 3);
    VERIFICAR(a[1].pos.columna == 5);
}

PRUEBA(cadenas_sin_cerrar) {
    const Analisis a("x = \"hola\ny = 2;");
    VERIFICAR(a.cuenta("L0003") == 1);
    VERIFICAR((a.tipos() == Tipos{T::Identificador, T::Asignar, T::LiteralCadena, T::Identificador,
                                  T::Asignar, T::LiteralEntero, T::PuntoYComa}));
    VERIFICAR(a[2].texto == "hola");
    VERIFICAR(a[3].pos.linea == 2);

    const Analisis angular("«sin cierre");
    VERIFICAR(angular.cuenta("L0003") == 1);
    const Analisis triple("\"\"\"sin cierre\n\n");
    VERIFICAR(triple.cuenta("L0003") == 1);
}

PRUEBA(literales_de_caracter) {
    const std::string e_nfd = std::string("'e") + "\xCC\x81" + "'";
    const Analisis a("'a' 'ñ' '\\n' '\\'' '\\u{1F335}' '\"' " + e_nfd);
    VERIFICAR(a.limpio());
    const char32_t esperados[] = {U'a', U'ñ', U'\n', U'\'', U'\U0001F335', U'"', U'é'};
    VERIFICAR(a.tokens.size() == 8);
    for (std::size_t i = 0; i < 7; ++i) {
        VERIFICAR(a[i].tipo == T::LiteralCaracter);
        VERIFICAR(a[i].caracter == esperados[i]);
    }
}

PRUEBA(literales_de_caracter_invalidos) {
    for (const char* texto : {"''", "'ab'", "'a", "'ñu'"}) {
        const Analisis a(texto);
        VERIFICAR(a.cuenta("L0006") == 1);
        VERIFICAR(a.diagnosticos.errores() == 1);
        VERIFICAR(a[0].tipo == T::LiteralCaracter);
    }
}

// ── Signos ──────────────────────────────────────────────────────────────────

PRUEBA(coincidencia_mas_larga) {
    const Analisis a("a<<=b>>=c**=d**e...f..=g..h.i ?? j?.k -> => := :: ++ --");
    VERIFICAR(a.limpio());
    VERIFICAR((a.tipos() == Tipos{T::Identificador, T::DesplazarIzqIgual, T::Identificador,
                                  T::DesplazarDerIgual, T::Identificador, T::PotenciaIgual,
                                  T::Identificador, T::Potencia, T::Identificador, T::Elipsis,
                                  T::Identificador, T::RangoInclusivo, T::Identificador, T::Rango,
                                  T::Identificador, T::Punto, T::Identificador, T::FusionNula,
                                  T::Identificador, T::AccesoSeguro, T::Identificador, T::Flecha,
                                  T::FlechaGruesa, T::Definir, T::DobleDosPuntos, T::Incremento,
                                  T::Decremento}));
}

PRUEBA(signos_del_espanol_y_matematicos) {
    const Analisis a("¿a ≠ b? ¡c ≤ d! e ≥ f ∧ g ∨ ¬h × i ÷ j − k ← l → m");
    VERIFICAR(a.limpio());
    VERIFICAR((a.tipos() == Tipos{T::InterrogacionAbre, T::Identificador, T::Distinto, T::Identificador,
                                  T::InterrogacionCierra, T::ExclamacionAbre, T::Identificador,
                                  T::MenorIgual, T::Identificador, T::NoLogico, T::Identificador,
                                  T::MayorIgual, T::Identificador, T::YLogico, T::Identificador,
                                  T::OLogico, T::NoLogico, T::Identificador, T::Asterisco,
                                  T::Identificador, T::Diagonal, T::Identificador, T::Menos,
                                  T::Identificador, T::Asignar, T::Identificador, T::Flecha,
                                  T::Identificador}));
}

PRUEBA(caracteres_inesperados) {
    const Analisis a("a $ b ` c # d");
    VERIFICAR(a.cuenta("L0002") == 3);
    VERIFICAR((a.tipos() == Tipos{T::Identificador, T::Error, T::Identificador, T::Error,
                                  T::Identificador, T::Error, T::Identificador}));

    // El signo de interrogación griego se confunde con el punto y coma.
    const Analisis griego("x = 1\xCD\xBE");
    VERIFICAR(griego.cuenta("L0002") == 1);
    VERIFICAR(!griego.diagnosticos.lista().front().ayuda.empty());

    const Analisis nulo(std::string("a\0b", 3));
    VERIFICAR(nulo.cuenta("L0002") == 1);
    VERIFICAR((nulo.tipos() == Tipos{T::Identificador, T::Error, T::Identificador}));
}

// ── Comentarios y blancos ───────────────────────────────────────────────────

PRUEBA(comentarios_se_omiten) {
    const Analisis a("a // hasta el fin de línea ñ\nb /* bloque /* anidado */ sigue */ c");
    VERIFICAR(a.limpio());
    VERIFICAR((a.tipos() == Tipos{T::Identificador, T::Identificador, T::Identificador}));
    VERIFICAR(a[1].inicia_linea);
    VERIFICAR(!a[2].inicia_linea);
    VERIFICAR(a[2].precedido_de_espacio);
}

PRUEBA(comentarios_conservados) {
    OpcionesLexico opciones;
    opciones.conservar_comentarios = true;
    const Analisis a("/// doc\n// normal\n/** doc */ /* normal */ //// separador\n/**/ x", opciones);
    VERIFICAR(a.limpio());
    VERIFICAR((a.tipos() == Tipos{T::ComentarioDoc, T::Comentario, T::ComentarioDoc, T::Comentario,
                                  T::Comentario, T::Comentario, T::Identificador}));
    VERIFICAR(a[0].lexema == "/// doc");
    VERIFICAR(a[2].lexema == "/** doc */");
    VERIFICAR(a[2].pos.linea == 3);
}

PRUEBA(comentario_de_bloque_sin_cerrar) {
    const Analisis a("a /* abierto /* anidado */\nb");
    VERIFICAR(a.cuenta("L0004") == 1);
    VERIFICAR((a.tipos() == Tipos{T::Identificador}));
    VERIFICAR(a.diagnosticos.lista().front().pos.columna == 3);
}

PRUEBA(la_diagonal_sola_es_un_operador) {
    const Analisis a("a / b /= c");
    VERIFICAR((a.tipos() == Tipos{T::Identificador, T::Diagonal, T::Identificador, T::EntreIgual, T::Identificador}));
}

PRUEBA(banderas_de_espaciado) {
    const Analisis a("a+b\n  c d");
    VERIFICAR(a[0].inicia_linea && !a[0].precedido_de_espacio);
    VERIFICAR(!a[1].inicia_linea && !a[1].precedido_de_espacio);
    VERIFICAR(!a[2].precedido_de_espacio);
    VERIFICAR(a[3].inicia_linea && a[3].precedido_de_espacio);
    VERIFICAR(!a[4].inicia_linea && a[4].precedido_de_espacio);
}

PRUEBA(espacios_unicode) {
    // Espacio de no separación (U+00A0) y separador de línea (U+2028).
    const Analisis a("a\xC2\xA0" "b\xE2\x80\xA8" "c");
    VERIFICAR(a.limpio());
    VERIFICAR((a.tipos() == Tipos{T::Identificador, T::Identificador, T::Identificador}));
    VERIFICAR(a[2].pos.linea == 2);
    VERIFICAR(a[2].pos.columna == 1);
}

// ── Posiciones ──────────────────────────────────────────────────────────────

PRUEBA(columnas_en_puntos_de_codigo) {
    const Analisis a("año = «niño»;\n\tsegún ñ");
    VERIFICAR(a.limpio());
    VERIFICAR(a[0].pos.linea == 1 && a[0].pos.columna == 1 && a[0].pos.desplazamiento == 0);
    VERIFICAR(a[0].longitud == 4);
    VERIFICAR(a[1].pos.columna == 5 && a[1].pos.desplazamiento == 5);
    VERIFICAR(a[2].pos.columna == 7 && a[2].longitud == 9);
    VERIFICAR(a[3].pos.columna == 13);
    VERIFICAR(a[4].pos.linea == 2 && a[4].pos.columna == 2);
    VERIFICAR(a[5].pos.linea == 2 && a[5].pos.columna == 8);
    VERIFICAR(a[6].tipo == T::FinArchivo && a[6].pos.linea == 2 && a[6].pos.columna == 9);
}

PRUEBA(terminadores_de_linea) {
    const Analisis a("a\r\nb\rc\nd");
    VERIFICAR(a[0].pos.linea == 1);
    VERIFICAR(a[1].pos.linea == 2);
    VERIFICAR(a[2].pos.linea == 3);
    VERIFICAR(a[3].pos.linea == 4);
    VERIFICAR(a.fuente.cantidad_lineas() == 4);
    VERIFICAR(a.fuente.linea(1) == "a");
    VERIFICAR(a.fuente.linea(2) == "b");
    VERIFICAR(a.fuente.linea(4) == "d");
}

PRUEBA(bom_inicial_se_ignora) {
    const Analisis a("\xEF\xBB\xBF" "si x");
    VERIFICAR(a.limpio());
    VERIFICAR(a.fuente.tenia_bom());
    VERIFICAR((a.tipos() == Tipos{T::PrSi, T::Identificador}));
    VERIFICAR(a[0].pos.desplazamiento == 0 && a[0].pos.columna == 1);

    const Analisis intermedio("a \xEF\xBB\xBF" "b");
    VERIFICAR(intermedio.cuenta("L0010") == 1);
    VERIFICAR(!intermedio.diagnosticos.hay_errores());
}

// Al reutilizar el mismo Token no deben quedar restos del token anterior.
PRUEBA(siguiente_reutiliza_el_destino) {
    const Fuente fuente("prueba.alma", "nombre 7 \"texto\\n\" ; 2.5 'c'");
    Diagnosticos diagnosticos;
    TablaSimbolos simbolos;
    Lexico lexico(fuente, diagnosticos, simbolos);
    Token t;

    lexico.siguiente(t);
    VERIFICAR(t.tipo == T::Identificador && t.simbolo == 0 && t.texto == "nombre");
    lexico.siguiente(t);
    VERIFICAR(t.tipo == T::LiteralEntero && t.entero == 7);
    VERIFICAR(t.simbolo == kSinSimbolo && t.texto.empty());
    lexico.siguiente(t);
    VERIFICAR(t.tipo == T::LiteralCadena && t.texto == "texto\n");
    lexico.siguiente(t);
    VERIFICAR(t.tipo == T::PuntoYComa && t.texto.empty() && t.entero == 0 && t.lexema == ";");
    lexico.siguiente(t);
    VERIFICAR(t.tipo == T::LiteralReal && t.real == 2.5);
    lexico.siguiente(t);
    VERIFICAR(t.tipo == T::LiteralCaracter && t.caracter == U'c');
    for (int i = 0; i < 3; ++i) {
        lexico.siguiente(t);
        VERIFICAR(t.tipo == T::FinArchivo && t.lexema.empty() && t.longitud == 0);
        VERIFICAR(t.pos.columna == 29);
    }
    VERIFICAR(lexico.siguiente().tipo == T::FinArchivo);
}

PRUEBA(archivo_vacio) {
    const Analisis vacio("");
    VERIFICAR(vacio.limpio());
    VERIFICAR(vacio.tokens.size() == 1 && vacio[0].tipo == T::FinArchivo);
    const Analisis blancos("  \n\t// nada\n");
    VERIFICAR(blancos.tokens.size() == 1);
    VERIFICAR(blancos[0].pos.linea == 3);
}

// ── Codificación y seguridad ────────────────────────────────────────────────

// «función» guardada en Latin-1: la ó llega como el byte 0xF3.
PRUEBA(utf8_invalido_en_el_codigo) {
    const Analisis a("funci\xF3n x");
    VERIFICAR(a.cuenta("L0001") == 1);
    VERIFICAR((a.tipos() == Tipos{T::Identificador, T::Error, T::Identificador, T::Identificador}));
    VERIFICAR(a.diagnosticos.lista().front().pos.columna == 6);
    VERIFICAR(!a.diagnosticos.lista().front().ayuda.empty());
    VERIFICAR(a[3].pos.columna == 9);
}

PRUEBA(utf8_invalido_en_cadenas_y_comentarios) {
    const Analisis a("\"a\xFF" "b\" // \xC3\n/* \x80 */ x");
    VERIFICAR(a.cuenta("L0001") == 3);
    VERIFICAR((a.tipos() == Tipos{T::LiteralCadena, T::Identificador}));
    VERIFICAR(a[0].texto == "a\xEF\xBF\xBD" "b");
    // La explicación larga solo acompaña al primer error.
    VERIFICAR(!a.diagnosticos.lista()[0].ayuda.empty());
    VERIFICAR(a.diagnosticos.lista()[1].ayuda.empty());
}

PRUEBA(controles_bidireccionales) {
    const Analisis comentario("// \xE2\x80\xAE oculto\nx");
    VERIFICAR(comentario.cuenta("L0009") == 1);
    VERIFICAR(!comentario.diagnosticos.hay_errores());
    VERIFICAR(comentario.diagnosticos.advertencias() == 1);

    const Analisis cadena("\"\xE2\x81\xA6\"");
    VERIFICAR(cadena.cuenta("L0009") == 1);
    VERIFICAR(!cadena.diagnosticos.hay_errores());

    const Analisis codigo("a \xE2\x80\xAE b");
    VERIFICAR(codigo.cuenta("L0009") == 1);
    VERIFICAR(codigo.diagnosticos.hay_errores());
}

PRUEBA(muchos_errores_no_agotan_la_memoria) {
    const Analisis a(std::string(5000, '$'));
    VERIFICAR(a.diagnosticos.errores() == 5000);
    VERIFICAR(a.diagnosticos.lista().size() == Diagnosticos::kMaximoAlmacenados);
    VERIFICAR(a.tokens.size() == 5001);
}

// ── Diagnósticos ────────────────────────────────────────────────────────────

PRUEBA(formato_de_diagnosticos) {
    const Analisis a("año = «niño;");
    std::ostringstream salida;
    a.diagnosticos.imprimir(salida, a.fuente, false);
    const std::string esperado =
        "error[L0003]: cadena sin cerrar\n"
        " --> prueba.alma:1:7\n"
        "  |\n"
        "1 | año = «niño;\n"
        "  |       ^^^^^^\n"
        "  = ayuda: falta «»»; una cadena no puede continuar en otra línea (para eso usa comillas triples)\n"
        "\n";
    VERIFICAR(salida.str() == esperado);
    if (salida.str() != esperado) std::cerr << salida.str();
    VERIFICAR(a.diagnosticos.resumen() == "1 error");
}

PRUEBA(programa_completo) {
    const Analisis a(
        "función área(radio: real) → real {\n"
        "    devolver 3.1416 × radio ** 2;\n"
        "}\n");
    VERIFICAR(a.limpio());
    VERIFICAR((a.tipos() == Tipos{T::PrFuncion, T::Identificador, T::ParentesisIzq, T::Identificador,
                                  T::DosPuntos, T::PrReal, T::ParentesisDer, T::Flecha, T::PrReal,
                                  T::LlaveIzq, T::PrDevolver, T::LiteralReal, T::Asterisco,
                                  T::Identificador, T::Potencia, T::LiteralEntero, T::PuntoYComa,
                                  T::LlaveDer}));
    VERIFICAR(a.simbolos.cantidad() == 2);
}

}  // namespace

int main() {
#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);
#endif
    for (const Prueba& p : pruebas()) {
        const int antes = g_fallos;
        p.funcion();
        if (g_fallos != antes) std::cerr << "FALLÓ " << p.nombre << '\n';
    }
    std::cout << pruebas().size() << " pruebas, " << g_verificaciones << " verificaciones, " << g_fallos
              << " fallos\n";
    return g_fallos == 0 ? 0 : 1;
}
