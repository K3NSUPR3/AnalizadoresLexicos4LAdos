#!/usr/bin/env python3
"""Genera src/unicode_tablas.inc a partir de la base de datos Unicode de Python.

Uso:  python herramientas/generar_unicode.py

Tablas generadas (solo puntos de código >= U+0080; el ASCII se resuelve
directamente en el analizador léxico):

  kXidInicio        rangos con la propiedad XID_Start
  kXidContinuacion  rangos con la propiedad XID_Continue
  kComposiciones    pares canónicos (base, marca) -> carácter precompuesto
"""

import pathlib
import unicodedata

SALIDA = pathlib.Path(__file__).resolve().parent.parent / "src" / "unicode_tablas.inc"


def es_sustituto(cp: int) -> bool:
    return 0xD800 <= cp <= 0xDFFF


def rangos(predicado):
    resultado = []
    inicio = None
    for cp in range(0x80, 0x110000):
        cumple = (not es_sustituto(cp)) and predicado(chr(cp))
        if cumple and inicio is None:
            inicio = cp
        elif not cumple and inicio is not None:
            resultado.append((inicio, cp - 1))
            inicio = None
    if inicio is not None:
        resultado.append((inicio, 0x10FFFF))
    return resultado


def composiciones():
    pares = []
    for cp in range(0x80, 0x110000):
        if es_sustituto(cp):
            continue
        descomposicion = unicodedata.decomposition(chr(cp))
        if not descomposicion or descomposicion.startswith("<"):
            continue
        partes = descomposicion.split()
        if len(partes) != 2:
            continue
        base, marca = (int(x, 16) for x in partes)
        # Se descartan las exclusiones de composición: solo interesan los
        # pares que la forma NFC realmente recompone.
        if unicodedata.normalize("NFC", chr(base) + chr(marca)) == chr(cp):
            pares.append((base, marca, cp))
    pares.sort()
    return pares


def emitir_rangos(f, nombre, tabla):
    f.write(f"constexpr Rango {nombre}[] = {{\n")
    for i in range(0, len(tabla), 4):
        fila = ", ".join(f"{{0x{a:04X}, 0x{b:04X}}}" for a, b in tabla[i:i + 4])
        f.write(f"    {fila},\n")
    f.write("};\n\n")


def main():
    xid_inicio = rangos(lambda c: c.isidentifier())
    xid_continuacion = rangos(lambda c: ("a" + c).isidentifier())
    pares = composiciones()

    with open(SALIDA, "w", encoding="utf-8", newline="\n") as f:
        f.write("// Archivo generado por herramientas/generar_unicode.py. NO EDITAR A MANO.\n")
        f.write(f"// Unicode {unicodedata.unidata_version}\n\n")
        f.write(f'constexpr char kVersionUnicode[] = "{unicodedata.unidata_version}";\n\n')
        emitir_rangos(f, "kXidInicio", xid_inicio)
        emitir_rangos(f, "kXidContinuacion", xid_continuacion)
        f.write("constexpr Composicion kComposiciones[] = {\n")
        for i in range(0, len(pares), 3):
            fila = ", ".join(f"{{0x{a:04X}, 0x{b:04X}, 0x{c:04X}}}" for a, b, c in pares[i:i + 3])
            f.write(f"    {fila},\n")
        f.write("};\n")

    print(f"{SALIDA.name}: Unicode {unicodedata.unidata_version}, "
          f"{len(xid_inicio)} rangos XID_Start, {len(xid_continuacion)} rangos XID_Continue, "
          f"{len(pares)} composiciones")


if __name__ == "__main__":
    main()
