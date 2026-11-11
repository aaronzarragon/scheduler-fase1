#pragma once
#include <vector>
#include <deque>
#include <string>
#include <pthread.h>
#include "pcb.h"

// ============================================================
// shared_state.h — Estado global compartido entre hilos
// Toda lectura/escritura debe hacerse bajo g_mutex.
// ============================================================

enum class SchedAlgo { RR, SRTF };

// ---- Gantt entry -----------------------------------------------
struct GanttEntry {
    int  tick_start;
    int  tick_end;
    std::string label;   // nombre del proceso o "IDLE"
};

// ---- Estado global ---------------------------------------------
extern pthread_mutex_t g_mutex;

extern std::vector<PCB>       g_processes;   // todos los PCBs creados
extern std::deque<int>        g_ready_queue; // PIDs en READY (cola RR)
extern int                    g_next_pid;    // próximo PID a asignar
extern int                    g_clock;       // tick simulado actual

extern SchedAlgo              g_algo;
extern int                    g_quantum;
extern int                    g_speed_ms;    // ms reales por tick

extern bool                   g_running;     // motor activo
extern bool                   g_exit;        // señal de salida

extern std::vector<GanttEntry> g_gantt;      // historial Gantt

// Helpers (no requieren mutex, llamar con mutex tomado)
PCB* findPCB(int pid);
int  countFinished();
