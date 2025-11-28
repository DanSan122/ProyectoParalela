// gpu_stub.cpp
// Implementación CPU de referencia (stub) para el análisis por edad.
// Provee dos funciones:
// - contarPacientesRangoEdad_GPU: replica el comportamiento del kernel (cuenta visitas)
// - contarPacientesRangoEdadUnicos_CPU: cuenta pacientes únicos por DNI (deduplicación en host)
// Usar el stub cuando no exista soporte CUDA en la máquina de desarrollo.
#include "common.h"
#include <fstream>
#include <vector>
#include <iostream>
#include <unordered_set>

// GPU Stub (CPU fallback): exported functions
// - contarPacientesRangoEdad_GPU: cuenta visitas en CPU (comportamiento equivalente al kernel)
// Location: gpu_stub.cpp -> contarPacientesRangoEdad_GPU
extern "C" long long contarPacientesRangoEdad_GPU(const char* archivo, int minEdad, int maxEdad)
{
    const size_t CHUNK = 100000;
    std::ifstream in(archivo, std::ios::binary | std::ios::ate);
    if (!in.is_open()) {
        std::cerr << "(stub) No se pudo abrir archivo: " << archivo << std::endl;
        return -1;
    }
    std::streamsize filesize = in.tellg();
    if (filesize <= 0) return 0;
    size_t rec_size = sizeof(RegistroClinico);
    long long total_recs = filesize / (long long)rec_size;
    in.seekg(0, std::ios::beg);

    long long totalEncontrados = 0;
    std::vector<RegistroClinico> buf;
    for (long long offset = 0; offset < total_recs; offset += (long long)CHUNK) {
        size_t thisChunk = (size_t)std::min((long long)CHUNK, total_recs - offset);
        buf.resize(thisChunk);
        in.read(reinterpret_cast<char*>(buf.data()), thisChunk * rec_size);
        if (!in) {
            std::cerr << "(stub) Lectura parcial o error al leer chunk" << std::endl;
            break;
        }
        for (size_t i = 0; i < thisChunk; ++i) {
            int edad = buf[i].edad;
            if (edad >= minEdad && edad <= maxEdad) ++totalEncontrados;
        }
    }
    in.close();
    return totalEncontrados;
}

// GPU Stub (CPU fallback): conteo único (deduplicación en host)
// Location: gpu_stub.cpp -> contarPacientesRangoEdadUnicos_CPU
extern "C" long long contarPacientesRangoEdadUnicos_CPU(const char* archivo, int minEdad, int maxEdad)
{
    const size_t CHUNK = 100000;
    std::ifstream in(archivo, std::ios::binary | std::ios::ate);
    if (!in.is_open()) {
        std::cerr << "(stub) No se pudo abrir archivo: " << archivo << std::endl;
        return -1;
    }
    std::streamsize filesize = in.tellg();
    if (filesize <= 0) return 0;
    size_t rec_size = sizeof(RegistroClinico);
    long long total_recs = filesize / (long long)rec_size;
    in.seekg(0, std::ios::beg);

    std::unordered_set<int> seen;
    std::vector<RegistroClinico> buf;
    for (long long offset = 0; offset < total_recs; offset += (long long)CHUNK) {
        size_t thisChunk = (size_t)std::min((long long)CHUNK, total_recs - offset);
        buf.resize(thisChunk);
        in.read(reinterpret_cast<char*>(buf.data()), thisChunk * rec_size);
        if (!in) {
            std::cerr << "(stub) Lectura parcial o error al leer chunk" << std::endl;
            break;
        }
        for (size_t i = 0; i < thisChunk; ++i) {
            int edad = buf[i].edad;
            if (edad >= minEdad && edad <= maxEdad) seen.insert(buf[i].dni);
        }
    }
    in.close();
    return (long long)seen.size();
}
