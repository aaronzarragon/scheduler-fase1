#include <iostream>
#include <pthread.h>
#include "shared_state.h"
#include "cli.h"
#include "scheduler.h"

// ============================================================
// main.cpp — Punto de entrada
// Crea dos hilos POSIX:
//   1. Hilo CLI   — entrada/salida interactiva
//   2. Hilo Motor — planificación de CPU (scheduling engine)
// ============================================================

int main() {
    pthread_t tid_cli, tid_sched;

    // Crear hilo motor (scheduler) primero — arranca en espera
    if (pthread_create(&tid_sched, nullptr, schedulerThread, nullptr) != 0) {
        std::cerr << "[FATAL]: No se pudo crear el hilo de scheduling.\n";
        return 1;
    }

    // Crear hilo CLI
    if (pthread_create(&tid_cli, nullptr, cliThread, nullptr) != 0) {
        std::cerr << "[FATAL]: No se pudo crear el hilo CLI.\n";
        return 1;
    }

    // Esperar a que el hilo CLI termine (el usuario escribió exit o EOF)
    pthread_join(tid_cli, nullptr);

    // Señalizar salida si no se hizo ya
    pthread_mutex_lock(&g_mutex);
    g_exit    = true;
    g_running = false;
    pthread_mutex_unlock(&g_mutex);

    pthread_join(tid_sched, nullptr);

    // Destruir mutex
    pthread_mutex_destroy(&g_mutex);

    std::cout << "[OK] Simulación finalizada limpiamente.\n";
    return 0;
}
