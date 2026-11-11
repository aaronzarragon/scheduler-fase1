#include "shared_state.h"
#include <algorithm>

// ============================================================
// shared_state.cpp — Inicialización de variables globales
// ============================================================

pthread_mutex_t g_mutex = PTHREAD_MUTEX_INITIALIZER;

std::vector<PCB>        g_processes;
std::deque<int>         g_ready_queue;
int                     g_next_pid   = 1;
int                     g_clock      = 0;

SchedAlgo               g_algo       = SchedAlgo::RR;
int                     g_quantum    = 2;
int                     g_speed_ms   = 1000;

bool                    g_running    = false;
bool                    g_exit       = false;

std::vector<GanttEntry> g_gantt;

// Busca un PCB por PID (debe llamarse con mutex tomado)
PCB* findPCB(int pid) {
    for (auto& p : g_processes)
        if (p.pid == pid) return &p;
    return nullptr;
}

// Cuenta procesos finalizados
int countFinished() {
    int c = 0;
    for (const auto& p : g_processes)
        if (p.state == ProcessState::TERMINATED) c++;
    return c;
}
