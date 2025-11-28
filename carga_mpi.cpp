// carga_mpi.cpp
// Programa que distribuye el parseo de múltiples CSV usando MPI (ranks)
// y OpenMP (paralelismo intra-rank). Cada rank convierte su porción
// de CSVs en un archivo temporal `temp_rank_X.dat`. El proceso maestro
// concatena los temporales en `registros.dat` y reconstruye `tabla_hash.dat`,
// rellenando los campos `pos_siguiente` para permitir búsquedas por DNI.
#include "common.h"

#include <mpi.h>
#include <omp.h>

#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>
#include <cstring>
#include "time_utils.h"


// Simple hash function (potencia de 2 TABLE_SIZE)
inline int hash1(int dni) { return dni & (TABLE_SIZE - 1); }
// Parsea una línea CSV a RegistroClinico (similar al código legado)
RegistroClinico parsearLinea(const std::string &linea)
{
    std::stringstream ss(linea);
    RegistroClinico r;
    std::memset(&r, 0, sizeof(r));
    std::string campo;
    std::getline(ss, campo, ',');
    std::strncpy(r.fecha, campo.c_str(), sizeof(r.fecha) - 1);
    std::getline(ss, campo, ',');
    r.dni = campo.empty() ? 0 : std::stoi(campo);
    std::getline(ss, campo, ',');
    std::strncpy(r.nombre, campo.c_str(), sizeof(r.nombre) - 1);
    std::getline(ss, campo, ',');
    std::strncpy(r.apellido, campo.c_str(), sizeof(r.apellido) - 1);
    std::getline(ss, campo, ',');
    r.edad = campo.empty() ? 0 : std::stoi(campo);
    std::getline(ss, campo, ',');
    std::strncpy(r.medico, campo.c_str(), sizeof(r.medico) - 1);
    std::getline(ss, campo, ',');
    std::strncpy(r.motivo, campo.c_str(), sizeof(r.motivo) - 1);
    std::getline(ss, campo, ',');
    std::strncpy(r.examenes, campo.c_str(), sizeof(r.examenes) - 1);
    std::getline(ss, campo, ',');
    std::strncpy(r.resultados, campo.c_str(), sizeof(r.resultados) - 1);
    std::getline(ss, campo);
    std::strncpy(r.receta, campo.c_str(), sizeof(r.receta) - 1);
    r.pos_siguiente = NULL_OFFSET;
    return r;
}

// Lee todas las líneas (sin cabecera) de un CSV en memoria
std::vector<std::string> leerLineasCSV(const std::string &ruta)
{
    std::vector<std::string> lines;
    std::ifstream in(ruta);
    if (!in.is_open()) {
        std::cerr << "No se pudo abrir " << ruta << std::endl;
        return lines;
    }
    std::string linea;
    // Descarta cabecera
    if (!std::getline(in, linea)) return lines;
    while (std::getline(in, linea)) {
        if (!linea.empty()) lines.push_back(linea);
    }
    return lines;
}

int main(int argc, char** argv)
{
    // MPI: Inicio del entorno MPI (MPI_Init)
    MPI_Init(&argc, &argv);
    int world_rank, world_size;
    MPI_Comm_rank(MPI_COMM_WORLD, &world_rank);
    MPI_Comm_size(MPI_COMM_WORLD, &world_size);

    // Tiempo total de ejecución del proceso (solo se imprime si hay TTY)
    time_utils::ScopedTimer total_timer(std::string("carga_mpi total (rank ") + std::to_string(world_rank) + ")");

    // MPI: Maestro recopila lista de csv en ../csv y prepara broadcast
    // (CSV list serialization + MPI_Bcast más abajo)
    // Maestro recopila lista de csv en ../csv
    std::vector<std::string> files;
    if (world_rank == 0) {
        for (auto &f : std::filesystem::directory_iterator("../csv")) {
            if (f.path().extension() == ".csv") files.push_back(f.path().string());
        }
    }

    // Serializar lista para mandar a todos mediante broadcast
    // Primero maestro construye un string con rutas separadas por '\n'
    std::string concat;
    if (world_rank == 0) {
        std::ostringstream oss;
        for (size_t i = 0; i < files.size(); ++i) {
            if (i) oss << '\n';
            oss << files[i];
        }
        concat = oss.str();
    }

    // MPI: Broadcast longitud y contenido de la lista de CSVs
    // Broadcast longitud
    int len = (int)concat.size();
    MPI_Bcast(&len, 1, MPI_INT, 0, MPI_COMM_WORLD);
    if (len < 0) len = 0;
    concat.resize(len);
    // Broadcast contenido (si len==0, se envía buffer vacío)
    MPI_Bcast(&concat[0], len, MPI_CHAR, 0, MPI_COMM_WORLD);

    // Cada proceso reconstruye la lista
    if (world_rank != 0 || files.empty()) {
        files.clear();
        std::istringstream iss(concat);
        std::string p;
        while (std::getline(iss, p)) if (!p.empty()) files.push_back(p);
    }

    // MPI: Asignación simple — repartición por round-robin entre ranks
    // Aquí puedes reemplazar por reparto por tamaño/line-count si lo deseas
    std::vector<std::string> my_files;
    for (size_t i = 0; i < files.size(); ++i) {
        if ((int)(i % world_size) == world_rank) my_files.push_back(files[i]);
    }

    if (my_files.empty()) {
        if (world_rank == 0) std::cout << "Ningún archivo asignado a este rank?" << std::endl;
    }

    // OpenMP: Por cada archivo, leer líneas, parsear en paralelo con OpenMP
    // y escribir temp_rank_X.dat
    std::vector<RegistroClinico> acumulado;
    for (auto &ruta : my_files) {
        std::cout << "Rank " << world_rank << " procesando " << ruta << std::endl;
        auto lines = leerLineasCSV(ruta);
        size_t n = lines.size();
        std::vector<RegistroClinico> regs(n);
#pragma omp parallel for schedule(static)
        for (long long i = 0; i < (long long)n; ++i) {
            regs[i] = parsearLinea(lines[i]);
        }
        // Append regs to acumulado
        acumulado.insert(acumulado.end(), regs.begin(), regs.end());
        std::cout << "Rank " << world_rank << " parseó " << n << " líneas de " << ruta << std::endl;
    }

    // I/O: Escribe archivo temporal binario `temp_rank_X.dat` (uno por proceso)
    std::ostringstream tmpname;
    tmpname << "temp_rank_" << world_rank << ".dat";
    std::ofstream out(tmpname.str(), std::ios::binary);
    if (!out.is_open()) {
        std::cerr << "No se pudo crear " << tmpname.str() << std::endl;
        MPI_Abort(MPI_COMM_WORLD, 1);
    }
    if (!acumulado.empty()) out.write(reinterpret_cast<char*>(acumulado.data()), acumulado.size() * sizeof(RegistroClinico));
    out.close();

    MPI_Barrier(MPI_COMM_WORLD);

    // Maestro unifica todos los temporales en registros.dat (append en orden de rank)
    if (world_rank == 0) {
        // MPI: Maestro — concatenación de temporales y reconstrucción de tabla hash
        // (concatenación de temp_rank_X.dat -> registros.dat)
        // Asegurar existencia de tabla_hash.dat (vacía) para compatibilidad con la GUI
        if (!std::filesystem::exists("tabla_hash.dat")) {
            std::ofstream th("tabla_hash.dat", std::ios::binary);
            HashEntry empty;
            empty.head_offset = NULL_OFFSET;
            for (int i = 0; i < TABLE_SIZE; ++i) th.write(reinterpret_cast<char*>(&empty), sizeof(empty));
        }

        std::ofstream registros("registros.dat", std::ios::binary | std::ios::app);
        if (!registros.is_open()) {
            std::cerr << "No se pudo abrir/crear registros.dat" << std::endl;
            MPI_Abort(MPI_COMM_WORLD, 1);
        }
        for (int r = 0; r < world_size; ++r) {
            std::ostringstream nm;
            nm << "temp_rank_" << r << ".dat";
            if (!std::filesystem::exists(nm.str())) continue;
            std::ifstream in(nm.str(), std::ios::binary);
            registros << in.rdbuf();
            in.close();
            // opcional: borrar temporal
            std::filesystem::remove(nm.str());
            std::cout << "Maestro añadió " << nm.str() << " a registros.dat\n";
        }
        registros.close();

        // Reconstruir tabla hash y actualizar pos_siguiente en registros.dat
        // (Aquí se recorre registros.dat, se actualizan pos_siguiente y se genera tabla_hash.dat)
        try {
            std::vector<HashEntry> table(TABLE_SIZE);
            for (int i = 0; i < TABLE_SIZE; ++i) table[i].head_offset = NULL_OFFSET;

            std::fstream registros_rw("registros.dat", std::ios::in | std::ios::out | std::ios::binary);
            if (!registros_rw.is_open()) {
                std::cerr << "No se pudo abrir registros.dat para reconstruir tabla hash" << std::endl;
            } else {
                RegistroClinico r;
                long long offset = 0;
                registros_rw.seekg(0, std::ios::beg);
                while (registros_rw.read(reinterpret_cast<char*>(&r), sizeof(r))) {
                    int pos = hash1(r.dni);
                    long long prev_head = table[pos].head_offset;
                    r.pos_siguiente = prev_head;
                    // escribir de nuevo el registro con pos_siguiente actualizado
                    registros_rw.seekp(offset, std::ios::beg);
                    registros_rw.write(reinterpret_cast<char*>(&r), sizeof(r));
                    registros_rw.flush();
                    table[pos].head_offset = offset;
                    offset += sizeof(r);
                    registros_rw.seekg(offset, std::ios::beg);
                }
                registros_rw.close();

                // Escribir tabla_hash.dat con los offsets calculados
                std::ofstream th("tabla_hash.dat", std::ios::binary | std::ios::trunc);
                if (!th.is_open()) {
                    std::cerr << "No se pudo escribir tabla_hash.dat" << std::endl;
                } else {
                    for (int i = 0; i < TABLE_SIZE; ++i) {
                        HashEntry e; e.head_offset = table[i].head_offset;
                        th.write(reinterpret_cast<char*>(&e), sizeof(e));
                    }
                    th.close();
                }
            }
        } catch (const std::exception &ex) {
            std::cerr << "Error reconstruyendo tabla hash: " << ex.what() << std::endl;
        }

        std::cout << "Unificación completada por Maestro (tabla hash reconstruida)." << std::endl;
    }

    MPI_Finalize();
    return 0;
}
