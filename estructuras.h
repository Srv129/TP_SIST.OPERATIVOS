#ifndef ESTRUCTURAS_H_INCLUDED
#define ESTRUCTURAS_H_INCLUDED

#include <stdio.h>
#include <time.h>
#include <string.h>

// --- Nombres de Recursos POSIX ---
#define SHM_CONTROL_NAME "/shm_control_tarea"
#define SHM_DATOS_NAME "/shm_datos_tarea"
#define SEM_ID_MUTEX_NAME "/sem_id_mutex"
#define SEM_SHM_VACIA_NAME "/sem_shm_vacia"
#define SEM_SHM_LLENA_NAME "/sem_shm_llena"

#define BLOQUE_IDS 10

// --- Estructuras de Datos Compartidas ---

// Estructura del registro de datos
typedef struct {
    long id;
    int valor_aleatorio;
    char tiempo_generacion[20];
} Registro;

// Estructura de control para variables compartidas críticas
typedef struct {
    long proximo_id;
    int registros_pendientes;
    long id_esperado;       // <--- ESTA LÍNEA DEBE ESTAR AQUÍ
} MemoriaControl;

// Función auxiliar (Definición)
void get_current_time_str(char *buffer, size_t size);


#endif // ESTRUCTURAS_H_INCLUDED



