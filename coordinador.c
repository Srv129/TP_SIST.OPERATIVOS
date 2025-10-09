#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h> // Incluido para ftruncate
#include <sys/wait.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <semaphore.h>
#include <errno.h>
#include "estructuras.h"

// Punteros globales para acceso a la SHM y semáforos
MemoriaControl *shm_control = NULL;
Registro *shm_datos = NULL;
sem_t *sem_id_mutex = NULL, *sem_shm_vacia = NULL, *sem_shm_llena = NULL;
int fd_control = -1, fd_datos = -1;

void limpiar_ipc(void) {
    printf("\n[Coordinador %d] Iniciando limpieza de recursos IPC...\n", getpid());

    // 1. Cierre y Desmapeo
    if (shm_control != NULL) munmap(shm_control, sizeof(MemoriaControl));
    if (shm_datos != NULL) munmap(shm_datos, sizeof(Registro));

    // 2. Unlink (Eliminación) de SHM
    if (fd_control != -1) shm_unlink(SHM_CONTROL_NAME);
    if (fd_datos != -1) shm_unlink(SHM_DATOS_NAME);

    // 3. Cierre y Unlink (Eliminación) de Semáforos
    if (sem_id_mutex != NULL) { sem_close(sem_id_mutex); sem_unlink(SEM_ID_MUTEX_NAME); }
    if (sem_shm_vacia != NULL) { sem_close(sem_shm_vacia); sem_unlink(SEM_SHM_VACIA_NAME); }
    if (sem_shm_llena != NULL) { sem_close(sem_shm_llena); sem_unlink(SEM_SHM_LLENA_NAME); }

    printf("[Coordinador %d] Limpieza de recursos IPC finalizada.\n", getpid());
}

int inicializar_ipc(int total_registros) {
    // 1. Crear y mapear SHM de Control
    if ((fd_control = shm_open(SHM_CONTROL_NAME, O_CREAT | O_RDWR, 0666)) == -1) { perror("shm_open control"); return 0; }
    if (ftruncate(fd_control, sizeof(MemoriaControl)) == -1) { perror("ftruncate control"); return 0; }
    shm_control = mmap(NULL, sizeof(MemoriaControl), PROT_READ | PROT_WRITE, MAP_SHARED, fd_control, 0);
    if (shm_control == MAP_FAILED) { perror("mmap control"); return 0; }

    shm_control->proximo_id = 1;
    shm_control->registros_pendientes = total_registros;
    shm_control->id_esperado = 1; // CLAVE: Inicialización del ID esperado.

    // 2. Crear y mapear SHM de Datos (Buzón)
    if ((fd_datos = shm_open(SHM_DATOS_NAME, O_CREAT | O_RDWR, 0666)) == -1) { perror("shm_open datos"); return 0; }
    if (ftruncate(fd_datos, sizeof(Registro)) == -1) { perror("ftruncate datos"); return 0; }
    shm_datos = mmap(NULL, sizeof(Registro), PROT_READ | PROT_WRITE, MAP_SHARED, fd_datos, 0);
    if (shm_datos == MAP_FAILED) { perror("mmap datos"); return 0; }

    // 3. Crear Semáforos
    if ((sem_id_mutex = sem_open(SEM_ID_MUTEX_NAME, O_CREAT, 0666, 1)) == SEM_FAILED) { perror("sem_open mutex"); return 0; }
    if ((sem_shm_vacia = sem_open(SEM_SHM_VACIA_NAME, O_CREAT, 0666, 1)) == SEM_FAILED) { perror("sem_open vacia"); return 0; }
    if ((sem_shm_llena = sem_open(SEM_SHM_LLENA_NAME, O_CREAT, 0666, 0)) == SEM_FAILED) { perror("sem_open llena"); return 0; }

    return 1; // Éxito
}

int main(int argc, char *argv[]) {
    // 1. VALIDACIÓN DE PARÁMETROS
    if (argc != 3) {
        printf("Uso: %s <num_generadores> <total_registros>\n", argv[0]);
        return 1;
    }

    long num_generadores_l = strtol(argv[1], NULL, 10);
    long total_registros_l = strtol(argv[2], NULL, 10);

    if (num_generadores_l <= 0 || total_registros_l <= 0) {
        printf("Error: Ambos parámetros deben ser números enteros positivos.\n");
        return 1;
    }
    int num_generadores = (int)num_generadores_l;
    int total_registros = (int)total_registros_l;

    atexit(limpiar_ipc);

    // 2. INICIALIZACIÓN DE RECURSOS
    if (!inicializar_ipc(total_registros)) {
        return 1;
    }

    FILE *archivo_csv = fopen("datos_prueba.csv", "w");
    if (archivo_csv == NULL) {
        perror("Error al abrir datos_prueba.csv");
        return 1;
    }
    fprintf(archivo_csv, "ID,Valor_Aleatorio,Tiempo_Generacion\n");

    // 3. CREACIÓN DE PROCESOS GENERADORES
    printf("[Coordinador %d] Lanzando %d procesos generadores...\n", getpid(), num_generadores);
    for (int i = 0; i < num_generadores; i++) {
        pid_t pid = fork();
        if (pid == 0) {
            // Código del Proceso Generador
            execlp("./generador", "generador", NULL);
            perror("execlp error al lanzar generador");
            exit(1);
        } else if (pid < 0) {
            perror("fork error");
        }
    }

    // 4. CICLO DE COORDINACIÓN (RECIBIR Y ESCRIBIR)
    int escritos = 0;
    while (escritos < total_registros) {
        // Esperar a que el generador escriba (buzón esté lleno)
        if (sem_wait(sem_shm_llena) == -1) {
            if (errno == EINTR) continue;
            perror("sem_wait llena"); break;
        }

        // Escribir el registro desde la SHM de Datos al CSV
        fprintf(archivo_csv, "%ld,%d,%s\n",
                shm_datos->id,
                shm_datos->valor_aleatorio,
                shm_datos->tiempo_generacion);

        // CLAVE DEL ORDENAMIENTO: Incremento el ID que espero
        shm_control->id_esperado++;

        escritos++;

        // Señalizar que el buzón de datos está vacío (Generador puede escribir de nuevo)
        if (sem_post(sem_shm_vacia) == -1) {
            perror("sem_post vacia"); break;
        }
    }

    // 5. FINALIZACIÓN Y ESPERA DE PROCESOS
    printf("[Coordinador %d] Esperando a que terminen los generadores...\n", getpid());
    int status;
    for (int i = 0; i < num_generadores; i++) {
        wait(&status);
    }

    fclose(archivo_csv);
    printf("[Coordinador %d] Proceso finalizado. Registros generados: %d.\n", getpid(), escritos);

    return 0;
}
