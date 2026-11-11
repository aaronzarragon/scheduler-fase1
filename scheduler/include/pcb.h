#pragma once
#include <string>

// ============================================================
// pcb.h — Process Control Block
// Define el estado y los datos de cada proceso simulado.
// ============================================================

enum class ProcessState {
    NEW,
    READY,
    RUNNING,
    BLOCKED,
    TERMINATED
};

// Convierte un estado a su representación en texto
inline std::string stateToString(ProcessState s) {
    switch (s) {
        case ProcessState::NEW:        return "NEW";
        case ProcessState::READY:      return "READY";
        case ProcessState::RUNNING:    return "RUNNING";
        case ProcessState::BLOCKED:    return "BLOCKED";
        case ProcessState::TERMINATED: return "TERMINATED";
    }
    return "UNKNOWN";
}

struct PCB {
    int    pid;              // ID único autoincremental
    std::string name;        // Nombre del proceso (sin espacios)
    ProcessState state;      // Estado actual

    int    burst_initial;    // Ráfaga inicial (ticks)
    int    burst_remaining;  // Ráfaga restante (ticks)
    int    priority;         // Prioridad 1-5 (defecto 3)

    int    arrival_time;     // Tick de llegada al sistema
    int    start_time;       // Tick cuando se ejecutó por primera vez (-1 = nunca)
    int    finish_time;      // Tick cuando terminó (-1 = no terminado)

    // Métricas calculadas (se actualizan al finalizar o consultar)
    int    waiting_time;     // WT = TAT - burst_initial
    int    turnaround_time;  // TAT = finish_time - arrival_time
    int    response_time;    // RT = start_time - arrival_time

    PCB()
        : pid(0), state(ProcessState::NEW),
          burst_initial(0), burst_remaining(0), priority(3),
          arrival_time(0), start_time(-1), finish_time(-1),
          waiting_time(0), turnaround_time(0), response_time(0)
    {}
};
