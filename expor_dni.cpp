// expor_dni.cpp
// Utilidad para extraer todos los DNIs únicos desde los CSV de `../csv`
// y volcar un archivo `dnis.csv` con la lista. Útil para pruebas o muestreo.
#include <iostream>
#include <fstream>
#include <filesystem>
#include <set>
#include <string>

namespace fs = std::filesystem;

int main() {
    std::string carpeta = "../csv";
    std::set<int> dnis;

    if (!fs::exists(carpeta)) {
        std::cerr << "❌ Carpeta no encontrada: " << carpeta << "\n";
        return 1;
    }

    for (const auto& archivo : fs::directory_iterator(carpeta)) {
        if (archivo.path().extension() == ".csv") {
            std::ifstream file(archivo.path());
            if (!file.is_open()) {
                std::cerr << "❌ No se pudo abrir: " << archivo.path() << "\n";
                continue;
            }

            std::string linea;
            std::getline(file, linea); // Saltar encabezado

            while (std::getline(file, linea)) {
                size_t pos_coma = linea.find(','); // DNI está después de Fecha
                if (pos_coma == std::string::npos) continue;

                size_t pos_dni = linea.find(',', pos_coma + 1);
                if (pos_dni == std::string::npos) continue;

                std::string dni_str = linea.substr(pos_coma + 1, pos_dni - pos_coma - 1);
                try {
                    int dni = std::stoi(dni_str);
                    dnis.insert(dni);
                } catch (...) {
                    continue;
                }
            }

            file.close();
            std::cout << "📄 Procesado: " << archivo.path().filename() << "\n";
        }
    }

    std::ofstream salida("dnis.csv");
    if (!salida.is_open()) {
        std::cerr << "❌ Error creando dnis.csv\n";
        return 1;
    }

    salida << "DNI\n";
    for (int dni : dnis) {
        salida << dni << "\n";
    }
    salida.close();

    std::cout << "✅ Exportación completada. DNIs únicos: " << dnis.size() << "\n";
    std::cout << "📁 Archivo generado: dnis.csv\n";

    return 0;
}