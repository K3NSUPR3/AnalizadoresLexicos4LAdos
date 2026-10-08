// almalili: interfaz de línea de comandos del analizador léxico.

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <iostream>
#include <string>
#include <string_view>
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
#include <shellapi.h>
#include <io.h>
#else
#include <unistd.h>
#endif

using namespace almalili;

namespace {

constexpr std::string_view kVersion = "0.1.0";

enum Salida : int { kExito = 0, kErroresLexicos = 1, kUsoIncorrecto = 2 };

struct Opciones {
    std::string archivo;
    bool json = false;
    bool simbolos = false;
    bool comentarios = false;
    bool vocabulario = false;
    bool medir = false;
    bool color = true;
};

// ── Plataforma ──────────────────────────────────────────────────────────────

#ifdef _WIN32

// En Windows los argumentos de main() llegan en la página de códigos ANSI, que
// no puede representar cualquier nombre de archivo; se toman de la línea de
// comandos en UTF-16 y se pasan a UTF-8.
std::vector<std::string> argumentos(int, char**) {
    std::vector<std::string> r;
    int n = 0;
    LPWSTR* anchos = CommandLineToArgvW(GetCommandLineW(), &n);
    if (anchos == nullptr) return r;
    for (int i = 0; i < n; ++i) {
        const int bytes = WideCharToMultiByte(CP_UTF8, 0, anchos[i], -1, nullptr, 0, nullptr, nullptr);
        std::string s(bytes > 0 ? static_cast<std::size_t>(bytes - 1) : 0, '\0');
        if (bytes > 1) WideCharToMultiByte(CP_UTF8, 0, anchos[i], -1, s.data(), bytes, nullptr, nullptr);
        r.push_back(std::move(s));
    }
    LocalFree(anchos);
    return r;
}

// Hace que la consola interprete la salida como UTF-8 y devuelve si stderr
// admite colores ANSI.
bool preparar_consola() {
    SetConsoleOutputCP(CP_UTF8);
    if (_isatty(_fileno(stderr)) == 0) return false;
    const HANDLE h = GetStdHandle(STD_ERROR_HANDLE);
    DWORD modo = 0;
    if (GetConsoleMode(h, &modo) == 0) return false;
    return SetConsoleMode(h, modo | ENABLE_VIRTUAL_TERMINAL_PROCESSING) != 0;
}

#else

std::vector<std::string> argumentos(int argc, char** argv) {
    return {argv, argv + argc};
}

bool preparar_consola() {
    return isatty(fileno(stderr)) != 0;
}

#endif

// ── Formato de texto ────────────────────────────────────────────────────────

// Versión de una sola línea de `texto`: escapa saltos y controles, sustituye
// bytes inválidos y recorta a `maximo` caracteres.
std::string mostrar(std::string_view texto, std::size_t maximo) {
    std::string r;
    std::size_t n = 0;
    const char* p = texto.data();
    const char* fin = p + texto.size();
    while (p < fin) {
        if (n + 1 >= maximo && utf8::contar({p, static_cast<std::size_t>(fin - p)}) > 1) {
            r += "…";
            break;
        }
        const utf8::Decodificado d = utf8::decodificar(p, fin);
        if (!d.valido) {
            utf8::anexar(r, utf8::kReemplazo);
            ++n;
        } else if (d.punto == '\n') {
            r += "\\n";
            n += 2;
        } else if (d.punto == '\r') {
            r += "\\r";
            n += 2;
        } else if (d.punto == '\t') {
            r += "\\t";
            n += 2;
        } else if (d.punto < 0x20 || d.punto == 0x7F || unicode::es_control_bidi(d.punto) ||
                   unicode::es_salto_linea(d.punto)) {
            char bufer[16];
            n += static_cast<std::size_t>(
                std::snprintf(bufer, sizeof bufer, "\\u{%X}", static_cast<unsigned>(d.punto)));
            r += bufer;
        } else {
            r.append(p, d.longitud);
            ++n;
        }
        p += d.longitud;
    }
    return r;
}

// Rellena con espacios hasta `ancho` columnas, contando caracteres y no bytes.
std::string ajustar(std::string texto, std::size_t ancho) {
    const std::size_t n = utf8::contar(texto);
    if (n < ancho) texto.append(ancho - n, ' ');
    return texto;
}

std::string real_a_texto(double v) {
    char bufer[40];
    std::snprintf(bufer, sizeof bufer, "%.15g", v);
    return bufer;
}

std::string codigo_unicode(char32_t c) {
    char bufer[16];
    std::snprintf(bufer, sizeof bufer, "U+%04X", static_cast<unsigned>(c));
    return bufer;
}

std::string escapar_json(std::string_view texto) {
    std::string r;
    const char* p = texto.data();
    const char* fin = p + texto.size();
    while (p < fin) {
        const utf8::Decodificado d = utf8::decodificar(p, fin);
        if (!d.valido) {
            utf8::anexar(r, utf8::kReemplazo);
        } else if (d.punto == '"') {
            r += "\\\"";
        } else if (d.punto == '\\') {
            r += "\\\\";
        } else if (d.punto == '\n') {
            r += "\\n";
        } else if (d.punto == '\r') {
            r += "\\r";
        } else if (d.punto == '\t') {
            r += "\\t";
        } else if (d.punto < 0x20) {
            char bufer[8];
            std::snprintf(bufer, sizeof bufer, "\\u%04x", static_cast<unsigned>(d.punto));
            r += bufer;
        } else {
            r.append(p, d.longitud);
        }
        p += d.longitud;
    }
    return r;
}

// Valor del token para la columna VALOR de la tabla; vacío si no tiene.
std::string valor_legible(const Token& t) {
    switch (t.tipo) {
        case TipoToken::LiteralEntero:
            return std::to_string(t.entero);
        case TipoToken::LiteralReal:
            return real_a_texto(t.real);
        case TipoToken::LiteralCaracter: {
            std::string c;
            utf8::anexar(c, t.caracter);
            return "'" + mostrar(c, 8) + "' " + codigo_unicode(t.caracter);
        }
        case TipoToken::LiteralCadena:
            return "\"" + mostrar(t.texto, 40) + "\"";
        case TipoToken::Identificador: {
            std::string r = "#" + std::to_string(t.simbolo);
            if (t.texto != t.lexema) r += " " + std::string(t.texto);
            return r;
        }
        default:
            return {};
    }
}

// ── Modos de salida ─────────────────────────────────────────────────────────

void imprimir_ayuda() {
    std::cout <<
        "almalili " << kVersion << " — analizador léxico del lenguaje AlmaLili\n"
        "\n"
        "Uso: almalili [opciones] <archivo.alma>\n"
        "\n"
        "Opciones:\n"
        "  --json          escribe tokens, símbolos y diagnósticos en JSON\n"
        "  --simbolos      muestra la tabla de símbolos al terminar\n"
        "  --comentarios   conserva los comentarios como tokens\n"
        "  --vocabulario   lista las palabras reservadas y los signos del lenguaje\n"
        "  --medir         mide la velocidad del análisis sin imprimir tokens\n"
        "  --sin-color     no usa colores en los diagnósticos\n"
        "  --version       muestra la versión\n"
        "  -h, --ayuda     muestra esta ayuda\n"
        "\n"
        "El archivo debe estar codificado en UTF-8.\n"
        "Código de salida: 0 sin errores, 1 con errores léxicos, 2 por uso incorrecto.\n";
}

void imprimir_vocabulario() {
    for (const Categoria cat : {Categoria::Reservada, Categoria::Signo}) {
        std::cout << (cat == Categoria::Reservada ? "PALABRAS RESERVADAS\n" : "\nSIGNOS\n");
        for (std::size_t i = 0; i < kCantidadTipos; ++i) {
            const auto tipo = static_cast<TipoToken>(i);
            if (categoria(tipo) != cat) continue;
            std::string alternativas;
            for (const Grafia& g : grafias()) {
                if (g.tipo != tipo || !g.alias) continue;
                alternativas += alternativas.empty() ? "  (también: " : ", ";
                alternativas += g.texto;
            }
            if (!alternativas.empty()) alternativas += ")";
            std::cout << "  " << ajustar(std::string(nombre(tipo)), 22)
                      << ajustar(std::string(grafia(tipo)), 16) << alternativas << '\n';
        }
    }
}

void imprimir_tabla(const std::vector<Token>& tokens) {
    std::cout << ajustar("LÍN:COL", 10) << ajustar("TIPO", 22) << ajustar("LEXEMA", 30) << "VALOR\n";
    for (const Token& t : tokens) {
        const std::string ubicacion = std::to_string(t.pos.linea) + ":" + std::to_string(t.pos.columna);
        std::cout << ajustar(ubicacion, 10) << ajustar(std::string(nombre(t.tipo)), 22)
                  << ajustar(mostrar(t.lexema, 28), 30) << valor_legible(t) << '\n';
    }
}

void imprimir_simbolos(const TablaSimbolos& simbolos) {
    std::cout << '\n' << ajustar("ID", 6) << ajustar("NOMBRE", 30) << ajustar("PRIMERA", 10) << "APARICIONES\n";
    IdSimbolo id = 0;
    for (const Simbolo& s : simbolos) {
        const std::string ubicacion =
            std::to_string(s.primera_aparicion.linea) + ":" + std::to_string(s.primera_aparicion.columna);
        std::cout << ajustar("#" + std::to_string(id++), 6) << ajustar(mostrar(s.nombre, 28), 30)
                  << ajustar(ubicacion, 10) << s.apariciones << '\n';
    }
}

void imprimir_json(const Fuente& fuente, const std::vector<Token>& tokens,
                   const TablaSimbolos& simbolos, const Diagnosticos& diagnosticos) {
    std::ostream& o = std::cout;
    o << "{\n  \"archivo\": \"" << escapar_json(fuente.nombre()) << "\",\n  \"tokens\": [";
    bool primero = true;
    for (const Token& t : tokens) {
        o << (primero ? "\n" : ",\n") << "    {\"tipo\": \"" << nombre(t.tipo) << "\", \"categoria\": \""
          << nombre(categoria(t.tipo)) << "\", \"linea\": " << t.pos.linea << ", \"columna\": "
          << t.pos.columna << ", \"desplazamiento\": " << t.pos.desplazamiento << ", \"longitud\": "
          << t.longitud << ", \"lexema\": \"" << escapar_json(t.lexema) << '"';
        switch (t.tipo) {
            case TipoToken::LiteralEntero:
                o << ", \"valor\": " << t.entero;
                break;
            case TipoToken::LiteralReal:
                o << ", \"valor\": " << real_a_texto(t.real);
                break;
            case TipoToken::LiteralCaracter: {
                std::string c;
                utf8::anexar(c, t.caracter);
                o << ", \"valor\": \"" << escapar_json(c) << '"';
                break;
            }
            case TipoToken::LiteralCadena:
                o << ", \"valor\": \"" << escapar_json(t.texto) << '"';
                break;
            case TipoToken::Identificador:
                o << ", \"simbolo\": " << t.simbolo << ", \"nombre\": \"" << escapar_json(t.texto) << '"';
                break;
            default:
                break;
        }
        o << '}';
        primero = false;
    }
    o << "\n  ],\n  \"simbolos\": [";
    primero = true;
    IdSimbolo id = 0;
    for (const Simbolo& s : simbolos) {
        o << (primero ? "\n" : ",\n") << "    {\"id\": " << id++ << ", \"nombre\": \""
          << escapar_json(s.nombre) << "\", \"linea\": " << s.primera_aparicion.linea
          << ", \"columna\": " << s.primera_aparicion.columna << ", \"apariciones\": " << s.apariciones << '}';
        primero = false;
    }
    o << "\n  ],\n  \"diagnosticos\": [";
    primero = true;
    for (const Diagnostico& d : diagnosticos.lista()) {
        o << (primero ? "\n" : ",\n") << "    {\"gravedad\": \"" << nombre(d.gravedad) << "\", \"codigo\": \""
          << d.codigo << "\", \"linea\": " << d.pos.linea << ", \"columna\": " << d.pos.columna
          << ", \"desplazamiento\": " << d.pos.desplazamiento << ", \"longitud\": " << d.longitud
          << ", \"mensaje\": \"" << escapar_json(d.mensaje) << '"';
        if (!d.ayuda.empty()) o << ", \"ayuda\": \"" << escapar_json(d.ayuda) << '"';
        o << '}';
        primero = false;
    }
    o << "\n  ],\n  \"errores\": " << diagnosticos.errores() << ",\n  \"advertencias\": "
      << diagnosticos.advertencias() << "\n}\n";
}

// Repite el análisis durante medio segundo y reporta la mejor pasada.
int medir(const Fuente& fuente, OpcionesLexico opciones) {
    using Reloj = std::chrono::steady_clock;
    const auto arranque = Reloj::now();
    double mejor = 1e300;
    std::size_t tokens = 0;
    std::size_t errores = 0;
    int pasadas = 0;
    do {
        Diagnosticos diagnosticos;
        TablaSimbolos simbolos;
        Lexico lexico(fuente, diagnosticos, simbolos, opciones);
        std::size_t n = 0;
        Token t;
        const auto t0 = Reloj::now();
        for (lexico.siguiente(t); t.tipo != TipoToken::FinArchivo; lexico.siguiente(t)) ++n;
        const double segundos = std::chrono::duration<double>(Reloj::now() - t0).count();
        mejor = std::min(mejor, segundos);
        tokens = n;
        errores = diagnosticos.errores();
        ++pasadas;
    } while (pasadas < 1000 && std::chrono::duration<double>(Reloj::now() - arranque).count() < 0.5);

    const double megabytes = static_cast<double>(fuente.texto().size()) / (1024.0 * 1024.0);
    std::printf("archivo:   %s (%.2f MiB, %u líneas)\n", fuente.nombre().c_str(), megabytes,
                fuente.cantidad_lineas());
    std::printf("tokens:    %zu\n", tokens);
    std::printf("pasadas:   %d\n", pasadas);
    std::printf("mejor:     %.3f ms\n", mejor * 1e3);
    if (mejor > 0) {
        std::printf("velocidad: %.1f MiB/s, %.1f millones de tokens/s\n", megabytes / mejor,
                    static_cast<double>(tokens) / mejor / 1e6);
    }
    return errores != 0 ? kErroresLexicos : kExito;
}

}  // namespace

int main(int argc, char** argv) {
    const bool consola_con_color = preparar_consola();
    const std::vector<std::string> args = argumentos(argc, argv);

    Opciones op;
    for (std::size_t i = 1; i < args.size(); ++i) {
        const std::string& a = args[i];
        if (a == "-h" || a == "--ayuda" || a == "--help") {
            imprimir_ayuda();
            return kExito;
        }
        if (a == "--version") {
            std::cout << "almalili " << kVersion << " (Unicode " << unicode::version() << ")\n";
            return kExito;
        }
        if (a == "--json") op.json = true;
        else if (a == "--simbolos") op.simbolos = true;
        else if (a == "--comentarios") op.comentarios = true;
        else if (a == "--vocabulario") op.vocabulario = true;
        else if (a == "--medir") op.medir = true;
        else if (a == "--sin-color") op.color = false;
        else if (a.size() > 1 && a.front() == '-') {
            std::cerr << "almalili: opción desconocida «" << a << "» (usa --ayuda para ver las disponibles)\n";
            return kUsoIncorrecto;
        } else if (!op.archivo.empty()) {
            std::cerr << "almalili: solo se puede analizar un archivo a la vez\n";
            return kUsoIncorrecto;
        } else {
            op.archivo = a;
        }
    }

    if (op.vocabulario) {
        imprimir_vocabulario();
        return kExito;
    }
    if (op.archivo.empty()) {
        imprimir_ayuda();
        return kUsoIncorrecto;
    }

    std::string contenido;
    std::string problema;
    if (!Fuente::leer_archivo(std::filesystem::u8path(op.archivo), contenido, problema)) {
        std::cerr << "almalili: «" << op.archivo << "»: " << problema << '\n';
        return kUsoIncorrecto;
    }
    const Fuente fuente(op.archivo, std::move(contenido));

    OpcionesLexico opciones_lexico;
    opciones_lexico.conservar_comentarios = op.comentarios;

    if (op.medir) return medir(fuente, opciones_lexico);

    Diagnosticos diagnosticos;
    TablaSimbolos simbolos;
    Lexico lexico(fuente, diagnosticos, simbolos, opciones_lexico);
    const std::vector<Token> tokens = lexico.analizar_todo();

    if (op.json) {
        imprimir_json(fuente, tokens, simbolos, diagnosticos);
    } else {
        imprimir_tabla(tokens);
        if (op.simbolos) imprimir_simbolos(simbolos);
        std::cout.flush();
        if (!diagnosticos.lista().empty()) {
            std::cerr << '\n';
            diagnosticos.imprimir(std::cerr, fuente, op.color && consola_con_color);
            std::cerr << fuente.nombre() << ": " << diagnosticos.resumen() << '\n';
        }
    }
    return diagnosticos.hay_errores() ? kErroresLexicos : kExito;
}
