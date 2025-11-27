// main_gui_gestor.cpp (sin Q_OBJECT)
// Incluye las librerías necesarias de Qt y C++
#include <QApplication>
#include <QWidget>
#include <QPushButton>
#include <QLineEdit>
#include <QLabel>
#include <QString>
#include <QMessageBox>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QDialog>
#include <fstream>
#include <filesystem>
#include <vector>
#include <iostream>
#include <QStackedWidget>
#include <QInputDialog>
#include <cstring>
#include <shared_mutex>
#include <mutex>

// Usar definiciones compartidas
#include "common.h"

// Declaración de la función CUDA (wrapper) -- definida en kernel_filtro.cu
// Usar `const char*` para poder pasar directamente la ruta gestionada por la GUI
extern "C" long long contarPacientesRangoEdad_GPU(const char* archivo, int minEdad, int maxEdad);
// CPU stub que cuenta pacientes únicos (si está disponible)
extern "C" long long contarPacientesRangoEdadUnicos_CPU(const char* archivo, int minEdad, int maxEdad);

// Archivos binarios para la tabla hash y los registros clínicos
std::fstream tabla_file;
std::fstream registros_file;
// Paths abiertos (para debug/UI)
std::string g_tabla_path;
std::string g_registros_path;
// In-memory table + synchronization
std::vector<HashEntry> in_memory_table;
std::shared_mutex table_mutex;          // shared for readers, exclusive for writers
std::mutex registros_io_mutex;         // protect seek/read/write on registros_file
std::mutex tabla_file_mutex;           // protect writes to tabla_file

// Función hash simple para obtener la posición en la tabla hash a partir del DNI
int hash1(int dni) {
    // Usa una operación AND para obtener un valor entre 0 y TABLE_SIZE-1
    return dni & (TABLE_SIZE - 1);
}

// Inicializa los archivos binarios si no existen y los abre para lectura/escritura
void inicializarArchivos() {
    // Buscamos archivos en el directorio actual y en `output/` para compatibilidad
    // Preferir archivos dentro de `output/` si existen (el loader MPI escribe allí)
    const std::vector<std::string> tabla_candidates = {"output/tabla_hash.dat", "tabla_hash.dat"};
    const std::vector<std::string> registros_candidates = {"output/registros.dat", "registros.dat"};

    std::string tabla_path;
    for (auto &p : tabla_candidates) {
        if (std::filesystem::exists(p)) { tabla_path = p; break; }
    }
    if (tabla_path.empty()) {
        // crear en el primer candidato (cwd)
        tabla_path = tabla_candidates[0];
        std::ofstream out(tabla_path, std::ios::binary);
        HashEntry empty; empty.head_offset = NULL_OFFSET;
        for (int i = 0; i < TABLE_SIZE; ++i) out.write(reinterpret_cast<char*>(&empty), sizeof(empty));
    }

    std::string registros_path;
    for (auto &p : registros_candidates) {
        if (std::filesystem::exists(p)) { registros_path = p; break; }
    }
    if (registros_path.empty()) {
        registros_path = registros_candidates[0];
        std::ofstream out(registros_path, std::ios::binary);
    }

    // Abre ambos archivos en modo lectura/escritura binario
    tabla_file.open(tabla_path, std::ios::in | std::ios::out | std::ios::binary);
    registros_file.open(registros_path, std::ios::in | std::ios::out | std::ios::binary);
    if (!tabla_file.is_open() || !registros_file.is_open()) {
        std::cerr << "Error abriendo archivos binarios: tabla='" << tabla_path << "' registros='" << registros_path << "'" << std::endl;
        exit(1);
    }
    // Guardar paths globales para debug/UI
    g_tabla_path = tabla_path;
    g_registros_path = registros_path;
    std::cout << "Archivos abiertos: tabla='" << tabla_path << "' registros='" << registros_path << "'\n";

    // Cargar tabla en memoria
    in_memory_table.resize(TABLE_SIZE);
    tabla_file.seekg(0, std::ios::beg);
    for (int i = 0; i < TABLE_SIZE; ++i) {
        HashEntry e;
        tabla_file.read(reinterpret_cast<char*>(&e), sizeof(e));
        if (!tabla_file) { e.head_offset = NULL_OFFSET; }
        in_memory_table[i] = e;
    }
    // reset file position
    tabla_file.clear();
}

// Escribe el offset del primer registro (head) en la posición dada de la tabla hash
void escribirHead(int pos, long long head_offset) {
    {
        std::unique_lock<std::shared_mutex> wlock(table_mutex);
        in_memory_table[pos].head_offset = head_offset;
    }
    // persist to disk
    std::lock_guard<std::mutex> lg(tabla_file_mutex);
    tabla_file.seekp(pos * sizeof(HashEntry), std::ios::beg);
    HashEntry e{head_offset};
    tabla_file.write(reinterpret_cast<char*>(&e), sizeof(e));
    tabla_file.flush();
}

// Lee el offset del primer registro (head) en la posición dada de la tabla hash
long long leerHead(int pos) {
    std::shared_lock<std::shared_mutex> rlock(table_mutex);
    return in_memory_table[pos].head_offset;
}

// Inserta un nuevo registro clínico en la lista enlazada correspondiente al hash del DNI
void insertarRegistro(const RegistroClinico& r) {
    int pos = hash1(r.dni);
    RegistroClinico tmp = r;
    // exclusive on table while updating head
    std::unique_lock<std::shared_mutex> wlock(table_mutex);
    // append record safely
    {
        std::lock_guard<std::mutex> io_lock(registros_io_mutex);
        registros_file.seekp(0, std::ios::end);
        long long new_off = registros_file.tellp();
        tmp.pos_siguiente = in_memory_table[pos].head_offset;
        registros_file.write(reinterpret_cast<char*>(&tmp), sizeof(tmp));
        registros_file.flush();
        // update in-memory and persist head
        in_memory_table[pos].head_offset = new_off;
        // persist only this head
        {
            std::lock_guard<std::mutex> lg(tabla_file_mutex);
            tabla_file.seekp(pos * sizeof(HashEntry), std::ios::beg);
            HashEntry e{new_off};
            tabla_file.write(reinterpret_cast<char*>(&e), sizeof(e));
            tabla_file.flush();
        }
    }
}

// Busca todos los registros clínicos asociados a un DNI y devuelve sus offsets en el archivo
std::vector<long long> buscarRegistros(int dni) {
    registros_file.clear();  // Limpia flags como EOF
    registros_file.seekg(0, std::ios::beg); // Vuelve al inicio

    std::vector<long long> offsets;
    int pos = hash1(dni);
    long long offset = leerHead(pos);
    RegistroClinico r;

    if (offset == NULL_OFFSET) return offsets; // Si no hay registros, retorna vacío

        long long filesize = 0;
        try {
            filesize = std::filesystem::file_size(g_registros_path.empty() ? "registros.dat" : g_registros_path);
        } catch (...) {
            std::lock_guard<std::mutex> io_lock(registros_io_mutex);
            registros_file.seekg(0, std::ios::end);
            filesize = registros_file.tellg();
            registros_file.seekg(0, std::ios::beg);
        }

    // Recorre la lista enlazada de registros para ese DNI
    while (offset != NULL_OFFSET) {
        if (offset < 0 || offset + sizeof(r) > filesize) break; // Verifica límites de archivo
        {
            std::lock_guard<std::mutex> io_lock(registros_io_mutex);
            registros_file.seekg(offset, std::ios::beg);
            registros_file.read(reinterpret_cast<char*>(&r), sizeof(r));
        }
        if (r.dni == dni) offsets.push_back(offset); // Si el DNI coincide, guarda el offset
        offset = r.pos_siguiente; // Avanza al siguiente registro en la lista
    }
    return offsets;
}

// Elimina todos los registros asociados a un DNI, reconstruyendo la lista enlazada
// dni: el DNI cuyos registros se eliminarán completamente del archivo
void eliminarPorDNI(int dni) {
    int pos = hash1(dni);                  // Calcula la posición en la tabla hash para el DNI
    long long head = leerHead(pos);        // Obtiene el offset del primer registro de la lista enlazada
    long long nuevo_head = NULL_OFFSET;    // Nuevo head para la lista reconstruida
    long long offset = head;               // Offset actual en la lista enlazada
    RegistroClinico r;
    std::vector<RegistroClinico> nuevos;   // Vector para almacenar los registros que se conservarán

    // Recorre la lista enlazada y guarda solo los registros que NO corresponden al DNI a eliminar
    while (offset != NULL_OFFSET) {
        {
            std::lock_guard<std::mutex> io_lock(registros_io_mutex);
            registros_file.seekg(offset, std::ios::beg); // Posiciona el puntero de lectura en el registro actual
            registros_file.read(reinterpret_cast<char*>(&r), sizeof(r));
        }
        if (r.dni != dni) {
            nuevos.push_back(r); // Solo guarda los registros que no se eliminan
        }
        offset = r.pos_siguiente; // Avanza al siguiente registro en la lista
    }

    // Sobrescribe el archivo de registros solo con los registros no eliminados
    registros_file.close();
    // Abre el archivo en modo truncado para borrar todo y escribir solo los registros restantes
    registros_file.open(g_registros_path.empty() ? "registros.dat" : g_registros_path, std::ios::out | std::ios::trunc | std::ios::binary);

    long long new_offset = 0;
    // Reconstruye la lista enlazada en orden inverso para mantener el orden original
    for (auto& reg : nuevos) {
        reg.pos_siguiente = nuevo_head; // El siguiente registro apunta al anterior en la lista
        nuevo_head = new_offset;        // Actualiza el nuevo head al offset actual
        registros_file.write(reinterpret_cast<char*>(&reg), sizeof(reg));
        new_offset += sizeof(reg);
    }
    // Actualiza el head de la lista en la tabla hash
    escribirHead(pos, nuevos.empty() ? NULL_OFFSET : (new_offset - sizeof(RegistroClinico)));
}

// Elimina un registro específico (por índice) de los registros asociados a un DNI
// dni: el DNI cuyos registros se buscan
// indexEliminar: el índice (0-based) del registro a eliminar entre los registros de ese DNI
void eliminarRegistroEspecifico(int dni, int indexEliminar) {
    int pos = hash1(dni);                  // Calcula la posición en la tabla hash
    long long offset = leerHead(pos);      // Obtiene el offset del primer registro de la lista enlazada
    long long nuevo_head = NULL_OFFSET;    // Nuevo head para reconstruir la lista
    RegistroClinico actual;
    int idx = 0;

    std::vector<RegistroClinico> nuevos;   // Vector para almacenar los registros que se conservarán

    // Recorre la lista enlazada y guarda todos los registros excepto el que se quiere eliminar
    while (offset != NULL_OFFSET) {
        {
            std::lock_guard<std::mutex> io_lock(registros_io_mutex);
            registros_file.seekg(offset);      // Posiciona el puntero de lectura en el registro actual
            registros_file.read(reinterpret_cast<char*>(&actual), sizeof(actual));
        }
        long long siguiente = actual.pos_siguiente; // Guarda el offset al siguiente registro

        // Si el registro no es el que se debe eliminar, lo agrega al vector de nuevos registros
        if (actual.dni != dni || idx != indexEliminar) {
            RegistroClinico copia = actual;
            nuevos.push_back(copia);
        }
        // Solo incrementa el índice si el registro pertenece al DNI buscado
        if (actual.dni == dni)
            idx++;

        offset = siguiente; // Avanza al siguiente registro en la lista
    }

    // Sobrescribe el archivo de registros solo con los registros no eliminados
    registros_file.close();
    registros_file.open(g_registros_path.empty() ? "registros.dat" : g_registros_path, std::ios::out | std::ios::trunc | std::ios::binary);

    long long head = NULL_OFFSET;
    long long new_offset = 0;
    // Reconstruye la lista enlazada en orden inverso para mantener el orden original
    for (auto it = nuevos.rbegin(); it != nuevos.rend(); ++it) {
        it->pos_siguiente = head; // El siguiente registro apunta al anterior en la lista
        registros_file.write(reinterpret_cast<char*>(&(*it)), sizeof(*it));
        head = new_offset;        // Actualiza el head al nuevo registro insertado
        new_offset += sizeof(RegistroClinico);
    }

    // Actualiza el head de la lista en la tabla hash
    escribirHead(pos, nuevos.empty() ? NULL_OFFSET : (new_offset - sizeof(RegistroClinico)));
}

// Ventana principal de la aplicación, hereda de QWidget
class MainWindow : public QWidget {
public:
    MainWindow(QWidget *parent = nullptr);
    void buscar();      // Método para buscar registros por DNI
    void insertar();    // Método para insertar un nuevo registro
    void eliminar();    // Método para eliminar registros


private:
    QStackedWidget *stacked; // Permite cambiar entre la vista de login y el menú principal
    QLineEdit *user, *pass;  // Campos de usuario y contraseña
};

// Constructor de la ventana principal, define la interfaz gráfica
MainWindow::MainWindow(QWidget *parent) : QWidget(parent) {
    setWindowTitle("Gestor Clínico");
    stacked = new QStackedWidget(this);

    // Vista de login
    QWidget *login = new QWidget;
    QVBoxLayout *logLayout = new QVBoxLayout(login);
    user = new QLineEdit;
    pass = new QLineEdit;
    pass->setEchoMode(QLineEdit::Password);
    QPushButton *btnLogin = new QPushButton("Iniciar sesión");
    logLayout->addWidget(new QLabel("Usuario:")); logLayout->addWidget(user);
    logLayout->addWidget(new QLabel("Contraseña:")); logLayout->addWidget(pass);
    logLayout->addWidget(btnLogin);

    // Menú principal
    QWidget *menu = new QWidget;
    QVBoxLayout *menuLayout = new QVBoxLayout(menu);
    QPushButton *btnBuscar = new QPushButton("Buscar por DNI");
    QPushButton *btnInsertar = new QPushButton("Insertar Registro");
    QPushButton *btnEliminar = new QPushButton("Eliminar por DNI");
    QPushButton *btnEliminarUno = new QPushButton("Eliminar un Registro por DNI");
    QPushButton *btnGPU = new QPushButton("Análisis GPU");
    QPushButton *btnSalir = new QPushButton("Salir");
    menuLayout->addWidget(btnBuscar);
    menuLayout->addWidget(btnInsertar);
    menuLayout->addWidget(btnEliminar);
    menuLayout->addWidget(btnEliminarUno);
    menuLayout->addWidget(btnGPU);
    menuLayout->addWidget(btnSalir);

    // Añade las vistas al stacked widget
    stacked->addWidget(login);
    stacked->addWidget(menu);
    QVBoxLayout *mainLayout = new QVBoxLayout(this);
    mainLayout->addWidget(stacked);

    // Conexión del botón de login
    QObject::connect(btnLogin, &QPushButton::clicked, [=]() {
        // Verifica credenciales simples (usuario: admin, contraseña: 1234)
        if (user->text() == "admin" && pass->text() == "1234") {
            stacked->setCurrentIndex(1); // Accede al menú si las credenciales son correctas
        } else {
            QMessageBox::warning(this, "Error", "Credenciales incorrectas");
        }
    });

    // Conexiones de los botones del menú principal
    QObject::connect(btnBuscar, &QPushButton::clicked, this, &MainWindow::buscar);
    QObject::connect(btnInsertar, &QPushButton::clicked, this, &MainWindow::insertar);
    QObject::connect(btnEliminar, &QPushButton::clicked, this, &MainWindow::eliminar);
    //QObject::connect(btnEliminarUno, &QPushButton::clicked, this, &MainWindow::eliminar);
    QObject::connect(btnGPU, &QPushButton::clicked, [=]() {
        bool ok1 = false;
        int minEdad = QInputDialog::getInt(this, "Análisis GPU", "Edad mínima:", 0, 0, 200, 1, &ok1);
        if (!ok1) return;
        bool ok2 = false;
        int maxEdad = QInputDialog::getInt(this, "Análisis GPU", "Edad máxima:", minEdad, 0, 200, 1, &ok2);
        if (!ok2) return;
        // Usar el path que abrió la GUI (normalmente "output/registros.dat").
        // Si por alguna razón no está inicializado, caer a 'registros.dat'.
        std::string path = g_registros_path.empty() ? std::string("registros.dat") : g_registros_path;
        // Preguntar modo: contar visitas (registros) o pacientes únicos (DNI)
        QStringList modos;
        modos << "Visitas (registros)" << "Pacientes (únicos)";
        bool modoOk = false;
        QString modo = QInputDialog::getItem(this, "Modo de conteo", "Seleccionar modo:", modos, 0, false, &modoOk);
        if (!modoOk) return;

        QApplication::setOverrideCursor(Qt::WaitCursor);
        long long resultado = 0;
        if (modo.startsWith("Visitas")) {
            resultado = contarPacientesRangoEdad_GPU(path.c_str(), minEdad, maxEdad);
        } else {
            // Llamamos al stub CPU para contar pacientes únicos
            resultado = contarPacientesRangoEdadUnicos_CPU(path.c_str(), minEdad, maxEdad);
        }
        QApplication::restoreOverrideCursor();
        if (resultado < 0) {
            QMessageBox::warning(this, "Error", "Ocurrió un error durante el análisis.");
        } else {
            QString etiqueta = modo.startsWith("Visitas") ? "visitas" : "pacientes";
            QMessageBox::information(this, "Resultado", QString::number(resultado) + " " + etiqueta + " en el rango.");
        }
    });
    QObject::connect(btnSalir, &QPushButton::clicked, qApp, &QApplication::quit);
}

// Método para buscar registros por DNI y mostrarlos uno a uno
void MainWindow::buscar() {
    QDialog d(this);
    QFormLayout form(&d);
    QLineEdit *dniEdit = new QLineEdit;
    QPushButton *buscarBtn = new QPushButton("Buscar");
    form.addRow("DNI:", dniEdit);
    form.addWidget(buscarBtn);
    QObject::connect(buscarBtn, &QPushButton::clicked, [&]() {
        int dni = dniEdit->text().toInt();
        auto registros = buscarRegistros(dni);
        d.accept();
        if (!registros.empty()) {
            int index = 0;

            // Función lambda recursiva para mostrar los registros uno a uno
            std::function<void()> mostrar;
            mostrar = [&]() {
                RegistroClinico r;
                registros_file.seekg(registros[index]);
                registros_file.read(reinterpret_cast<char*>(&r), sizeof(r));
                QString info = "Resultado " + QString::number(index + 1) + "/" + QString::number(registros.size()) + ":\n";
                info += "Fecha: " + QString(r.fecha) + "\nDNI: " + QString::number(r.dni) +
                        "\nNombre: " + QString(r.nombre) +
                        "\nApellido: " + QString(r.apellido) +
                        "\nEdad: " + QString::number(r.edad) +
                        "\nMédico: " + QString(r.medico) +
                        "\nMotivo: " + QString(r.motivo) +
                        "\nExámenes: " + QString(r.examenes) +
                        "\nResultados: " + QString(r.resultados) +
                        "\nReceta: " + QString(r.receta);
                QMessageBox msgBox;
                QPushButton *prevBtn = msgBox.addButton("<< Anterior", QMessageBox::ActionRole);
                QPushButton *nextBtn = msgBox.addButton("Siguiente >>", QMessageBox::ActionRole);
                QPushButton *closeBtn = msgBox.addButton(QMessageBox::Close);
                msgBox.setText(info);
                // Permite navegar entre los registros encontrados
                QObject::connect(prevBtn, &QPushButton::clicked, [&]() {
                    if (index > 0) --index;
                    msgBox.done(0);
                    mostrar();
                });
                QObject::connect(nextBtn, &QPushButton::clicked, [&]() {
                    if (index < registros.size() - 1) ++index;
                    msgBox.done(0);
                    mostrar();
                });
                msgBox.exec();
            };

            mostrar();  // Muestra el primer registro
        } else {
            // Mostrar información de depuración: head offset y paths usados
            int pos = hash1(dni);
            long long head = leerHead(pos);
            QString debugMsg = "No se encontraron registros.\n";
            debugMsg += "Hash pos: " + QString::number(pos) + "\n";
            debugMsg += "Head offset leído: " + QString::number(head) + "\n";
            debugMsg += "Tabla usada: " + QString::fromStdString(g_tabla_path) + "\n";
            debugMsg += "Registros usados: " + QString::fromStdString(g_registros_path) + "\n";
            QMessageBox::information(this, "Sin resultados", debugMsg);
        }
    });

    d.exec();
}

// Método para insertar un nuevo registro clínico
void MainWindow::insertar() {
    QDialog d(this);
    QFormLayout form(&d);
    RegistroClinico r{};
    QLineEdit *dni = new QLineEdit;
    QLineEdit *nombre = new QLineEdit;
    QLineEdit *apellido = new QLineEdit;
    QLineEdit *edad = new QLineEdit;
    QLineEdit *fecha = new QLineEdit;
    QLineEdit *medico = new QLineEdit;
    QLineEdit *motivo = new QLineEdit;
    QLineEdit *examen = new QLineEdit;
    QLineEdit *resultado = new QLineEdit;
    QLineEdit *receta = new QLineEdit;

    form.addRow("DNI:", dni);
    form.addRow("Nombre:", nombre);
    form.addRow("Apellido:", apellido);
    form.addRow("Edad:", edad);
    form.addRow("Fecha (YYYY-MM-DD):", fecha);
    form.addRow("Médico:", medico);
    form.addRow("Motivo:", motivo);
    form.addRow("Exámenes:", examen);
    form.addRow("Resultados:", resultado);
    form.addRow("Receta:", receta);

    QPushButton *btnInsert = new QPushButton("Guardar");
    form.addWidget(btnInsert);

    // Al presionar guardar, toma los datos y los inserta en el archivo
    QObject::connect(btnInsert, &QPushButton::clicked, [&]() {
        // === VALIDACIONES ===

        // Validar DNI (8 dígitos numéricos)
        QString dniTexto = dni->text().trimmed();
        if (dniTexto.length() != 8 || !dniTexto.toInt()) {
            QMessageBox::warning(&d, "Error", "DNI inválido. Debe tener 8 dígitos numéricos.");
            return;
        }

        // Validar Nombre y Apellido (no vacíos)
        if (nombre->text().trimmed().isEmpty() || apellido->text().trimmed().isEmpty()) {
            QMessageBox::warning(&d, "Error", "Nombre y Apellido no pueden estar vacíos.");
            return;
        }

        // Validar Edad (número positivo)
        bool edadOK;
        int edadValor = edad->text().toInt(&edadOK);
        if (!edadOK || edadValor <= 0) {
            QMessageBox::warning(&d, "Error", "Edad inválida. Debe ser un número positivo.");
            return;
        }

        // Validar Fecha (formato YYYY-MM-DD)
        QRegExp regexFecha("\\d{4}-\\d{2}-\\d{2}");
        if (!regexFecha.exactMatch(fecha->text())) {
            QMessageBox::warning(&d, "Error", "Fecha inválida. Use el formato YYYY-MM-DD.");
            return;
        }

        // Validar Médico y Motivo (no vacíos)
        if (medico->text().trimmed().isEmpty() || motivo->text().trimmed().isEmpty()) {
            QMessageBox::warning(&d, "Error", "Los campos 'Médico' y 'Motivo' no pueden estar vacíos.");
            return;
        }

        // === COPIA DE DATOS ===

        strncpy(r.fecha, fecha->text().toStdString().c_str(), sizeof(r.fecha));
        r.dni = dniTexto.toInt();
        strncpy(r.nombre, nombre->text().toStdString().c_str(), sizeof(r.nombre));
        strncpy(r.apellido, apellido->text().toStdString().c_str(), sizeof(r.apellido));
        r.edad = edadValor;
        strncpy(r.medico, medico->text().toStdString().c_str(), sizeof(r.medico));
        strncpy(r.motivo, motivo->text().toStdString().c_str(), sizeof(r.motivo));
        strncpy(r.examenes, examen->text().toStdString().c_str(), sizeof(r.examenes));
        strncpy(r.resultados, resultado->text().toStdString().c_str(), sizeof(r.resultados));
        strncpy(r.receta, receta->text().toStdString().c_str(), sizeof(r.receta));

        insertarRegistro(r); // Inserta el registro en el archivo binario
        QMessageBox::information(&d, "Insertado", "Registro insertado correctamente.");
        d.accept();
    });

    d.exec();
}


// Método para eliminar registros por DNI o uno específico
void MainWindow::eliminar() {
    QDialog d(this);
    QVBoxLayout* layout = new QVBoxLayout(&d);
    QLineEdit* dniEdit = new QLineEdit;
    QPushButton* eliminarTodoBtn = new QPushButton("Eliminar TODOS los registros");
    QPushButton* eliminarUnoBtn = new QPushButton("Eliminar UN registro específico");
    layout->addWidget(new QLabel("DNI:"));
    layout->addWidget(dniEdit);
    layout->addWidget(eliminarTodoBtn);
    layout->addWidget(eliminarUnoBtn);

    // Elimina todos los registros de un DNI
    QObject::connect(eliminarTodoBtn, &QPushButton::clicked, [&]() {
        int dni = dniEdit->text().toInt();
        eliminarPorDNI(dni);
        QMessageBox::information(&d, "Eliminado", "Todos los registros del DNI fueron eliminados.");
        d.accept();
    });

    // Elimina un registro específico de un DNI (seleccionando cuál)
    QObject::connect(eliminarUnoBtn, &QPushButton::clicked, [&]() {
        int dni = dniEdit->text().toInt();
        auto registros = buscarRegistros(dni);
        if (registros.empty()) {
            QMessageBox::information(&d, "Sin registros", "No se encontraron registros.");
            return;
        }

        QStringList opciones;
        for (size_t i = 0; i < registros.size(); ++i) {
            RegistroClinico r;
            registros_file.seekg(registros[i]);
            registros_file.read(reinterpret_cast<char*>(&r), sizeof(r));
            opciones << QString("[" + QString::number(i + 1) + "] ") + r.fecha + " - " + r.motivo;
        }

        bool ok;
        QString elegido = QInputDialog::getItem(&d, "Elegir Registro", "Seleccione registro a eliminar:", opciones, 0, false, &ok);
        if (ok && !elegido.isEmpty()) {
            int idx = elegido.mid(1, elegido.indexOf("]") - 1).toInt() - 1;
            eliminarRegistroEspecifico(dni, idx);
            QMessageBox::information(&d, "Eliminado", "El registro fue eliminado correctamente.");
            d.accept();
        }
    });

    // Nota: se eliminó un bloque redundante que sobrescribía registros con ceros.

    d.exec();
}

// Función principal, inicia la aplicación Qt y la ventana principal
int main(int argc, char *argv[]) {
    QApplication app(argc, argv);
    inicializarArchivos(); // Prepara los archivos binarios
    MainWindow w;
    w.show();
    return app.exec();
}
