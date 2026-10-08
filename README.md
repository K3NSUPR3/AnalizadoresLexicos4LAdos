<<<<<<< HEAD
# AlmaLili

Compilador de AlmaLili, un lenguaje de programación con vocabulario en español,
escrito en C++17. El código fuente de los programas se lee siempre en UTF-8.

Estado actual: **fase 1, análisis léxico**. El núcleo (`almalili_nucleo`) ya
incluye lo que las fases siguientes necesitan para apoyarse en él: manejo del
texto fuente, diagnósticos y tabla de símbolos.

## Compilar

Requiere un compilador de C++17 (MSVC 2019 o posterior, GCC 9+, Clang 10+) y
CMake 3.20+.

En Windows, con Visual Studio instalado, basta con:

```powershell
.\compilar.ps1 -Pruebas
```

El script carga el entorno de Visual Studio, compila en `build\` con Ninja y
ejecuta las pruebas. En otros sistemas:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

## Uso

```
almalili [opciones] <archivo.alma>

  --json          escribe tokens, símbolos y diagnósticos en JSON
  --simbolos      muestra la tabla de símbolos al terminar
  --comentarios   conserva los comentarios como tokens
  --vocabulario   lista las palabras reservadas y los signos del lenguaje
  --medir         mide la velocidad del análisis sin imprimir tokens
  --sin-color     no usa colores en los diagnósticos
```

Prueba con `build\almalili.exe ejemplos\muestra.alma --simbolos` y con
`build\almalili.exe ejemplos\errores.alma`. El código de salida es 0 si no hubo
errores, 1 si hubo errores léxicos y 2 si el uso fue incorrecto.

## El léxico de AlmaLili

Todo el vocabulario está en [include/almalili/tokens.def](include/almalili/tokens.def).
Agregar una palabra reservada, un signo o una grafía alternativa es agregar una
línea ahí; el enumerado, las tablas y el analizador se derivan de ese archivo.

| Elemento | Reglas |
| --- | --- |
| Palabras reservadas | En minúsculas y con su ortografía correcta (`función`, `según`, `carácter`). Las que llevan acento también se aceptan sin él (`funcion`). Hay sinónimos: `devolver`, `retornar` y `regresar` son el mismo token. |
| Identificadores | Cualquier letra Unicode (`año`, `niños`, `pingüino`, `MÁXIMO`), dígitos y `_`. Distinguen mayúsculas. Una letra seguida de un acento combinante equivale a la letra acentuada. |
| Enteros | Decimales, `0x` hexadecimal, `0b` binario, `0o` octal; `_` como separador (`1_000_000`). Hasta 64 bits sin signo. |
| Reales | Punto decimal y exponente opcional (`3.14`, `2.5e-3`). El punto solo es decimal si le sigue un dígito, así `1..5` es un rango. |
| Cadenas | `"rectas"`, `«angulares»` (se anidan), `“tipográficas”` y `"""multilínea"""`. Escapes: `\n \t \r \0 \\ \" \' \xHH \u{H…}`. |
| Caracteres | `'a'`, `'ñ'`, `'\n'`: exactamente un carácter Unicode. |
| Comentarios | `// línea` y `/* bloque */` (se anidan). `///` y `/** */` son de documentación. |
| Signos | Los habituales de la familia de C más `¿ ¡ .. ..= ... ?? ?. := **`. Varios tienen forma matemática equivalente: `≠ ≤ ≥ × ÷ → ⇒ ← ∧ ∨ ¬`. |

Cada token lleva su tipo, posición (línea y columna en caracteres, no en bytes),
lexema, valor ya convertido y dos banderas de espaciado (`inicia_linea`,
`precedido_de_espacio`) para que el analizador sintáctico decida cómo tratar los
saltos de línea.

### Diagnósticos

El analizador nunca se detiene en el primer error: lo reporta y continúa.

| Código | Significado |
| --- | --- |
| L0001 | Secuencia de bytes inválida en UTF-8 |
| L0002 | Carácter inesperado |
| L0003 | Cadena sin cerrar |
| L0004 | Comentario de bloque sin cerrar |
| L0005 | Secuencia de escape inválida |
| L0006 | Literal de carácter inválido |
| L0007 | Literal numérico mal formado |
| L0008 | Literal numérico fuera de rango |
| L0009 | Control de formato bidireccional (advertencia dentro de cadenas y comentarios, error fuera) |
| L0010 | Marca de orden de bytes (BOM) a mitad del archivo (advertencia) |

## Organización

```
include/almalili/   interfaz pública del núcleo
  tokens.def          vocabulario: tipos de token, reservadas, signos
  token.hpp           TipoToken, Token y consultas sobre el vocabulario
  lexico.hpp          Lexico: de texto UTF-8 a tokens
  fuente.hpp          Fuente: texto de un archivo y su tabla de líneas
  diagnostico.hpp     errores y advertencias, con impresión de la línea señalada
  tabla_simbolos.hpp  identificadores únicos y cadenas internadas
  utf8.hpp            decodificación y codificación estricta de UTF-8
  unicode.hpp         propiedades Unicode (identificadores, espacios, composición)
src/                implementación y programa `almalili` (main.cpp)
pruebas/            pruebas del analizador léxico
ejemplos/           programas de muestra
herramientas/       generar_unicode.py regenera src/unicode_tablas.inc
```

Las tablas Unicode se generan con `python herramientas/generar_unicode.py` a
partir de la base de datos Unicode de la versión de Python instalada.

## Limitaciones conocidas

- La normalización de identificadores compone pares letra + marca; no es NFC
  completa (no reordena varias marcas sobre una misma letra).
- Los archivos fuente se limitan a 4 GiB.
=======
# AnalizadoresLexicos4LAdos
Analizador léxico básico para la ejecución y capacitación de proyectos de investigación 
>>>>>>> 23ffc08bbd0931e9b6d3d77f4916746f8581cd52
