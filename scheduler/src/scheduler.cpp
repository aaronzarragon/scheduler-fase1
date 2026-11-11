#include "scheduler.h"
#include "shared_state.h"
#include <chrono>
#include <thread>
#include <algorithm>
#include <climits>

// ============================================================
// scheduler.cpp — Motor de planificación de CPU
// Ejecuta en su propio hilo POSIX (schedulerThread).
// ============================================================

// Selecciona el PID con menor ráfaga restante (SRTF)
static int selectSRTF() {
    int best_pid = -1;
    int best_bt  = INT_MAX;
    for (const auto& p : g_processes) {
        if (p.state == ProcessState::READY) {
            if (p.burst_remaining < best_bt) {
                best_bt  = p.burst_remaining;
                best_pid = p.pid;
            }
        }
    }
    return best_pid;
}

// Selecciona el frente de la cola READY para RR
static int selectRR() {
    // Busca el primer PID en g_ready_queue que esté en READY
    while (!g_ready_queue.empty()) {
        int pid = g_ready_queue.front();
        PCB* p  = findPCB(pid);
        if (p && p->state == ProcessState::READY)
            return pid;
        g_ready_queue.pop_front();
    }
    return -1;
}

int selectNext() {
    if (g_algo == SchedAlgo::SRTF) return selectSRTF();
    return selectRR();
}

// Agrega/actualiza entrada Gantt
static void ganttRecord(int tick_start, int tick_end, const std::string& label) {
    // Fusiona con la última entrada si es el mismo proceso
    if (!g_gantt.empty() && g_gantt.back().label == label &&
        g_gantt.back().tick_end == tick_start) {
        g_gantt.back().tick_end = tick_end;
    } else {
        g_gantt.push_back({tick_start, tick_end, label});
    }
}

void* schedulerThread(void* /*arg*/) {
    int rr_ticks = 0;      // ticks ejecutados del quantum actual
    int current_pid = -1;  // PID en ejecución ahora

    while (true) {
        // 1. Verificar señal de salida
        pthread_mutex_lock(&g_mutex);
        bool should_exit = g_exit;
        pthread_mutex_unlock(&g_mutex);
        if (should_exit) break;

        // 2. Verificar si el motor está corriendo
        pthread_mutex_lock(&g_mutex);
        bool is_running = g_running;
        pthread_mutex_unlock(&g_mutex);

        if (!is_running) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            continue;
        }

        // 3. Tick de CPU ----------------------------------------
        pthread_mutex_lock(&g_mutex);

        // 3.0 Si hay procesos y todos ya terminaron, no hay trabajo pendiente:
        //     el reloj se congela para no acumular IDLE al final del Gantt
        //     (eso distorsionaría el diagrama y el % de uso de CPU).
        //     Si no hay ningún proceso, el reloj avanza en IDLE (ver guía, 'start').
        if (!g_processes.empty() &&
            countFinished() == (int)g_processes.size()) {
            pthread_mutex_unlock(&g_mutex);
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            continue;
        }

        // 3a. Revisar si llegan procesos del batch en este tick
        for (auto& p : g_processes) {
            if (p.state == ProcessState::NEW && p.arrival_time <= g_clock) {
                p.state = ProcessState::READY;
                // Para RR: agregar a la cola si no está
                bool in_q = false;
                for (int id : g_ready_queue) if (id == p.pid) { in_q = true; break; }
                if (!in_q) g_ready_queue.push_back(p.pid);
            }
        }

        // 3b. SRTF: si hay un proceso corriendo con más BT que otro en READY → desapropiación
        if (g_algo == SchedAlgo::SRTF && current_pid != -1) {
            PCB* cur = findPCB(current_pid);
            int  best_other = selectSRTF(); // mejor READY (no incluye RUNNING)
            if (best_other != -1 && best_other != current_pid) {
                PCB* other = findPCB(best_other);
                if (cur && other && other->burst_remaining < cur->burst_remaining) {
                    // Desapropiación
                    cur->state = ProcessState::READY;
                    g_ready_queue.push_back(cur->pid);
                    current_pid = -1;
                    rr_ticks = 0;
                }
            }
        }

        // 3c. Elegir proceso si no hay ninguno corriendo
        if (current_pid == -1) {
            int pid = selectNext();
            if (pid != -1) {
                PCB* p = findPCB(pid);
                p->state = ProcessState::RUNNING;
                if (p->start_time == -1) p->start_time = g_clock;
                current_pid = pid;
                rr_ticks = 0;
                // Remover de g_ready_queue si RR
                if (g_algo == SchedAlgo::RR) {
                    for (auto it = g_ready_queue.begin(); it != g_ready_queue.end(); ++it) {
                        if (*it == pid) { g_ready_queue.erase(it); break; }
                    }
                }
            }
        }

        // 3d. Ejecutar un tick
        int tick_now = g_clock;
        if (current_pid != -1) {
            PCB* p = findPCB(current_pid);
            if (p) {
                p->burst_remaining--;
                rr_ticks++;
                ganttRecord(tick_now, tick_now + 1, p->name);

                // ¿Terminó?
                if (p->burst_remaining <= 0) {
                    p->state = ProcessState::TERMINATED;
                    p->finish_time = g_clock + 1;
                    p->turnaround_time = p->finish_time - p->arrival_time;
                    p->waiting_time    = p->turnaround_time - p->burst_initial;
                    p->response_time   = (p->start_time == -1) ? 0
                                         : p->start_time - p->arrival_time;
                    current_pid = -1;
                    rr_ticks = 0;
                }
                // ¿Se acabó el quantum (RR)?
                else if (g_algo == SchedAlgo::RR && rr_ticks >= g_quantum) {
                    p->state = ProcessState::READY;
                    g_ready_queue.push_back(p->pid);
                    current_pid = -1;
                    rr_ticks = 0;
                }
            }
        } else {
            // CPU ociosa
            ganttRecord(tick_now, tick_now + 1, "IDLE");
        }

        g_clock++;

        int speed = g_speed_ms;
        pthread_mutex_unlock(&g_mutex);

        // 3e. Retardo real
        std::this_thread::sleep_for(std::chrono::milliseconds(speed));
    }
    return nullptr;
}
