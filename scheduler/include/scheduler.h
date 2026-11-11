#pragma once
#include <pthread.h>

// ============================================================
// scheduler.h — Hilo Motor de Scheduling (CPU)
// ============================================================

void* schedulerThread(void* arg);

// Función interna: selecciona el próximo proceso según la política activa
// Retorna el PID elegido o -1 si no hay nada listo
int selectNext();
