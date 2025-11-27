#include <iostream>
#include <fstream>
#include <string>
#include <sstream>
#include <filesystem>
#include <cstring>
#include <vector>

// gestor_dni.cpp
// Aplicación interactiva de consola para gestionar registros clínicos:
// - Buscar por DNI
// - Insertar registros
// - Eliminar registros (todos o por índice)
// Implementa las mismas estructuras empaquetadas que el resto del proyecto
// y usa la tabla hash + lista enlazada en disco.

// Estructura que representa un registro clínico
#pragma pack(push, 1)
struct RegistroClinico
{
    char fecha[11];         // Fecha en formato AAAA-MM-DD
    int dni;                // DNI del paciente
    char nombre[25];        // Nombre del paciente
    char apellido[25];      // Apellido del paciente
    int edad;               // Edad del paciente
    char medico[40];        // Nombre del médico
    char motivo[50];        // Motivo de la consulta
    char examenes[50];      // Exámenes realizados
    char resultados[30];    // Resultados de los exámenes
    char receta[60];        // Receta médica
    long long pos_siguiente;// Offset al siguiente registro en la lista enlazada
};

// Estructura para la entrada de la tabla hash
struct HashEntry
{
    long long head_offset = -1; // Offset al primer registro de la lista enlazada
};
#pragma pack(pop)

const int TABLE_SIZE = 131072;      // Tamaño de la tabla hash (potencia de 2)
const long long NULL_OFFSET = -1;   // Valor para indicar fin de lista o vacío

std::fstream tabla_file;            // Archivo para la tabla hash
std::fstream registros_file;        // Archivo para los registros clínicos

// Función hash simple usando AND para aprovechar potencia de 2
int hash1(int dni)
{
    return dni & (TABLE_SIZE - 1);
}

// Inicializa los archivos binarios si no existen y los abre
void inicializarArchivos()
{
    // Si no existe la tabla hash, la crea e inicializa con valores vacíos
    if (!std::filesystem::exists("tabla_hash.dat"))
    {
        std::ofstream out("tabla_hash.dat", std::ios::binary);
        HashEntry empty;
        for (int i = 0; i < TABLE_SIZE; ++i)
            out.write(reinterpret_cast<char *>(&empty), sizeof(empty));
    }
    // Si no existe el archivo de registros, lo crea vacío
    if (!std::filesystem::exists("registros.dat"))
    {
        std::ofstream out("registros.dat", std::ios::binary);
    }

    // Abre ambos archivos en modo lectura/escritura binario
    tabla_file.open("tabla_hash.dat", std::ios::in | std::ios::out | std::ios::binary);
    registros_file.open("registros.dat", std::ios::in | std::ios::out | std::ios::binary);
    if (!tabla_file.is_open() || !registros_file.is_open())
    {
        std::cerr << "Error abriendo archivos binarios." << std::endl;
        exit(1);
    }
}

// Escribe el offset del primer registro en la posición de la tabla hash
void escribirHead(int pos, long long head_offset)
{
    tabla_file.seekp(pos * sizeof(HashEntry), std::ios::beg);
    HashEntry e{head_offset};
    tabla_file.write(reinterpret_cast<char *>(&e), sizeof(e));
}

// Lee el offset del primer registro desde la tabla hash
long long leerHead(int pos)
{
    HashEntry e;
    tabla_file.seekg(pos * sizeof(HashEntry), std::ios::beg);
    tabla_file.read(reinterpret_cast<char *>(&e), sizeof(e));
    return e.head_offset;
}

// Inserta un nuevo registro clínico en la lista enlazada correspondiente al DNI
void insertarRegistro(const RegistroClinico &r)
{
    int pos = hash1(r.dni);                // Calcula la posición en la tabla hash
    long long head = leerHead(pos);        // Lee el offset actual de la cabeza de la lista
    RegistroClinico tmp = r;
    registros_file.seekp(0, std::ios::end);// Se posiciona al final del archivo de registros
    long long new_off = registros_file.tellp(); // Obtiene el offset donde se insertará el nuevo registro
    tmp.pos_siguiente = head;              // El nuevo registro apunta al anterior head
    registros_file.write(reinterpret_cast<char *>(&tmp), sizeof(tmp)); // Escribe el registro
    escribirHead(pos, new_off);            // Actualiza la cabeza de la lista en la tabla hash
}

// Muestra los registros asociados a un DNI con índice y retorna sus offsets
std::vector<long long> mostrarRegistrosConIndices(int dni)
{
    std::vector<long long> offsets;
    int pos = hash1(dni);
    long long offset = leerHead(pos);
    RegistroClinico r;
    int idx = 1;

    if (offset == NULL_OFFSET)
    {
        std::cout << "No hay registros para el DNI " << dni << ".\n";
        return offsets;
    }

    long long filesize = std::filesystem::file_size("registros.dat");

    // Recorre la lista enlazada de registros para ese DNI
    while (offset != NULL_OFFSET)
    {
        if (offset < 0 || offset + sizeof(r) > filesize)
        {
            std::cerr << "Offset inválido: " << offset << "\n";
            break;
        }

        registros_file.seekg(offset, std::ios::beg);
        registros_file.read(reinterpret_cast<char *>(&r), sizeof(r));

        if (r.dni == dni)
        {
            std::cout << "[" << idx << "] Fecha: " << r.fecha
                      << " | Motivo: " << r.motivo
                      << " | Médico: " << r.medico << "\n";
            offsets.push_back(offset);
            ++idx;
        }

        offset = r.pos_siguiente;
    }

    if (offsets.empty())
        std::cout << "No se encontraron registros válidos para el DNI " << dni << ".\n";

    return offsets;
}

// Busca y muestra todos los registros asociados a un DNI
void buscarPorDNI(int dni)
{
    int pos = hash1(dni);
    long long offset = leerHead(pos);
    if (offset == NULL_OFFSET)
    {
        std::cout << "No hay registros para DNI " << dni << "\n";
        return;
    }

    RegistroClinico r;
    int idx = 0;
    long long filesize = std::filesystem::file_size("registros.dat");

    // Recorre la lista enlazada y muestra los registros que coinciden con el DNI
    while (offset != NULL_OFFSET)
    {
        if (offset < 0 || offset + sizeof(r) > filesize)
        {
            std::cerr << "Offset inválido " << offset << "\n";
            break;
        }

        registros_file.seekg(offset, std::ios::beg);
        registros_file.read(reinterpret_cast<char *>(&r), sizeof(r));
        if (r.dni == dni)
        {
            std::cout << "--- Registro " << ++idx << " ---\n"
                      << "Fecha: " << r.fecha << "\n"
                      << "DNI: " << r.dni << "   Edad: " << r.edad << "\n"
                      << "Nombre: " << r.nombre << " " << r.apellido << "\n"
                      << "Medico: " << r.medico << "\n"
                      << "Motivo: " << r.motivo << "\n"
                      << "Examenes: " << r.examenes << "\n"
                      << "Resultados: " << r.resultados << "\n"
                      << "Receta: " << r.receta << "\n\n";
        }
        offset = r.pos_siguiente;
    }

    if (idx == 0)
        std::cout << "No se encontraron registros para DNI " << dni << "\n";
}

// Elimina lógicamente todos los registros asociados a un DNI
void eliminarRegistrosDeDNI(int dni)
{
    int pos = hash1(dni);
    long long offset = leerHead(pos);
    long long new_head = NULL_OFFSET;
    RegistroClinico actual;

    // Recorre la lista enlazada y copia solo los registros que NO coinciden con el DNI
    while (offset != NULL_OFFSET)
    {
        registros_file.seekg(offset);
        registros_file.read(reinterpret_cast<char *>(&actual), sizeof(actual));
        long long siguiente = actual.pos_siguiente;

        if (actual.dni != dni)
        {
            // Guardamos este nodo
            RegistroClinico copia = actual;
            copia.pos_siguiente = new_head;
            registros_file.seekp(0, std::ios::end);
            long long nuevo_offset = registros_file.tellp();
            registros_file.write(reinterpret_cast<char *>(&copia), sizeof(copia));
            new_head = nuevo_offset;
        }

        offset = siguiente;
    }

    escribirHead(pos, new_head);
    std::cout << "Registros del DNI " << dni << " eliminados (lógicamente).\n";

    // Sincronizar y cerrar archivos
    registros_file.flush();
    registros_file.close();
    tabla_file.flush();
    tabla_file.close();

    // Reabrir archivos
    tabla_file.open("tabla_hash.dat", std::ios::in | std::ios::out | std::ios::binary);
    registros_file.open("registros.dat", std::ios::in | std::ios::out | std::ios::binary);
    if (!tabla_file.is_open() || !registros_file.is_open())
    {
        std::cerr << "Error reabriendo archivos tras eliminación.\n";
        exit(1);
    }
}

// Elimina lógicamente un registro específico de un DNI (por índice)
void eliminarUnRegistroDeDNI(int dni, int indiceEliminar)
{
    int pos = hash1(dni);
    long long offset = leerHead(pos);
    long long new_head = NULL_OFFSET;
    RegistroClinico actual;
    int idx = 0;

    // Recorre la lista enlazada y copia todos los registros excepto el que se quiere eliminar
    while (offset != NULL_OFFSET)
    {
        registros_file.seekg(offset);
        registros_file.read(reinterpret_cast<char *>(&actual), sizeof(actual));
        long long siguiente = actual.pos_siguiente;

        if (actual.dni != dni || idx != indiceEliminar)
        {
            // Copiar este nodo a una nueva posición, excepto si es el que queremos eliminar
            RegistroClinico copia = actual;
            copia.pos_siguiente = new_head;
            registros_file.seekp(0, std::ios::end);
            long long nuevo_offset = registros_file.tellp();
            registros_file.write(reinterpret_cast<char *>(&copia), sizeof(copia));
            new_head = nuevo_offset;
        }

        if (actual.dni == dni)
            idx++;

        offset = siguiente;
    }

    escribirHead(pos, new_head);
    std::cout << "Registro " << indiceEliminar << " del DNI " << dni << " eliminado correctamente.\n";

    // Sincronizar y cerrar archivos
    registros_file.flush();
    registros_file.close();
    tabla_file.flush();
    tabla_file.close();

    // Reabrir archivos
    tabla_file.open("tabla_hash.dat", std::ios::in | std::ios::out | std::ios::binary);
    registros_file.open("registros.dat", std::ios::in | std::ios::out | std::ios::binary);
    if (!tabla_file.is_open() || !registros_file.is_open())
    {
        std::cerr << "Error reabriendo archivos tras eliminación.\n";
        exit(1);
    }
}

// Validaciones de campos de entrada
bool validarFecha(const char* fecha) {
    return strlen(fecha) == 10 && fecha[4] == '-' && fecha[7] == '-';
}

bool validarTexto(const char* campo, int maxLen) {
    return strlen(campo) > 0 && strlen(campo) < maxLen;
}

bool validarDNI(int dni) {
    return dni > 0;
}

bool validarEdad(int edad) {
    return edad >= 0 && edad <= 120;
}

int main()
{
    inicializarArchivos(); // Prepara los archivos necesarios

    int opcion;
    while (true)
    {
        // Menú principal
        std::cout << "\n--- MENÚ GESTOR DE REGISTROS ---\n";
        std::cout << "1. Buscar por DNI\n";
        std::cout << "2. Insertar nuevo registro\n";
        std::cout << "3. Eliminar todos los registros de un DNI\n";
        std::cout << "4. Eliminar un registro específico de un DNI\n";
        std::cout << "0. Salir\n";
        std::cout << "Opción: ";
        std::cin >> opcion;

        if (opcion == 0)
            break;

        int dni;
        if (opcion == 1)
        {
            // Buscar registros por DNI
            std::cout << "Ingrese DNI: ";
            std::cin >> dni;
            buscarPorDNI(dni);
        }
        else if (opcion == 2)
        {
            // Insertar un nuevo registro clínico
            RegistroClinico r;
            std::cin.ignore();

            std::cout << "Fecha (AAAA-MM-DD): ";
            std::cin.getline(r.fecha, 11);
            if (!validarFecha(r.fecha)) {
                std::cerr << "Formato de fecha inválido.\n";
                continue;
            }

            std::cout << "DNI: ";
            std::cin >> r.dni;
            if (!validarDNI(r.dni)) {
                std::cerr << "DNI inválido.\n";
                continue;
            }
            std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');  // Limpia ENTER

            std::cout << "Nombre: ";
            std::cin.getline(r.nombre, 25);
            if (!validarTexto(r.nombre, 25)) {
                std::cerr << "Nombre inválido.\n";
                continue;
            }

            std::cout << "Apellido: ";
            std::cin.getline(r.apellido, 25);
            if (!validarTexto(r.apellido, 25)) {
                std::cerr << "Apellido inválido.\n";
                continue;
            }

            std::cout << "Edad: ";
            std::cin >> r.edad;
            if (!validarEdad(r.edad)) {
                std::cerr << "Edad inválida.\n";
                continue;
            }
            std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');  // Limpia ENTER

            std::cout << "Medico: ";
            std::cin.getline(r.medico, 40);
            if (!validarTexto(r.medico, 40)) {
                std::cerr << "Nombre del médico inválido.\n";
                continue;
            }

            std::cout << "Motivo: ";
            std::cin.getline(r.motivo, 50);
            if (!validarTexto(r.motivo, 50)) {
                std::cerr << "Motivo inválido.\n";
                continue;
            }

            std::cout << "Examenes: ";
            std::cin.getline(r.examenes, 50);
            if (!validarTexto(r.examenes, 50)) {
                std::cerr << "Exámenes inválidos.\n";
                continue;
            }

            std::cout << "Resultados: ";
            std::cin.getline(r.resultados, 30);
            if (!validarTexto(r.resultados, 30)) {
                std::cerr << "Resultados inválidos.\n";
                continue;
            }

            std::cout << "Receta: ";
            std::cin.getline(r.receta, 60);
            if (!validarTexto(r.receta, 60)) {
                std::cerr << "Receta inválida.\n";
                continue;
            }

            r.pos_siguiente = NULL_OFFSET;
            insertarRegistro(r);

            // Sincroniza y reabre archivos tras la inserción
            registros_file.flush();
            registros_file.close();
            tabla_file.flush();
            tabla_file.close();

            tabla_file.open("tabla_hash.dat", std::ios::in | std::ios::out | std::ios::binary);
            registros_file.open("registros.dat", std::ios::in | std::ios::out | std::ios::binary);
            if (!tabla_file.is_open() || !registros_file.is_open())
            {
                std::cerr << "Error reabriendo archivos tras inserción.\n";
                exit(1);
            }

            std::cout << "Registro insertado con éxito.\n";
        }

        else if (opcion == 3)
        {
            // Eliminar todos los registros de un DNI
            std::cout << "Ingrese DNI: ";
            std::cin >> dni;
            eliminarRegistrosDeDNI(dni);
        }
        else if (opcion == 4)
        {
            // Eliminar un registro específico de un DNI
            std::cout << "Ingrese DNI: ";
            std::cin >> dni;
            auto offsets = mostrarRegistrosConIndices(dni);
            if (offsets.empty()) {
                std::cout << "No hay registros para eliminar.\n";
                continue;
            }
            int idxEliminar;
            std::cout << "Ingrese el número del registro a eliminar: ";
            std::cin >> idxEliminar;
            idxEliminar--;  // Ajustar de índice de usuario (1-based) a índice real (0-based)

            if (idxEliminar < 0 || idxEliminar >= offsets.size()) {
                std::cout << "Índice inválido.\n";
                continue;
            }
            eliminarUnRegistroDeDNI(dni, idxEliminar);

        }

    }

    tabla_file.close();
    registros_file.close();
    std::cout << "Saliendo del gestor.\n";
    return 0;
}
