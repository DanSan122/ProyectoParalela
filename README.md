# ProyectoParalela

ProyectoParalela transforma un gestor clínico secuencial en un flujo híbrido (MPI + OpenMP + CUDA (opcional))
manteniendo el layout binario original (structs empaquetados y arrays fijos de char).

Contenido y propósito
- `common.h`: definiciones compartidas (RegistroClinico, HashEntry, constantes).
- `carga_mpi.cpp`: loader paralelo (MPI + OpenMP) que parsea `csv/` y genera `registros.dat` y `tabla_hash.dat`.
- `main_gui_gestor.cpp`: interfaz Qt que carga la tabla en memoria y permite buscar/insertar/eliminar registros.
- `kernel_filtro.cu`: kernel CUDA para análisis por rango de edad (requiere nvcc para compilar).
- `gpu_stub.cpp`: fallback en CPU para pruebas y conteo de pacientes únicos.
- `csv/`: datos CSV de entrada (no deben ser incluidos en el repo).

Build y ejecución (Linux)
1. Instala dependencias (g++, mpic++, OpenMP, Qt5). Para CUDA, instala el toolkit si quieres compilar `kernel_filtro.cu`.

2. Compilar la GUI (usa el stub si no tienes CUDA):
```bash
g++ -fPIC -std=c++17 main_gui_gestor.cpp gpu_stub.cpp -o output/gestor_gui `pkg-config --cflags --libs Qt5Widgets` -pthread
```

3. Ejecutar el loader MPI para generar los binarios:
```bash
# Ejecutar desde la raíz del proyecto; `carga_mpi` escribe en output/
g++ -fopenmp -std=c++17 carga_mpi.cpp -o output/carga_mpi
mpirun -np 4 output/carga_mpi
```

4. Ejecutar la GUI:
```bash
./output/gestor_gui
```

Git / datos
- Este repositorio incluye código fuente; los archivos binarios `.dat` y la carpeta `csv/` están ignorados por `.gitignore`.
- Si necesitas compartir datos grandes, usa Git LFS o un almacenamiento externo (S3, Drive, releases GitHub).

Notas y recomendaciones
- `kernel_filtro.cu` requiere `nvcc` y un GPU NVIDIA para aprovechar la aceleración. En entornos sin CUDA se usa `gpu_stub.cpp`.
- Se recomienda ejecutar el análisis en un hilo para no bloquear la UI (QtConcurrent/QFutureWatcher).
- Mantén artefactos y binarios en `output/`.

Licencia
Indica aquí la licencia si corresponde.
