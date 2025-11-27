// bench_io.cpp
// Pequeña utilidad CLI para medir tiempos de búsqueda e inserción usando
// el mismo formato binario (`registros.dat`, `tabla_hash.dat`).

#include "common.h"
#include "time_utils.h"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <vector>
#include <string>
#include <cstring>

using namespace std;

vector<HashEntry> load_table(const string &tabla_path) {
    vector<HashEntry> table(TABLE_SIZE);
    ifstream in(tabla_path, ios::binary);
    if (!in.is_open()) {
        cerr << "No se puede abrir " << tabla_path << "\n";
        return table;
    }
    for (int i = 0; i < TABLE_SIZE; ++i) {
        HashEntry e;
        in.read(reinterpret_cast<char*>(&e), sizeof(e));
        if (!in) e.head_offset = NULL_OFFSET;
        table[i] = e;
    }
    return table;
}

vector<long long> buscar_offsets(const string &registros_path, const vector<HashEntry> &table, int dni) {
    vector<long long> offsets;
    ifstream in(registros_path, ios::binary);
    if (!in.is_open()) return offsets;
    int pos = dni & (TABLE_SIZE - 1);
    long long offset = table[pos].head_offset;
    RegistroClinico r;
    long long filesize = 0;
    try { filesize = filesystem::file_size(registros_path); } catch (...) { in.seekg(0, ios::end); filesize = in.tellg(); in.seekg(0, ios::beg); }
    while (offset != NULL_OFFSET) {
        if (offset < 0 || offset + (long long)sizeof(r) > filesize) break;
        in.seekg(offset, ios::beg);
        in.read(reinterpret_cast<char*>(&r), sizeof(r));
        if (r.dni == dni) offsets.push_back(offset);
        offset = r.pos_siguiente;
    }
    return offsets;
}

long long insertar_dummy(const string &registros_path, const string &tabla_path, vector<HashEntry> &table, int dni) {
    RegistroClinico r{};
    strncpy(r.fecha, "2025-11-27", sizeof(r.fecha)-1);
    r.dni = dni;
    strncpy(r.nombre, "Bench", sizeof(r.nombre)-1);
    strncpy(r.apellido, "User", sizeof(r.apellido)-1);
    r.edad = 30;
    strncpy(r.medico, "Dr Test", sizeof(r.medico)-1);
    strncpy(r.motivo, "Bench", sizeof(r.motivo)-1);
    strncpy(r.examenes, "N/A", sizeof(r.examenes)-1);
    strncpy(r.resultados, "OK", sizeof(r.resultados)-1);
    strncpy(r.receta, "None", sizeof(r.receta)-1);
    r.pos_siguiente = NULL_OFFSET;

    // append
    fstream registros(registros_path, ios::in | ios::out | ios::binary);
    if (!registros.is_open()) {
        // try create
        registros.open(registros_path, ios::out | ios::binary);
        registros.close();
        registros.open(registros_path, ios::in | ios::out | ios::binary);
        if (!registros.is_open()) return -1;
    }
    registros.seekp(0, ios::end);
    long long new_off = registros.tellp();
    int pos = dni & (TABLE_SIZE - 1);
    r.pos_siguiente = table[pos].head_offset;
    registros.write(reinterpret_cast<char*>(&r), sizeof(r));
    registros.flush();
    registros.close();
    // update in-memory table and persist head
    table[pos].head_offset = new_off;
    ofstream th(tabla_path, ios::binary | ios::in | ios::out | ios::trunc);
    if (!th.is_open()) {
        // fallback: try open then write
        ofstream th2(tabla_path, ios::binary | ios::trunc);
        if (!th2.is_open()) return new_off;
        for (int i = 0; i < TABLE_SIZE; ++i) { th2.write(reinterpret_cast<char*>(&table[i]), sizeof(HashEntry)); }
        th2.close();
        return new_off;
    }
    for (int i = 0; i < TABLE_SIZE; ++i) { th.write(reinterpret_cast<char*>(&table[i]), sizeof(HashEntry)); }
    th.close();
    return new_off;
}

int main(int argc, char** argv) {
    if (argc < 5) {
        cout << "Usage: bench_io <search|insert> <registros.dat path> <tabla_hash.dat path> <dni> [iters]\n";
        return 1;
    }
    string mode = argv[1];
    string registros_path = argv[2];
    string tabla_path = argv[3];
    int dni = atoi(argv[4]);
    int iters = (argc >= 6) ? atoi(argv[5]) : 10;

    auto table = load_table(tabla_path);
    if (mode == "search") {
        time_utils::ScopedTimer t(string("bench_search DNI:") + to_string(dni) + " iters=" + to_string(iters));
        for (int i = 0; i < iters; ++i) {
            auto offs = buscar_offsets(registros_path, table, dni);
            // prevent optimizing out
            if (i == -1 && offs.size() > 0) cout << "ok";
        }
        cout << "Bench search completed (" << iters << " iters)\n";
    } else if (mode == "insert") {
        time_utils::ScopedTimer t(string("bench_insert DNI:") + to_string(dni) + " iters=" + to_string(iters));
        for (int i = 0; i < iters; ++i) {
            long long off = insertar_dummy(registros_path, tabla_path, table, dni);
            if (off < 0) {
                cerr << "Error inserting\n";
                return 2;
            }
        }
        cout << "Bench insert completed (" << iters << " iters)\n";
    } else {
        cerr << "Unknown mode: " << mode << "\n";
        return 1;
    }
    return 0;
}
