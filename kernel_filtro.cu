// kernel_filtro.cu
// Implementación CUDA para filtrar registros por edad.
// - `filtrarPorEdadKernel` marca cada registro que cumple el rango.
// - `contarPacientesRangoEdad_GPU` es el wrapper que lee el archivo en
//    chunks, copia a dispositivo, ejecuta el kernel y suma las coincidencias.
// Nota: esta implementación cuenta registros (visitas). Para contar pacientes
// únicos se requiere deduplicación por DNI (host o GPU).
#include "common.h"
#include <cuda_runtime.h>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <vector>

// CUDA Kernel (GPU): filtrar registros por edad
// Location: kernel_filtro.cu -> filtrarPorEdadKernel
// Kernel que marca flags[idx]=1 si edad in [minEdad, maxEdad]
__global__ void filtrarPorEdadKernel(const RegistroClinico* recs, int n, int minEdad, int maxEdad, int* flags)
{
    int idx = blockIdx.x * blockDim.x + threadIdx.x;
    if (idx >= n) return;
    int edad = recs[idx].edad;
    flags[idx] = (edad >= minEdad && edad <= maxEdad) ? 1 : 0;
}

// CUDA Wrapper (GPU): contarPacientesRangoEdad_GPU
// Location: kernel_filtro.cu -> contarPacientesRangoEdad_GPU
extern "C" long long contarPacientesRangoEdad_GPU(char* archivo, int minEdad, int maxEdad)
{
    const size_t CHUNK = 100000; // 100k registros por chunk
    std::ifstream in(archivo, std::ios::binary | std::ios::ate);
    if (!in.is_open()) {
        std::cerr << "No se pudo abrir archivo: " << archivo << std::endl;
        return -1;
    }
    std::streamsize filesize = in.tellg();
    if (filesize <= 0) return 0;
    size_t rec_size = sizeof(RegistroClinico);
    long long total_recs = filesize / (long long)rec_size;
    in.seekg(0, std::ios::beg);

    long long totalEncontrados = 0;

    // Buffers reutilizables en host
    size_t maxChunk = CHUNK;
    std::vector<RegistroClinico> hostBuf;
    hostBuf.reserve(maxChunk);

    for (long long offset = 0; offset < total_recs; offset += (long long)CHUNK) {
        size_t thisChunk = (size_t)std::min((long long)CHUNK, total_recs - offset);
        hostBuf.resize(thisChunk);
        in.read(reinterpret_cast<char*>(hostBuf.data()), thisChunk * rec_size);
        if (!in) {
            std::cerr << "Lectura parcial o error al leer chunk desde archivo" << std::endl;
            break;
        }

        // Device allocations
        RegistroClinico* d_recs = nullptr;
        int* d_flags = nullptr;
        cudaError_t err;
        err = cudaMalloc((void**)&d_recs, thisChunk * rec_size);
        if (err != cudaSuccess) { std::cerr << "cudaMalloc recs failed: " << cudaGetErrorString(err) << std::endl; return -1; }
        err = cudaMalloc((void**)&d_flags, thisChunk * sizeof(int));
        if (err != cudaSuccess) { cudaFree(d_recs); std::cerr << "cudaMalloc flags failed: " << cudaGetErrorString(err) << std::endl; return -1; }

        // Copy to device
        err = cudaMemcpy(d_recs, hostBuf.data(), thisChunk * rec_size, cudaMemcpyHostToDevice);
        if (err != cudaSuccess) { cudaFree(d_recs); cudaFree(d_flags); std::cerr << "cudaMemcpy H2D failed: " << cudaGetErrorString(err) << std::endl; return -1; }

        // Launch kernel
        int threads = 256;
        int blocks = (thisChunk + threads - 1) / threads;
        filtrarPorEdadKernel<<<blocks, threads>>>(d_recs, (int)thisChunk, minEdad, maxEdad, d_flags);
        err = cudaGetLastError();
        if (err != cudaSuccess) { cudaFree(d_recs); cudaFree(d_flags); std::cerr << "Kernel launch failed: " << cudaGetErrorString(err) << std::endl; return -1; }

        // Copy flags back
        std::vector<int> hostFlags(thisChunk);
        err = cudaMemcpy(hostFlags.data(), d_flags, thisChunk * sizeof(int), cudaMemcpyDeviceToHost);
        if (err != cudaSuccess) { cudaFree(d_recs); cudaFree(d_flags); std::cerr << "cudaMemcpy D2H failed: " << cudaGetErrorString(err) << std::endl; return -1; }

        // Sumar resultados
        long long sum = 0;
        for (size_t i = 0; i < thisChunk; ++i) sum += hostFlags[i];
        totalEncontrados += sum;

        cudaFree(d_recs);
        cudaFree(d_flags);
    }

    in.close();
    return totalEncontrados;
}
