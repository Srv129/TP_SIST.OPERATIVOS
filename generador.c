
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <time.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <semaphore.h>
#include <errno.h>
#include <sys/types.h> // Necesario para la conexi�n
#include "estructuras.h"

// Punteros globales
MemoriaControl *shm_control = NULL;
Registro *shm_datos = NULL;
sem_t *sem_id_mutex = NULL, *sem_shm_vacia = NULL, *sem_shm_llena = NULL;

// Implementaci�n de la funci�n auxiliar para obtener la hora actual
void get_current_time_str(char *buffer, size_t size) {
    time_t now = time(NULL);
    struct tm *tm_info = localtime(&now);
    strftime(buffer, size, "%H:%M:%S", tm_info);
}

int conectar_ipc(void) {
    // 1. Conectar y mapear SHM de Control
    int fd_control = shm_open(SHM_CONTROL_NAME, O_RDWR, 0666);
    if (fd_control == -1) { perror("shm_open control"); return 0; }
    shm_control = mmap(NULL, sizeof(MemoriaControl), PROT_READ | PROT_WRITE, MAP_SHARED, fd_control, 0);
    if (shm_control == MAP_FAILED) { perror("mmap control"); return 0; }
    close(fd_control);

    // 2. Conectar y mapear SHM de Datos
    int fd_datos = shm_open(SHM_DATOS_NAME, O_RDWR, 0666);
    if (fd_datos == -1) { perror("shm_open datos"); return 0; }
    shm_datos = mmap(NULL, sizeof(Registro), PROT_READ | PROT_WRITE, MAP_SHARED, fd_datos, 0);
    if (shm_datos == MAP_FAILED) { perror("mmap datos"); return 0; }
    close(fd_datos);

    // 3. Conectar Sem�foros
    if ((sem_id_mutex = sem_open(SEM_ID_MUTEX_NAME, 0)) == SEM_FAILED) { perror("sem_open mutex"); return 0; }
    if ((sem_shm_vacia = sem_open(SEM_SHM_VACIA_NAME, 0)) == SEM_FAILED) { perror("sem_open vacia"); return 0; }
    if ((sem_shm_llena = sem_open(SEM_SHM_LLENA_NAME, 0)) == SEM_FAILED) { perror("sem_open llena"); return 0; }

    return 1;
}

int main(void) {
    if (!conectar_ipc()) {
        fprintf(stderr, "[Generador %d] Fallo al conectar con IPCs. Saliendo.\n", getpid());
        return 1;
    }
    // Inicializaci�n del generador de n�meros aleatorios con el PID
    srand(time(NULL) ^ getpid());
    printf("[Generador %d] Conectado y trabajando.\n", getpid());

    while (1) {
        long current_id = 0;

        // --- 1. SOLICITAR ID (SECCI�N CR�TICA) ---
        if (sem_wait(sem_id_mutex) == -1) {
             if (errno == EINTR) continue;
             perror("sem_wait mutex"); break;
        }

        // Criterio de parada
        if (shm_control->registros_pendientes <= 0) {
            sem_post(sem_id_mutex);
            break;
        }

        // Asignar y actualizar el contador global (solo 1 ID a la vez - BLOQUE_IDS=1)
        current_id = shm_control->proximo_id;
        shm_control->proximo_id += BLOQUE_IDS; // BLOQUE_IDS debe ser 1
        shm_control->registros_pendientes -= BLOQUE_IDS;

        sem_post(sem_id_mutex); // Liberar el acceso al contador

        // --- 2. GENERAR REGISTRO ---
        Registro nuevo_registro;
        nuevo_registro.id = current_id;
        nuevo_registro.valor_aleatorio = rand() % 1000;
        get_current_time_str(nuevo_registro.tiempo_generacion, sizeof(nuevo_registro.tiempo_generacion));

        // --- 3. ENVIAR EL REGISTRO (ESPERA POR TURNO Y SINC. PRODUCCI�N) ---
        while (1) {
            // A) ESPERAR A QUE EL BUZ�N EST� VAC�O
            if (sem_wait(sem_shm_vacia) == -1) {
                if (errno == EINTR) continue;
                perror("sem_wait vacia"); exit(1);
            }

            // B) VERIFICAR TURNO ANTES DE ESCRIBIR
            if (nuevo_registro.id == shm_control->id_esperado) {
                // **ES NUESTRO TURNO:** Escribimos y se�alamos 'llena'
                memcpy(shm_datos, &nuevo_registro, sizeof(Registro));

                if (sem_post(sem_shm_llena) == -1) {
                    perror("sem_post llena"); exit(1);
                }
                break; // �XITO: Salir del bucle while(1) de espera
            } else {
                // **NO ES NUESTRO TURNO:** Liberamos el buz�n para el generador correcto
                sem_post(sem_shm_vacia);
                usleep(1000); // Pausa de 1ms para darle la oportunidad a otro proceso.
            }
        }
    }

    // Limpieza al salir
    munmap(shm_control, sizeof(MemoriaControl));
    munmap(shm_datos, sizeof(Registro));

    printf("[Generador %d] Proceso terminado.\n", getpid());
    return 0;
}
