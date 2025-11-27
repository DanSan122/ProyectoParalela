#include <iostream>
#include <fstream>
#include <vector>
#include <cstring>
#include <filesystem>

// Limpieza.cpp
// Herramienta que realiza una "limpieza física" de los archivos binarios:
// reconstruye `registros.dat` y `tabla_hash.dat` compactando registros
// y corrigiendo offsets. Útil para eliminar gaps o inconsistencias
// producidas por operaciones de eliminación lógicas.

// Estructura de registro clínico, empaquetada para evitar relleno de memoria
#pragma pack(push, 1)
struct RegistroClinico {
    char fecha[11];           // Fecha del registro (formato: "YYYY-MM-DD")
    int dni;                  // DNI del paciente
    char nombre[25];          // Nombre del paciente
    char apellido[25];        // Apellido del paciente
    int edad;                 // Edad del paciente
    char medico[40];          // Nombre del médico
    char motivo[50];          // Motivo de la consulta
    char examenes[50];        // Exámenes realizados
    char resultados[30];      // Resultados de los exámenes
    char receta[60];          // Receta médica
    long long pos_siguiente;  // Offset al siguiente registro en la lista enlazada
};

// Entrada de la tabla hash, almacena el offset al primer registro de la lista
struct HashEntry {
    long long head_offset = -1; // Offset al primer registro, -1 si está vacío
};
#pragma pack(pop)

const int TABLE_SIZE = 131072;      // Tamaño de la tabla hash
const long long NULL_OFFSET = -1;   // Valor para indicar ausencia de offset

int main() {
    // Abrir archivos binarios existentes de la tabla hash y registros
    std::ifstream tabla_in("tabla_hash.dat", std::ios::binary);
    std::ifstream registros_in("registros.dat", std::ios::binary);

    // Verificar que los archivos se abrieron correctamente
    if (!tabla_in || !registros_in) {
        std::cerr << "Error al abrir archivos existentes.\n";
        return 1;
    }

    // Crear archivos temporales para la nueva tabla hash y registros limpios
    std::ofstream nueva_tabla("tabla_hash_new.dat", std::ios::binary);
    std::ofstream nuevos_registros("registros_new.dat", std::ios::binary);

    // Vector para almacenar los nuevos offsets de los registros
    std::vector<long long> nuevos_offsets(TABLE_SIZE, NULL_OFFSET);

    std::cout << "Iniciando limpieza física de registros...\n";

    // Recorrer cada posición de la tabla hash
    for (int i = 0; i < TABLE_SIZE; ++i) {
        // Mostrar progreso cada 8192 posiciones
        if (i % 8192 == 0)
            std::cout << "Procesando posición " << i << " de " << TABLE_SIZE << "...\n";

        HashEntry entry;
        // Leer la entrada de la tabla hash
        tabla_in.read(reinterpret_cast<char*>(&entry), sizeof(entry));

        long long offset = entry.head_offset; // Offset al primer registro de la lista
        long long new_head = NULL_OFFSET;     // Nuevo offset al primer registro reconstruido

        std::vector<RegistroClinico> lista;   // Lista temporal para almacenar los registros de la posición

        // Recorrer la lista enlazada de registros en la posición actual
        while (offset != NULL_OFFSET) {
            registros_in.seekg(offset); // Ir al offset del registro
            RegistroClinico r;
            registros_in.read(reinterpret_cast<char*>(&r), sizeof(r)); // Leer el registro

            lista.push_back(r);         // Guardar el registro en la lista temporal
            offset = r.pos_siguiente;   // Avanzar al siguiente registro en la lista
        }

        // Si se encontraron registros en la posición, mostrar la cantidad
        if (!lista.empty())
            std::cout << "  ↪ " << lista.size() << " registro(s) encontrados en posición " << i << "\n";

        // Escribir los registros en orden inverso para reconstruir la lista enlazada
        for (int j = lista.size() - 1; j >= 0; --j) {
            RegistroClinico nuevo = lista[j];
            nuevo.pos_siguiente = new_head; // Actualizar el puntero al siguiente registro

            nuevos_registros.seekp(0, std::ios::end); // Ir al final del archivo de nuevos registros
            long long nuevo_offset = nuevos_registros.tellp(); // Obtener el offset actual
            nuevos_registros.write(reinterpret_cast<char*>(&nuevo), sizeof(nuevo)); // Escribir el registro

            new_head = nuevo_offset; // Actualizar el nuevo head de la lista
        }

        // Escribir la nueva entrada de la tabla hash con el nuevo head
        HashEntry nueva_entry;
        nueva_entry.head_offset = new_head;
        nueva_tabla.write(reinterpret_cast<char*>(&nueva_entry), sizeof(nueva_entry));
    }

    // Cerrar todos los archivos
    tabla_in.close();
    registros_in.close();
    nueva_tabla.close();
    nuevos_registros.close();

    // Eliminar los archivos originales y renombrar los nuevos archivos como los originales
    std::filesystem::remove("tabla_hash.dat");
    std::filesystem::remove("registros.dat");
    std::filesystem::rename("tabla_hash_new.dat", "tabla_hash.dat");
    std::filesystem::rename("registros_new.dat", "registros.dat");

    std::cout << "\n Limpieza completada. Registros reconstruidos correctamente.\n";
    return 0;
}
