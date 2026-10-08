// Analizador léxico de AlmaLili: convierte texto UTF-8 en tokens.
#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "almalili/diagnostico.hpp"
#include "almalili/fuente.hpp"
#include "almalili/tabla_simbolos.hpp"
#include "almalili/token.hpp"
#include "almalili/utf8.hpp"

namespace almalili {

struct OpcionesLexico {
    // Si es true, los comentarios se entregan como tokens en lugar de omitirse.
    bool conservar_comentarios = false;
};

// Códigos de diagnóstico que emite:
//   L0001  secuencia de bytes inválida en UTF-8
//   L0002  carácter inesperado
//   L0003  cadena sin cerrar
//   L0004  comentario de bloque sin cerrar
//   L0005  secuencia de escape inválida
//   L0006  literal de carácter inválido
//   L0007  literal numérico mal formado
//   L0008  literal numérico fuera de rango
//   L0009  control de formato bidireccional
//   L0010  marca de orden de bytes (BOM) a mitad del archivo
//
// Nunca se detiene ante un error: lo reporta, entrega el mejor token posible
// (o TipoToken::Error) y continúa.
class Lexico {
public:
    // `fuente`, `diagnosticos` y `simbolos` deben vivir más que el analizador y
    // que los tokens que produce.
    Lexico(const Fuente& fuente, Diagnosticos& diagnosticos, TablaSimbolos& simbolos,
           OpcionesLexico opciones = {});

    // Siguiente token. Al terminar devuelve FinArchivo tantas veces como se pida.
    // La forma que recibe el destino evita una copia y es la más rápida.
    void siguiente(Token& destino);
    Token siguiente() {
        Token t;
        siguiente(t);
        return t;
    }

    // Todos los tokens restantes, con FinArchivo al final.
    std::vector<Token> analizar_todo();

private:
    void ubicar(const char* q, Posicion& destino) noexcept;
    Posicion posicion_en(const char* q) noexcept;
    void caracter_inesperado(Token& t);
    void registrar_salto(const char* despues) noexcept;
    void error(std::string_view codigo, Posicion pos, std::size_t longitud, std::string mensaje,
               std::string ayuda = {});
    void advertencia(std::string_view codigo, Posicion pos, std::size_t longitud,
                     std::string mensaje, std::string ayuda = {});

    bool saltar_relleno(Token& comentario, bool& inicia_linea, bool& espacio);
    void saltar_comentario_linea();
    void saltar_comentario_bloque(Posicion inicio);
    utf8::Decodificado revisar_no_ascii(const char* q);

    void leer_identificador(Token& t);
    void leer_numero(Token& t);
    void leer_cadena(Token& t, std::string_view apertura, std::string_view cierre, bool multilinea,
                     bool anidable);
    void leer_caracter(Token& t);
    void leer_escape(std::string& salida);
    void leer_signo(Token& t);
    void leer_no_ascii(Token& t);
    std::string_view normalizar(std::string_view nombre);
    bool empieza_con(std::string_view prefijo) const noexcept;

    Diagnosticos& diagnosticos_;
    TablaSimbolos& simbolos_;
    OpcionesLexico opciones_;

    const char* inicio_;  // primer byte del texto
    const char* fin_;     // uno después del último; *fin_ == '\0'
    const char* p_;       // cursor

    // Seguimiento de línea y columna. La columna se calcula contando puntos de
    // código desde la última posición consultada, así el costo total es lineal.
    std::uint32_t linea_ = 1;
    const char* inicio_linea_;
    const char* marca_columna_;
    std::uint32_t columna_marca_ = 1;

    bool aviso_codificacion_ = false;  // ya se explicó cómo arreglar la codificación
    std::string temporal_;             // búfer reutilizable para valores de literales
};

}  // namespace almalili
