#include "cli.h"
#include "shared_state.h"
#include <iostream>
#include <sstream>
#include <vector>
#include <string>
#include <fstream>
#include <iomanip>
#include <algorithm>

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

// ---- Implementación de cada comando -------------------------

// create <nombre> <burst> [prioridad]
static void cmdCreate(const std::vector<std::string>& t) {
    if (t.size() < 3 || t.size() > 4) {
        std::cout << "[ERROR DE SINTAXIS]: Parámetros insuficientes.\n"
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
    std::ifstream file(path);
    if (!file.is_open()) {
        std::cout << "[ERROR]: No se puede abrir el archivo: " << path << "\n";
        return;
    }

    int loaded = 0;
    std::string line;
    while (std::getline(file, line)) {
        // Ignorar comentarios y líneas vacías
        if (line.empty() || line[0] == '#') continue;
        // Reemplazar comas por espacios para parsear
        for (char& c : line) if (c == ',') c = ' ';
        std::istringstream ss(line);
        std::string name;
        int burst, prio, arrival;
        if (!(ss >> name >> burst >> prio >> arrival)) {
            std::cout << "[AVISO]: Línea ignorada (formato inválido): " << line << "\n";
            continue;
        }
        if (burst <= 0 || prio < 1 || prio > 5 || arrival < 0) {
            std::cout << "[AVISO]: Línea ignorada (valores fuera de rango): " << line << "\n";
            continue;
        }

        pthread_mutex_lock(&g_mutex);
        PCB p;
        p.pid             = g_next_pid++;
        p.name            = name;
        p.burst_initial   = burst;
        p.burst_remaining = burst;
        p.priority        = prio;
        p.arrival_time    = arrival;
        p.state           = ProcessState::NEW; // el scheduler lo pasará a READY en arrival_time
        g_processes.push_back(p);
        pthread_mutex_unlock(&g_mutex);
        loaded++;
    }
    std::cout << "[OK] " << loaded << " proceso(s) cargado(s) desde " << path << "\n";
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
    int q = 2;
    if (t.size() == 3) {
        if (!isPositiveInt(t[2], q)) {
            std::cout << "[ERROR DE TIPO]: quantum debe ser un entero > 0.\n";
            return;
        }
    } else if (algo == "RR") {
        // quantum obligatorio para RR pero usamos defecto 2
    }

    pthread_mutex_lock(&g_mutex);
    g_algo    = (algo == "RR") ? SchedAlgo::RR : SchedAlgo::SRTF;
    g_quantum = q;
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
    pthread_mutex_unlock(&g_mutex);
    std::cout << "[OK] Motor de scheduling pausado. (tick=" << g_clock << ")\n";
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
        else if (cmd == "help")      cmdHelp();
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
