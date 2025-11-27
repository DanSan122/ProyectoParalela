// test_gpu_call.cpp
// Pequeño programa de prueba que llama al wrapper (GPU o stub) para
// verificar el conteo por edad sobre `output/registros.dat`.
#include <iostream>
extern "C" long long contarPacientesRangoEdad_GPU(char* archivo, int minEdad, int maxEdad);

int main() {
    // Nota: el wrapper en este repositorio puede ser el stub CPU si no hay CUDA.
    char path[] = "output/registros.dat";
    int minE = 30;
    int maxE = 40;
    long long res = contarPacientesRangoEdad_GPU(path, minE, maxE);
    std::cout << "Conteo entre " << minE << " y " << maxE << " = " << res << std::endl;
    return 0;
}
