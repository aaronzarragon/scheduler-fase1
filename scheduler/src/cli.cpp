#include "cli.h"
#include "shared_state.h"
#include <iostream>
#include <sstream>
#include <vector>
#include <string>
#include <fstream>
#include <iomanip>
#include <algorithm>
#include <filesystem>

// ============================================================
// cli.cpp — Hilo de consola interactiva
// Parsea, valida y despacha todos los comandos del usuario.
// ============================================================

// ---- Utilidades de parseo -----------------------------------

static std::vector<std::string> tokenize(const std::string& line) {
    std::istringstream ss(line);
    std::string tok;
    std::vector<std::string> tokens;
    while (ss >> tok) tokens.push_back(tok);
    return tokens;
}

static bool isPositiveInt(const std::string& s, int& out) {
    try {
        size_t pos;
        int v = std::stoi(s, &pos);
        if (pos != s.size()) return false;
        out = v;
        return v > 0;
    } catch (...) { return false; }
}

static bool isNonNegInt(const std::string& s, int& out) {
    try {
        size_t pos;
        int v = std::stoi(s, &pos);
        if (pos != s.size()) return false;
        out = v;
        return v >= 0;
    } catch (...) { return false; }
}

// Quita espacios y tabulaciones al inicio y al final
static std::string trim(const std::string& s) {
    size_t a = s.find_first_not_of(" \t");
    if (a == std::string::npos) return "";
    size_t b = s.find_last_not_of(" \t");
    return s.substr(a, b - a + 1);
}

// Divide una línea por comas y recorta cada campo
static std::vector<std::string> splitCSV(const std::string& line) {
    std::vector<std::string> fields;
    std::string field;
    std::istringstream ss(line);
    while (std::getline(ss, field, ',')) fields.push_back(trim(field));
    if (!line.empty() && line.back() == ',') fields.push_back(""); // coma final = campo vacío
    return fields;
}

// ---- Implementación de cada comando -------------------------

// create <nombre> <burst> [prioridad]
static void cmdCreate(const std::vector<std::string>& t) {
    if (t.size() < 3 || t.size() > 4) {
        std::cout << "[ERROR DE SINTAXIS]: "
                  << (t.size() < 3 ? "Parámetros insuficientes.\n" : "Demasiados parámetros.\n")
                  << "Uso correcto: create <nombre> <burst_time> [prioridad]\n";
        return;
    }
    // nombre sin espacios (ya garantizado por tokenizer)
    std::string name = t[1];
    int burst, prio = 3;
    if (!isPositiveInt(t[2], burst)) {
        std::cout << "[ERROR DE TIPO]: <burst_time> debe ser un entero > 0.\n";
        return;
    }
    if (t.size() == 4) {
        if (!isNonNegInt(t[3], prio) || prio < 1 || prio > 5) {
            std::cout << "[ERROR DE RANGO]: [prioridad] debe ser un entero entre 1 y 5.\n";
            return;
        }
    }

    pthread_mutex_lock(&g_mutex);
    PCB p;
    p.pid            = g_next_pid++;
    p.name           = name;
    p.state          = ProcessState::NEW;
    p.burst_initial  = burst;
    p.burst_remaining= burst;
    p.priority       = prio;
    p.arrival_time   = g_clock;
    // Pasar a READY inmediatamente (llegada en este tick)
    p.state = ProcessState::READY;
    g_processes.push_back(p);
    // Agregar a la cola RR
    g_ready_queue.push_back(p.pid);
    int assigned_pid = p.pid;
    pthread_mutex_unlock(&g_mutex);

    std::cout << "[OK] Proceso creado: PID=" << assigned_pid
              << " Nombre=" << name
              << " Burst=" << burst
              << " Prioridad=" << prio << "\n";
}

// load <archivo>
static void cmdLoad(const std::vector<std::string>& t) {
    if (t.size() != 2) {
        std::cout << "[ERROR DE SINTAXIS]: Uso correcto: load <archivo>\n";
        return;
    }
    std::string path = t[1];

    // Validar existencia: debe ser un archivo regular (no una carpeta)
    std::error_code ec;
    if (!std::filesystem::exists(path, ec)) {
        std::cout << "[ERROR]: El archivo no existe: " << path << "\n";
        return;
    }
    if (!std::filesystem::is_regular_file(path, ec)) {
        std::cout << "[ERROR]: La ruta no es un archivo: " << path << "\n";
        return;
    }
    std::ifstream file(path);
    if (!file.is_open()) {
        std::cout << "[ERROR]: No se puede abrir el archivo: " << path << "\n";
        return;
    }

    // Fase 1: validar TODO el archivo antes de insertar procesos.
    // Formato por línea: NOMBRE, BURST_TIME, PRIORIDAD, ARRIVAL_TIME
    struct Entry { std::string name; int burst, prio, arrival; };
    std::vector<Entry> entries;
    int errors = 0;
    int line_no = 0;
    std::string line;
    while (std::getline(file, line)) {
        line_no++;
        if (line_no == 1 && line.rfind("\xEF\xBB\xBF", 0) == 0) line.erase(0, 3); // BOM UTF-8 (Bloc de notas)
        if (!line.empty() && line.back() == '\r') line.pop_back(); // archivos con fin de línea Windows
        std::string trimmed = trim(line);
        if (trimmed.empty() || trimmed[0] == '#') continue;        // vacía o comentario

        std::vector<std::string> f = splitCSV(trimmed);
        Entry e;
        std::string why;
        if (f.size() != 4)
            why = "se esperaban 4 campos (NOMBRE, BURST_TIME, PRIORIDAD, ARRIVAL_TIME) y hay " + std::to_string(f.size());
        else if (f[0].empty() || f[0].find_first_of(" \t") != std::string::npos)
            why = "NOMBRE no puede estar vacío ni tener espacios";
        else if (!isPositiveInt(f[1], e.burst))
            why = "BURST_TIME debe ser un entero > 0";
        else if (!isNonNegInt(f[2], e.prio) || e.prio < 1 || e.prio > 5)
            why = "PRIORIDAD debe ser un entero entre 1 y 5";
        else if (!isNonNegInt(f[3], e.arrival))
            why = "ARRIVAL_TIME debe ser un entero >= 0";

        if (!why.empty()) {
            std::cout << "[ERROR DE FORMATO]: Línea " << line_no << ": " << why << ".\n";
            errors++;
            continue;
        }
        e.name = f[0];
        entries.push_back(e);
    }

    if (errors > 0) {
        std::cout << "[ERROR]: El archivo tiene " << errors
                  << " línea(s) inválida(s). No se cargó ningún proceso.\n";
        return;
    }
    if (entries.empty()) {
        std::cout << "[ERROR]: El archivo no contiene procesos.\n";
        return;
    }

    // Fase 2: insertar todos los procesos en una sola sección crítica,
    // para que el motor nunca vea el lote a medias.
    pthread_mutex_lock(&g_mutex);
    for (const auto& e : entries) {
        PCB p;
        p.pid             = g_next_pid++;
        p.name            = e.name;
        p.burst_initial   = e.burst;
        p.burst_remaining = e.burst;
        p.priority        = e.prio;
        p.arrival_time    = e.arrival;
        // Si su arrival_time ya llegó, entra directo a READY;
        // si no, queda en NEW y el scheduler lo pasará a READY en arrival_time.
        if (e.arrival <= g_clock) {
            p.state = ProcessState::READY;
            g_ready_queue.push_back(p.pid);
        } else {
            p.state = ProcessState::NEW;
        }
        g_processes.push_back(p);
    }
    pthread_mutex_unlock(&g_mutex);

    std::cout << "[OK] " << entries.size() << " proceso(s) cargado(s) desde " << path << "\n";
}

// ps
static void cmdPs(const std::vector<std::string>& t) {
    if (t.size() != 1) {
        std::cout << "[ERROR DE SINTAXIS]: El comando ps no acepta argumentos.\n";
        return;
    }
    pthread_mutex_lock(&g_mutex);
    std::cout << "\n"
              << std::left
              << std::setw(6)  << "PID"
              << std::setw(16) << "NOMBRE"
              << std::setw(12) << "ESTADO"
              << std::setw(16) << "BURST RESTANTE"
              << std::setw(10) << "PRIORIDAD"
              << "\n";
    std::cout << std::string(62, '-') << "\n";
    for (const auto& p : g_processes) {
        std::cout << std::left
                  << std::setw(6)  << p.pid
                  << std::setw(16) << p.name
                  << std::setw(12) << stateToString(p.state)
                  << std::setw(16) << (std::to_string(p.burst_remaining) + "s")
                  << std::setw(10) << p.priority
                  << "\n";
    }
    if (g_processes.empty()) std::cout << "  (sin procesos)\n";
    std::cout << "\n";
    pthread_mutex_unlock(&g_mutex);
}

// pcb <PID>
static void cmdPcb(const std::vector<std::string>& t) {
    if (t.size() != 2) {
        std::cout << "[ERROR DE SINTAXIS]: Uso correcto: pcb <PID>\n";
        return;
    }
    int pid;
    if (!isPositiveInt(t[1], pid)) {
        std::cout << "[ERROR DE TIPO]: El PID debe ser un número entero positivo.\n";
        return;
    }

    pthread_mutex_lock(&g_mutex);
    PCB* p = findPCB(pid);
    if (!p) {
        std::cout << "[ERROR]: No existe un proceso con PID=" << pid << "\n";
        pthread_mutex_unlock(&g_mutex);
        return;
    }

    int  wt  = (p->state == ProcessState::TERMINATED) ? p->waiting_time
                 : g_clock - p->arrival_time - (p->burst_initial - p->burst_remaining);
    std::string tat_str = (p->state == ProcessState::TERMINATED)
                            ? std::to_string(p->turnaround_time) + " ticks"
                            : "N/A (En ejecución)";
    int rt = (p->start_time == -1) ? -1 : p->start_time - p->arrival_time;

    std::cout << "\n" << std::string(50, '=') << "\n"
              << "        CONTROL BLOCK DE PROCESO (PCB) - PID: " << pid << "\n"
              << std::string(50, '=') << "\n"
              << std::left << std::setw(22) << "Nombre del Proceso" << ": " << p->name << "\n"
              << std::setw(22) << "Estado Actual"       << ": " << stateToString(p->state) << "\n"
              << std::setw(22) << "Prioridad"           << ": " << p->priority << "\n"
              << std::setw(22) << "Tiempo de Llegada"   << ": " << p->arrival_time << " ticks\n"
              << std::setw(22) << "Ráfaga Inicial"      << ": " << p->burst_initial << " ticks\n"
              << std::setw(22) << "Ráfaga Restante"     << ": " << p->burst_remaining << " ticks\n"
              << std::setw(22) << "Tiempo de Espera"    << ": " << (wt < 0 ? 0 : wt) << " ticks\n"
              << std::setw(22) << "Tiempo de Retorno"   << ": " << tat_str << "\n"
              << std::setw(22) << "Tiempo de Respuesta" << ": "
              << (rt == -1 ? "N/A" : std::to_string(rt) + " ticks") << "\n"
              << std::string(50, '=') << "\n\n";

    pthread_mutex_unlock(&g_mutex);
}

// set_sched <algoritmo> [quantum]
static void cmdSetSched(const std::vector<std::string>& t) {
    if (t.size() < 2 || t.size() > 3) {
        std::cout << "[ERROR DE SINTAXIS]: Uso correcto: set_sched <RR|SRTF> [quantum]\n";
        return;
    }
    std::string algo = t[1];
    if (algo != "RR" && algo != "SRTF") {
        std::cout << "[ERROR DE PARAMETRO]: Algoritmo no reconocido. Algoritmos válidos: RR, SRTF.\n";
        return;
    }
    int q = 2; // quantum por defecto para RR
    if (t.size() == 3) {
        if (algo == "SRTF") {
            std::cout << "[ERROR DE SINTAXIS]: SRTF no usa quantum. Uso correcto: set_sched SRTF\n";
            return;
        }
        if (!isPositiveInt(t[2], q)) {
            std::cout << "[ERROR DE TIPO]: quantum debe ser un entero > 0.\n";
            return;
        }
    }

    pthread_mutex_lock(&g_mutex);
    g_algo = (algo == "RR") ? SchedAlgo::RR : SchedAlgo::SRTF;
    if (algo == "RR") g_quantum = q; // SRTF conserva el quantum anterior
    pthread_mutex_unlock(&g_mutex);

    std::cout << "[OK] Algoritmo configurado: " << algo
              << (algo == "RR" ? " (quantum=" + std::to_string(q) + ")" : "") << "\n";
}

// set_speed <ms>
static void cmdSetSpeed(const std::vector<std::string>& t) {
    if (t.size() != 2) {
        std::cout << "[ERROR DE SINTAXIS]: Uso correcto: set_speed <ms_por_tick>\n";
        return;
    }
    int ms;
    if (!isPositiveInt(t[1], ms) || ms < 50 || ms > 5000) {
        std::cout << "[ERROR DE RANGO]: ms_por_tick debe ser un entero entre 50 y 5000.\n";
        return;
    }
    pthread_mutex_lock(&g_mutex);
    g_speed_ms = ms;
    pthread_mutex_unlock(&g_mutex);
    std::cout << "[OK] Velocidad configurada: " << ms << " ms/tick\n";
}

// start
static void cmdStart(const std::vector<std::string>& t) {
    if (t.size() != 1) {
        std::cout << "[ERROR DE SINTAXIS]: El comando start no acepta argumentos.\n";
        return;
    }
    pthread_mutex_lock(&g_mutex);
    g_running = true;
    pthread_mutex_unlock(&g_mutex);
    std::cout << "[OK] Motor de scheduling iniciado/reanudado.\n";
}

// pause
static void cmdPause(const std::vector<std::string>& t) {
    if (t.size() != 1) {
        std::cout << "[ERROR DE SINTAXIS]: El comando pause no acepta argumentos.\n";
        return;
    }
    pthread_mutex_lock(&g_mutex);
    g_running = false;
    int tick = g_clock;   // leer el reloj dentro de la sección crítica
    pthread_mutex_unlock(&g_mutex);
    std::cout << "[OK] Motor de scheduling pausado. (tick=" << tick << ")\n";
}

// gantt
static void cmdGantt(const std::vector<std::string>& t) {
    if (t.size() != 1) {
        std::cout << "[ERROR DE SINTAXIS]: El comando gantt no acepta argumentos.\n";
        return;
    }
    pthread_mutex_lock(&g_mutex);
    if (g_gantt.empty()) {
        std::cout << "DIAGRAMA DE GANTT: (vacío — el motor no ha ejecutado nada)\n";
        pthread_mutex_unlock(&g_mutex);
        return;
    }
    int t_end = g_gantt.back().tick_end;
    std::cout << "\nDIAGRAMA DE GANTT (0 - " << t_end << " ticks):\n";
    for (size_t i = 0; i < g_gantt.size(); i++) {
        const auto& e = g_gantt[i];
        std::cout << "[" << e.tick_start << "-" << e.tick_end << "s: " << e.label << "]";
        if (i + 1 < g_gantt.size()) std::cout << " -> ";
    }
    std::cout << "\n\n";
    pthread_mutex_unlock(&g_mutex);
}

// stats
static void cmdStats(const std::vector<std::string>& t) {
    if (t.size() != 1) {
        std::cout << "[ERROR DE SINTAXIS]: El comando stats no acepta argumentos.\n";
        return;
    }
    pthread_mutex_lock(&g_mutex);

    int total     = (int)g_processes.size();
    int finished  = countFinished();
    double avg_wt = 0, avg_tat = 0, avg_rt = 0;
    int cpu_ticks = 0;

    for (const auto& p : g_processes) {
        if (p.state == ProcessState::TERMINATED) {
            avg_wt  += p.waiting_time;
            avg_tat += p.turnaround_time;
            int rt   = (p.start_time == -1) ? 0 : p.start_time - p.arrival_time;
            avg_rt  += rt;
        }
    }
    for (const auto& e : g_gantt)
        if (e.label != "IDLE") cpu_ticks += (e.tick_end - e.tick_start);

    double cpu_pct = (g_clock > 0) ? 100.0 * cpu_ticks / g_clock : 0.0;

    if (finished > 0) {
        avg_wt  /= finished;
        avg_tat /= finished;
        avg_rt  /= finished;
    }

    std::cout << "\nESTADÍSTICAS DEL SISTEMA:\n"
              << "- Procesos Finalizados       : " << finished << " / " << total << "\n"
              << std::fixed << std::setprecision(2)
              << "- Tiempo Promedio de Espera  : " << avg_wt  << " ticks\n"
              << "- Tiempo Promedio de Retorno : " << avg_tat << " ticks\n"
              << "- Tiempo Promedio de Respuesta: "<< avg_rt  << " ticks\n"
              << "- Uso de CPU                 : " << cpu_pct << "%\n\n";

    pthread_mutex_unlock(&g_mutex);
}

// help
static void cmdHelp() {
    std::cout << "\n=== MANUAL RÁPIDO DE LA CLI ===\n"
              << "  create <nombre> <burst> [prioridad]  — Crear proceso\n"
              << "  load <archivo>                        — Cargar workload desde archivo\n"
              << "  ps                                    — Listar todos los procesos\n"
              << "  pcb <PID>                             — Ver PCB completo de un proceso\n"
              << "  set_sched <RR|SRTF> [quantum]         — Configurar algoritmo\n"
              << "  set_speed <ms>                        — Configurar velocidad (50-5000ms)\n"
              << "  start                                 — Iniciar/reanudar el motor\n"
              << "  pause                                 — Pausar el motor\n"
              << "  gantt                                 — Ver diagrama de Gantt\n"
              << "  stats                                 — Ver métricas del sistema\n"
              << "  help                                  — Este manual\n"
              << "  exit                                  — Salir de la simulación\n\n";
}

// ---- Bucle principal del hilo CLI ---------------------------

void* cliThread(void* /*arg*/) {
    std::string line;
    std::cout << "\n=== Simulador de Kernel — Shell & Scheduling Engine ===\n";
    std::cout << "Escriba 'help' para ver los comandos disponibles.\n\n";

    while (true) {
        std::cout << "> ";
        std::cout.flush();

        if (!std::getline(std::cin, line)) break; // EOF

        auto tokens = tokenize(line);
        if (tokens.empty()) continue;

        std::string cmd = tokens[0];

        if      (cmd == "create")    cmdCreate(tokens);
        else if (cmd == "load")      cmdLoad(tokens);
        else if (cmd == "ps")        cmdPs(tokens);
        else if (cmd == "pcb")       cmdPcb(tokens);
        else if (cmd == "set_sched") cmdSetSched(tokens);
        else if (cmd == "set_speed") cmdSetSpeed(tokens);
        else if (cmd == "start")     cmdStart(tokens);
        else if (cmd == "pause")     cmdPause(tokens);
        else if (cmd == "gantt")     cmdGantt(tokens);
        else if (cmd == "stats")     cmdStats(tokens);
        else if (cmd == "help") {
            if (tokens.size() != 1)
                std::cout << "[ERROR DE SINTAXIS]: El comando help no acepta argumentos.\n";
            else
                cmdHelp();
        }
        else if (cmd == "exit" && tokens.size() != 1) {
            std::cout << "[ERROR DE SINTAXIS]: El comando exit no acepta argumentos.\n";
        }
        else if (cmd == "exit") {
            std::cout << "[OK] Cerrando simulación. Liberando recursos...\n";
            pthread_mutex_lock(&g_mutex);
            g_running = false;
            g_exit    = true;
            pthread_mutex_unlock(&g_mutex);
            break;
        }
        else {
            std::cout << "[ERROR]: Comando desconocido: '" << cmd
                      << "'. Escriba 'help' para ver los comandos disponibles.\n";
        }
    }
    return nullptr;
}
