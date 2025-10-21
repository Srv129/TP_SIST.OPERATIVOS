#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/sem.h>
#include <sys/wait.h>
#include <signal.h>
#include <stdbool.h>

//--- CONFIGURACIÓN PRINCIPAL ---//

// Nombre del archivo de salida
const char* NOMBRE_ARCHIVO_SALIDA = "output.csv"; 
// Cantidad de IDs que cada generador solicita a la vez
const int TAMANIO_BLOQUE_IDS = 10;
// Clave única para la memoria compartida (SHM)
const key_t KEY_MEMORIA_COMPARTIDA = 1234;
// Clave única para los semáforos
const key_t KEY_SEMAFOROS = 5678;

#define MIN(x, y) (((x) < (y)) ? (x) : (y))

// Estructura que se almacenará en la memoria compartida
struct DatosCompartidos {
    long proximo_id;
    long total_registros_a_generar;
    bool coordinador_finalizo;

    struct Registro {
        long id;
        char nombre_producto[50];
        int cantidad;
        double precio;
    } buffer_registro;
};

// Índices para cada semáforo dentro del conjunto
enum {
    SEMAFORO_MUTEX_IDS,      // 0: Exclusión mutua para asignar IDs
    SEMAFORO_BUFFER_LLENO,   // 1: Señal para el coordinador (hay un registro listo)
    SEMAFORO_BUFFER_VACIO    // 2: Señal para los generadores (el buffer está libre)
};


//--- VARIABLES GLOBALES PARA GESTIÓN DE RECURSOS ---//

// ID del segmento de memoria compartida
int id_memoria_compartida = -1;
// ID del conjunto de semáforos
int id_semaforos = -1;
// Array para almacenar los PIDs de los procesos hijos
pid_t* pids_hijos = NULL;
// Contador de procesos hijos creados
int cantidad_hijos = 0;

//--- FUNCIONES AUXILIARES ---//

// Muestra cómo usar el programa
void mostrar_ayuda(const char* nombre_programa) {
    fprintf(stderr, "Uso: %s <cantidad_generadores> <total_registros>\n", nombre_programa);
    fprintf(stderr, "Ejemplo: %s 5 1000\n", nombre_programa);
}

// Libera todos los recursos IPC (memoria y semáforos)
void liberar_recursos_ipc() {
    // Elimina la memoria compartida
    if (id_memoria_compartida != -1) {
        shmctl(id_memoria_compartida, IPC_RMID, NULL);
        printf("[PADRE] Memoria compartida eliminada.\n");
    }
    // Elimina los semáforos
    if (id_semaforos != -1) {
        semctl(id_semaforos, 0, IPC_RMID, NULL);
        printf("[PADRE] Semáforos eliminados.\n");
    }
    // Libera la memoria del array de PIDs
    if (pids_hijos != NULL) {
        free(pids_hijos);
    }
}


// Función para limpiar recursos
void manejador_senial_interrupcion(int numero_senial) {
    printf("\nSeñal %d recibida. Finalizando de forma controlada...\n", numero_senial);
    // Termina a todos los procesos hijos para que no queden huérfanos
    for (int i = 0; i < cantidad_hijos; i++) {
        kill(pids_hijos[i], SIGTERM);
    }
    // Llama a la función de limpieza
    liberar_recursos_ipc();
    exit(numero_senial);
}

// Función para operar sobre un semáforo
void operar_semaforo(int id_semaforo, unsigned short indice_semaforo, short operacion) {
    // Estructura para la operación del semáforo
    struct sembuf formulario_operacion = {indice_semaforo, operacion, 0};

    if (semop(id_semaforo, &formulario_operacion, 1) == -1) {
        perror("semop");
        exit(EXIT_FAILURE);
    }
}

//--- LÓGICA DE LOS PROCESOS HIJOS ---//
// Lógica del Proceso Coordinador (consumidor)
void ejecutar_proceso_coordinador() {
    // Restaura el comportamiento por defecto
    signal(SIGINT, SIG_DFL);

    // Conecta este proceso a la memoria compartida
    struct DatosCompartidos* datos = (struct DatosCompartidos*)shmat(id_memoria_compartida, NULL, 0);
    if (datos == (void*)-1) {
        perror("shmat en coordinador");
        exit(EXIT_FAILURE);
    }

    // Abre el archivo CSV en modo "append" para añadir registros
    FILE* archivo_csv = fopen(NOMBRE_ARCHIVO_SALIDA, "a");
    if (archivo_csv == NULL) {
        perror("fopen en coordinador");
        exit(EXIT_FAILURE);
    }

    // Bucle principal: procesa exactamente la cantidad de registros esperada
    for (long i = 0; i < datos->total_registros_a_generar; ++i) {
        // 1. Espera a que un generador avise que el buffer está lleno
        operar_semaforo(id_semaforos, SEMAFORO_BUFFER_LLENO, -1);

        // 2. Lee el registro del buffer y lo escribe en el archivo
        const struct Registro* registro_leido = &datos->buffer_registro;
        fprintf(archivo_csv, "%ld,%s,%d,%.2f\n", registro_leido->id, registro_leido->nombre_producto, registro_leido->cantidad, registro_leido->precio);

        // 3. Avisa a los generadores que el buffer ya está vacío
        operar_semaforo(id_semaforos, SEMAFORO_BUFFER_VACIO, 1);
    }

    // Tareas finales del coordinador
    datos->coordinador_finalizo = true;
    fclose(archivo_csv);
    printf("[Coordinador] Finalizado. Total de registros: %ld\n", datos->total_registros_a_generar);
    shmdt(datos); // Desconecta la memoria compartida de este proceso
    exit(EXIT_SUCCESS);
}

// Lógica de los Procesos Generadores (productores)
void ejecutar_proceso_generador(int id_generador) {
    // Restaura el comportamiento por defecto
    signal(SIGINT, SIG_DFL);

    // Conecta este proceso a la memoria compartida
    struct DatosCompartidos* datos = (struct DatosCompartidos*)shmat(id_memoria_compartida, NULL, 0);
    if (datos == (void*)-1) {
        perror("shmat en generador");
        exit(EXIT_FAILURE);
    }

    const char* productos_ejemplo[] = {"Laptop", "Mouse", "Teclado", "Monitor", "Webcam"};
    
    while (true) {
        long id_inicio_bloque = -1;
        long id_fin_bloque = -1;

        // --- Inicio de Region Crítica para obtener IDs ---
        operar_semaforo(id_semaforos, SEMAFORO_MUTEX_IDS, -1);

        // Si ya no hay más IDs por asignar, termina el bucle
        if (datos->proximo_id >= datos->total_registros_a_generar) {
            operar_semaforo(id_semaforos, SEMAFORO_MUTEX_IDS, 1); 
            break; 
        }

        // Obtiene un nuevo bloque de IDs para este generador
        id_inicio_bloque = datos->proximo_id;
        id_fin_bloque = MIN(id_inicio_bloque + TAMANIO_BLOQUE_IDS, datos->total_registros_a_generar);
        datos->proximo_id = id_fin_bloque;
        
        operar_semaforo(id_semaforos, SEMAFORO_MUTEX_IDS, 1);
        // --- Fin de Region Crítica ---

        // Genera y envía cada registro del bloque asignado
        for (long id_actual = id_inicio_bloque; id_actual < id_fin_bloque; ++id_actual) {
            // Crea un registro con datos aleatorios
            struct Registro nuevo_registro;
            nuevo_registro.id = id_actual;
            strncpy(nuevo_registro.nombre_producto, productos_ejemplo[rand() % 5], 49);
            nuevo_registro.nombre_producto[49] = '\0'; // Asegura la terminación del string
            nuevo_registro.cantidad = (rand() % 100) + 1;
            nuevo_registro.precio = (double)(rand() % 200000) / 100.0;
            
            // 1. Espera a que el buffer de memoria compartida esté vacío
            operar_semaforo(id_semaforos, SEMAFORO_BUFFER_VACIO, -1);
            // 2. Copia el registro al buffer
            memcpy(&datos->buffer_registro, &nuevo_registro, sizeof(struct Registro));
            // 3. Avisa al coordinador que el buffer ya está lleno
            operar_semaforo(id_semaforos, SEMAFORO_BUFFER_LLENO, 1);
        }
    }
    
    printf("[Generador %d] Finalizado.\n", id_generador);
    shmdt(datos); // Desconecta la memoria compartida de este proceso
    exit(EXIT_SUCCESS);
}


//--- PROCESO PRINCIPAL (PADRE) ---//

int main(int argc, char* argv[]) {
    // 1. Validar argumentos de entrada
    if (argc != 3) {
        mostrar_ayuda(argv[0]);
        return 1;
    }

    int cantidad_generadores = atoi(argv[1]);
    long total_registros = atol(argv[2]);
    if (cantidad_generadores <= 0 || total_registros <= 0) {
        fprintf(stderr, "Error: Los argumentos deben ser números positivos.\n");
        return 1;
    }
    
    // Inicializa la semilla para números aleatorios
    srand(time(NULL) ^ getpid());

    // 2. Crear recursos IPC (Memoria Compartida y Semáforos)
    id_memoria_compartida = shmget(KEY_MEMORIA_COMPARTIDA, sizeof(struct DatosCompartidos), 0666 | IPC_CREAT | IPC_EXCL);
    if (id_memoria_compartida == -1) return 1;

    struct DatosCompartidos* datos_compartidos = (struct DatosCompartidos*)shmat(id_memoria_compartida, NULL, 0);
    if (datos_compartidos == (void*)-1) return 1;

    // Inicializar los datos en la memoria compartida
    datos_compartidos->proximo_id = 0;
    datos_compartidos->total_registros_a_generar = total_registros;
    datos_compartidos->coordinador_finalizo = false;

    // Crear el conjunto de 3 semáforos
    id_semaforos = semget(KEY_SEMAFOROS, 3, 0666 | IPC_CREAT | IPC_EXCL);
    if (id_semaforos == -1) return 1;

    // Inicializar cada semáforo con su valor correspondiente
    semctl(id_semaforos, SEMAFORO_MUTEX_IDS, SETVAL, 1);      // Disponible
    semctl(id_semaforos, SEMAFORO_BUFFER_LLENO, SETVAL, 0);   // Vacío al inicio
    semctl(id_semaforos, SEMAFORO_BUFFER_VACIO, SETVAL, 1);   // Libre al inicio
    
    // 3. Preparar el archivo de salida CSV
    FILE* archivo_csv = fopen(NOMBRE_ARCHIVO_SALIDA, "w");
    if (archivo_csv == NULL) return 1;
    fprintf(archivo_csv, "ID,NOMBRE_PRODUCTO,CANTIDAD,PRECIO\n");
    fclose(archivo_csv);
    
    // 4. Preparar la gestión de procesos hijos
    int total_hijos = cantidad_generadores + 1; // +1 por el coordinador
    pids_hijos = (pid_t*)malloc(total_hijos * sizeof(pid_t));
    if (pids_hijos == NULL) return 1; 

    // Establece el manejador de señales para el padre
    signal(SIGINT, manejador_senial_interrupcion);
    
    // 5. Crear los procesos hijos (Coordinador y Generadores)
    pid_t pid_nuevo_proceso;
    pid_nuevo_proceso = fork();
    if (pid_nuevo_proceso == 0)      { ejecutar_proceso_coordinador(); } 
    else if (pid_nuevo_proceso > 0)  { pids_hijos[cantidad_hijos++] = pid_nuevo_proceso; } 
    else                             { return 1; }

    for (int i = 0; i < cantidad_generadores; ++i) {
        pid_nuevo_proceso = fork();
        if (pid_nuevo_proceso == 0)      { ejecutar_proceso_generador(i + 1); } 
        else if (pid_nuevo_proceso > 0)  { pids_hijos[cantidad_hijos++] = pid_nuevo_proceso; } 
        else                             { return 1; }
    }
    
    // El padre ya no necesita acceso directo a la memoria compartida
    shmdt(datos_compartidos);

    // 6. Esperar a que todos los procesos hijos terminen
    printf("[PADRE] Esperando a que los %d procesos hijos finalicen...\n", cantidad_hijos);
    for (int i = 0; i < cantidad_hijos; ++i) {
        wait(NULL);
    }
    printf("[PADRE] Todos los procesos hijos han finalizado.\n");
    
    // 7. Liberar todos los recursos IPC
    liberar_recursos_ipc();
    
    return 0;
}
