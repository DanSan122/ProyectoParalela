
// search_dni.cpp
// Herramienta de diagnóstico para buscar e imprimir todos los registros
// asociados a un DNI determinado usando `output/tabla_hash.dat` y `output/registros.dat`.
#include "common.h"
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

// Función hash coherente con las usadas en el resto del proyecto
inline int hash1(int dni) { return dni & (TABLE_SIZE - 1); }

void printRegistro(const RegistroClinico &r, long long offset) {
    std::cout << "Offset: " << offset << "\n";
    std::cout << " Fecha: " << r.fecha << "\n";
    std::cout << " DNI: " << r.dni << "\n";
    std::cout << " Nombre: " << r.nombre << "\n";
    std::cout << " Apellido: " << r.apellido << "\n";
    std::cout << " Edad: " << r.edad << "\n";
    std::cout << " Medico: " << r.medico << "\n";
    std::cout << " Motivo: " << r.motivo << "\n";
    std::cout << " Examenes: " << r.examenes << "\n";
    std::cout << " Resultados: " << r.resultados << "\n";
    std::cout << " Receta: " << r.receta << "\n";
    std::cout << "-----------------------------\n";
}

int main(int argc, char** argv) {
    if (argc < 2) {
        std::cerr << "Uso: search_dni <DNI> [path_registros.dat]" << std::endl;
        return 1;
    }
    int dni = std::stoi(argv[1]);
    std::string registros_path = (argc >= 3) ? argv[2] : "output/registros.dat";
    std::string tabla_path = "output/tabla_hash.dat";

    // open tabla
    std::ifstream tabla(tabla_path, std::ios::binary);
    if (!tabla.is_open()) {
        // try project root
        tabla_path = "tabla_hash.dat";
        tabla.open(tabla_path, std::ios::binary);
        if (!tabla.is_open()) {
            std::cerr << "No se pudo abrir tabla_hash.dat en output/ ni en el directorio actual." << std::endl;
            return 1;
        }
    }

    int pos = hash1(dni);
    tabla.seekg(pos * sizeof(HashEntry), std::ios::beg);
    HashEntry he;
    tabla.read(reinterpret_cast<char*>(&he), sizeof(he));
    tabla.close();

    if (he.head_offset == NULL_OFFSET) {
        std::cout << "Head offset for hash pos " << pos << " is NULL (-1). No registros." << std::endl;
        return 0;
    }

    std::ifstream regs(registros_path, std::ios::binary);
    if (!regs.is_open()) {
        std::cerr << "No se pudo abrir registros file: " << registros_path << std::endl;
        return 1;
    }

    long long offset = he.head_offset;
    bool found = false;
    RegistroClinico r;
    while (offset != NULL_OFFSET) {
        regs.seekg(offset, std::ios::beg);
        regs.read(reinterpret_cast<char*>(&r), sizeof(r));
        if (!regs) break;
        if (r.dni == dni) {
            printRegistro(r, offset);
            found = true;
        }
        offset = r.pos_siguiente;
    }
    if (!found) std::cout << "No se encontraron registros con DNI " << dni << std::endl;
    regs.close();
    return 0;
}
