// common.h
// Definiciones compartidas entre los módulos C++ y CUDA del proyecto.
// Contiene la estructura empaquetada `RegistroClinico` (layout fijo en disco),
// la entrada de la tabla hash `HashEntry` y constantes globales.
// Mantener este archivo estable es crítico para la compatibilidad binaria.
#pragma once
#include <cstdint>
#include <limits>
#include <stddef.h>

#pragma pack(push, 1)
struct RegistroClinico {
    char fecha[11];
    int dni;
    char nombre[25];
    char apellido[25];
    int edad;
    char medico[40];
    char motivo[50];
    char examenes[50];
    char resultados[30];
    char receta[60];
    long long pos_siguiente;
};

struct HashEntry {
    long long head_offset;
};
#pragma pack(pop)

// Constantes compartidas
static const int TABLE_SIZE = 131072;
static const long long NULL_OFFSET = -1LL;
