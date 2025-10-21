#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdbool.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <signal.h>
#include <sys/wait.h>
#include <sys/file.h>
#include <errno.h>

#define TAMANIO_BUFFER 1024
#define MAX_CLIENTES_TOTAL 256 // Límite máximo de conexiones que el servidor puede manejar
const char* NOMBRE_ARCHIVO_BD = "output.csv";
const char* NOMBRE_ARCHIVO_TEMP = "output.tmp";

// Estructura para gestionar los sockets de los clientes en espera.
int sockets_en_espera[MAX_CLIENTES_TOTAL];
int clientes_en_espera_app = 0;
volatile sig_atomic_t clientes_activos = 0;

// Prototipos de funciones.
void manejar_cliente(int socket_cliente);
void buscar_registro_por_id(long id_buscado, char* resultado, size_t resultado_len);
void actualizar_registro_por_id(long id_buscado, int indice_campo, const char* nuevo_valor, char* respuesta, size_t respuesta_len);
void agregar_registro(const char* datos_registro, char* respuesta, size_t respuesta_len);
void eliminar_registro_por_id(long id_buscado, char* respuesta, size_t respuesta_len);
long obtener_max_id();

// Manejador que se activa cuando un cliente activo se desconecta.
void manejador_sigchld(int signum) {
    // Ignora el parámetro para evitar advertencias.
    (void)signum;
    // Recolecta todos los procesos hijos terminados.
    while (waitpid(-1, NULL, WNOHANG) > 0) {
        clientes_activos--;
        printf("Un cliente activo se ha desconectado. Clientes activos: %d\n", clientes_activos);
        
        // Si hay clientes en la sala de espera, damos paso al primero.
        if (clientes_en_espera_app > 0) {
            int socket_a_promover = sockets_en_espera[0];
            const char* msg = "OK_CONNECT\n";
            write(socket_a_promover, msg, strlen(msg)); 
            
            // Reorganizamos la cola de espera.
            for (int i = 0; i < clientes_en_espera_app - 1; i++) {
                sockets_en_espera[i] = sockets_en_espera[i+1];
            }
            clientes_en_espera_app--;

            // Creamos el proceso hijo para el cliente que estaba esperando.
            if (fork() == 0) {
                manejar_cliente(socket_a_promover);
                exit(0);
            }
            clientes_activos++;
            printf("Cliente en espera promovido a activo. Clientes activos: %d, en espera: %d\n", clientes_activos, clientes_en_espera_app);
        }
    }
}

int main(int argc, char *argv[]) {
    if (argc != 4) {
        fprintf(stderr, "Uso: %s <puerto> <clientes_concurrentes> <clientes_en_espera>\n", argv[0]);
        return 1;
    }
    // Convierte los argumentos a enteros.
    int puerto = atoi(argv[1]);
    int max_clientes_concurrentes = atoi(argv[2]);
    int max_clientes_espera = atoi(argv[3]);

    // Crea el socket del servidor.
    int socket_servidor = socket(AF_INET, SOCK_STREAM, 0);
    if (socket_servidor < 0) {
        perror("Error al crear el socket");
        return 1;
    }

    // Configura el socket para reutilizar la dirección y puerto.
    int opt = 1;
    setsockopt(socket_servidor, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    
    // Prepara la dirección del servidor
    struct sockaddr_in direccion_servidor;
    memset(&direccion_servidor, 0, sizeof(direccion_servidor));
    direccion_servidor.sin_family = AF_INET; // IPv4
    direccion_servidor.sin_addr.s_addr = INADDR_ANY; // Acepta conexiones en todas las interfaces
    direccion_servidor.sin_port = htons(puerto); // Convierte el puerto a formato de red

    // Enlaza el socket a la dirección y puerto especificados
    if (bind(socket_servidor, (struct sockaddr *)&direccion_servidor, sizeof(direccion_servidor)) < 0) {
        perror("bind"); return 1;
    }
    if (listen(socket_servidor, max_clientes_espera) < 0) {
        perror("listen"); return 1;
    }
    printf("Servidor listo. Límite: %d activos, %d en espera. CLOSE para cerrar el servidor.\n", max_clientes_concurrentes, max_clientes_espera);

    // Configura el manejador de señales para SIGCHLD
    signal(SIGCHLD, manejador_sigchld);
    // Convierte a este proceso en el líder de un nuevo grupo de procesos.
    setpgid(0, 0);

    // Bucle principal para aceptar conexiones y comandos.
    while (true) {
        fd_set read_fds;
        FD_ZERO(&read_fds);
        FD_SET(socket_servidor, &read_fds);
        FD_SET(STDIN_FILENO, &read_fds); // STDIN_FILENO es 0 (entrada estándar)

        // select() se bloquea hasta que haya actividad en el socket o en la terminal.
        if (select(socket_servidor + 1, &read_fds, NULL, NULL, NULL) < 0) {
            if (errno == EINTR) continue; // Si es interrumpido por una señal, reintenta.
            perror("select");
            break;
        }
        // Verifica si hay actividad en la entrada estándar (terminal del servidor).
        if (FD_ISSET(STDIN_FILENO, &read_fds)) {
            char command_buffer[TAMANIO_BUFFER];
            if (fgets(command_buffer, sizeof(command_buffer), stdin) != NULL) {
                command_buffer[strcspn(command_buffer, "\r\n")] = 0;
                if (strcmp(command_buffer, "CLOSE") == 0) {
                    printf("Comando CLOSE recibido. Cerrando el servidor y todos los clientes...\n");
                    // Envía la señal de terminación a todo el grupo de procesos (padre y todos los hijos).
                    kill(0, SIGTERM);
                    break; // Sale del bucle para apagar.
                }
            }
        }

        // Verifica si hay una nueva conexión en el socket del servidor.
        if (FD_ISSET(socket_servidor, &read_fds)) {
            int socket_cliente = accept(socket_servidor, NULL, NULL);
            if (socket_cliente < 0) continue;

            // Lógica para decidir si el cliente es atendido, puesto en espera o rechazado.
            if (clientes_activos < max_clientes_concurrentes) {
                // Hay espacio, se atiende inmediatamente.
                const char* msg = "OK_CONNECT\n";
                write(socket_cliente, msg, strlen(msg));
                
                if (fork() == 0) 
                {
                    // Proceso hijo maneja al cliente.
                    manejar_cliente(socket_cliente);
                    exit(0);
                }
                // Proceso padre continúa aceptando conexiones.
                clientes_activos++;
                printf("Cliente aceptado como activo. Activos: %d, En espera: %d\n", clientes_activos, clientes_en_espera_app);
            } else if (clientes_en_espera_app < max_clientes_espera) {
                // No hay espacio activo, pero sí en la sala de espera.
                const char* msg = "WAIT\n";
                write(socket_cliente, msg, strlen(msg));
                sockets_en_espera[clientes_en_espera_app++] = socket_cliente;
                printf("Cliente puesto en espera. Activos: %d, En espera: %d\n", clientes_activos, clientes_en_espera_app);
            } else {
                // El servidor está completamente lleno.
                const char* msg = "REJECT\n";
                write(socket_cliente, msg, strlen(msg));
                close(socket_cliente);
                printf("Servidor lleno. Cliente rechazado.\n");
            }
        }
    }
    
    close(socket_servidor);
    printf("Servidor cerrado.\n");
    return 0;
}



// Lógica para atender a un cliente.
void manejar_cliente(int socket_cliente) {

    char buffer[TAMANIO_BUFFER];
    char respuesta[TAMANIO_BUFFER];
    bool en_transaccion = false;

    // Abre el archivo de la base de datos.
    int fd_bd = open(NOMBRE_ARCHIVO_BD, O_RDWR);
    if (fd_bd < 0) {
        snprintf(respuesta, sizeof(respuesta), "ERROR|No se pudo abrir la base de datos\n");
        write(socket_cliente, respuesta, strlen(respuesta));
        close(socket_cliente);
        return;
    }
    printf("[PID: %d] Cliente conectado.\n", getpid());
    
    // Bucle principal para manejar comandos del cliente.
    while (true) {
        memset(buffer, 0, sizeof(buffer));
        int bytes_leidos = read(socket_cliente, buffer, sizeof(buffer) - 1);
        if (bytes_leidos <= 0) {
            printf("[PID: %d] Cliente desconectado.\n", getpid());
            break;
        }
        // Elimina saltos de línea para facilitar el procesamiento.
        buffer[strcspn(buffer, "\r\n")] = 0;

        if (strcmp(buffer, "BEGIN TRANSACTION") == 0) 
        {
            // Intenta obtener un bloqueo EXCLUSIVO sin esperar.
            if (flock(fd_bd, LOCK_EX | LOCK_NB) == 0) {
                en_transaccion = true;
                snprintf(respuesta, sizeof(respuesta), "Transacción iniciada.");
            } else {
                snprintf(respuesta, sizeof(respuesta), "ERROR|Base de datos bloqueada por otra transacción.");
            }

        } 

        else if (strcmp(buffer, "COMMIT TRANSACTION") == 0) 
        {
            if (en_transaccion) {
                flock(fd_bd, LOCK_UN); // Libera el bloqueo.
                en_transaccion = false;
                snprintf(respuesta, sizeof(respuesta), "Transacción confirmada.");
            } else {
                snprintf(respuesta, sizeof(respuesta), "ERROR|No hay transacción activa.");
            }

        } 

        else if (strncmp(buffer, "GET ", 4) == 0) 
        {
            long id;
            if (sscanf(buffer, "GET %ld", &id) == 1) {
            
                if (en_transaccion) {
                    // Si este cliente ya está en una transacción, tiene el bloqueo exclusivo y puede leer.
                    buscar_registro_por_id(id, respuesta, sizeof(respuesta));
                } else {
                    // Intenta obtener un bloqueo de lectura sin esperar.
                    if (flock(fd_bd, LOCK_SH | LOCK_NB) == 0) {
                        // Bloqueo de lectura obtenido con éxito.
                        buscar_registro_por_id(id, respuesta, sizeof(respuesta));
                        flock(fd_bd, LOCK_UN); // Liberar inmediatamente después de leer.
                    } else {
                        // No se pudo obtener el bloqueo de lectura, significa que hay una transacción activa.
                        snprintf(respuesta, sizeof(respuesta), "ERROR|Base de datos bloqueada por una transacción.");
                    }
                }
            } else {
                snprintf(respuesta, sizeof(respuesta), "ERROR|Uso: GET <ID>");
            }

        } 
        
        else if (strncmp(buffer, "UPDATE ", 7) == 0) 
        {
            long id; int campo; char valor[TAMANIO_BUFFER];

            if (sscanf(buffer, "UPDATE %ld %d %[^\n]", &id, &campo, valor) == 3) {
                if (en_transaccion) {
                    actualizar_registro_por_id(id, campo, valor, respuesta, sizeof(respuesta));
                } else {
                    snprintf(respuesta, sizeof(respuesta), "ERROR|Operación requiere una transacción.");
                }
            } else {
                snprintf(respuesta, sizeof(respuesta), "ERROR|Uso: UPDATE <ID> <Nro_Campo> <Valor>");
            }
        
        } 

        else if (strncmp(buffer, "ADD ", 4) == 0) 
        {
            if (en_transaccion) {
                agregar_registro(buffer + 4, respuesta, sizeof(respuesta));
            } else {
                snprintf(respuesta, sizeof(respuesta), "ERROR|Operación requiere una transacción.");
            }

        } 
        else if (strncmp(buffer, "DELETE ", 7) == 0) 
        {
            long id;
            if (sscanf(buffer, "DELETE %ld", &id) == 1) {
                if (en_transaccion) {
                    eliminar_registro_por_id(id, respuesta, sizeof(respuesta));
                } else {
                    snprintf(respuesta, sizeof(respuesta), "ERROR|Operación requiere una transacción.");
                }
            } else {
                snprintf(respuesta, sizeof(respuesta), "ERROR|Uso: DELETE <ID>");
            }

        } 
        else if (strcmp(buffer, "EXIT") == 0)
        {
            snprintf(respuesta, sizeof(respuesta), "Saliendo...\n");
            write(socket_cliente, respuesta, strlen(respuesta));
            break;
        } 

        else if (strcmp(buffer, "HELP") == 0) 
        {
            snprintf(respuesta, sizeof(respuesta), "Comandos: GET, UPDATE, ADD, DELETE, BEGIN TRANSACTION, COMMIT TRANSACTION, EXIT");
        }

        else 
        {
            snprintf(respuesta, sizeof(respuesta), "ERROR|Comando desconocido. Escriba HELP para ver la lista de comandos.");
        }
        // Envía la respuesta al cliente.
        strncat(respuesta, "\n", sizeof(respuesta) - strlen(respuesta) - 1);
        write(socket_cliente, respuesta, strlen(respuesta));
    }

    if (en_transaccion) flock(fd_bd, LOCK_UN);
    close(fd_bd);
    close(socket_cliente);
}

// Funciones de búsqueda y actualización.
void buscar_registro_por_id(long id_buscado, char* resultado, size_t resultado_len) {
    FILE* archivo = fopen(NOMBRE_ARCHIVO_BD, "r");
    if (!archivo) {snprintf(resultado, resultado_len, "ERROR|No se pudo abrir la BD"); return;}
    char linea[TAMANIO_BUFFER]; bool encontrado = false;
    fgets(linea, sizeof(linea), archivo);
    while (fgets(linea, sizeof(linea), archivo)) {
        long id_actual;
        if (sscanf(linea, "%ld,", &id_actual) == 1 && id_actual == id_buscado) {
            linea[strcspn(linea, "\r\n")] = 0;
            snprintf(resultado, resultado_len, "%s", linea);
            encontrado = true;
            break;
        }
    }
    if (!encontrado) snprintf(resultado, resultado_len, "ERROR|ID %ld no encontrado.", id_buscado);
    fclose(archivo);
}
void actualizar_registro_por_id(long id_buscado, int indice_campo, const char* nuevo_valor, char* respuesta, size_t respuesta_len) {
    FILE* original = fopen(NOMBRE_ARCHIVO_BD, "r");
    FILE* temporal = fopen(NOMBRE_ARCHIVO_TEMP, "w");
    if (!original || !temporal) {snprintf(respuesta, respuesta_len, "ERROR|No se pudieron abrir archivos"); if(original) fclose(original); if(temporal) fclose(temporal); return;}
    char linea[TAMANIO_BUFFER]; 
    bool encontrado = false;
    bool modificacion_valida = true;

    if (fgets(linea, sizeof(linea), original)) fputs(linea, temporal);
    while (fgets(linea, sizeof(linea), original)) {
        long id_actual;
        if (sscanf(linea, "%ld,", &id_actual) == 1 && id_actual == id_buscado) {
            encontrado = true;
            char partes[4][256];
            if (sscanf(linea, "%[^,],%[^,],%[^,],%[^\n]", partes[0], partes[1], partes[2], partes[3]) == 4) {
                // Realiza la validación antes de modificar.
                if (indice_campo == 2) { // CANTIDAD
                    if (strchr(nuevo_valor, '.') != NULL || strchr(nuevo_valor, ',') != NULL) {
                        snprintf(respuesta, respuesta_len, "ERROR|La cantidad no puede ser un número decimal.");
                        modificacion_valida = false;
                    } else if (atoi(nuevo_valor) < 0) {
                        snprintf(respuesta, respuesta_len, "ERROR|La cantidad no puede ser negativa.");
                        modificacion_valida = false;
                    }
                } else if (indice_campo == 3) { // PRECIO
                    if (strchr(nuevo_valor, ',') != NULL) {
                        snprintf(respuesta, respuesta_len, "ERROR|El precio debe usar un punto (.) como separador decimal, no una coma (,).");
                        modificacion_valida = false;
                    } else if (atof(nuevo_valor) < 0.0) {
                        snprintf(respuesta, respuesta_len, "ERROR|El precio no puede ser negativo.");
                        modificacion_valida = false;
                    }
                }

                if (modificacion_valida) {
                    if (indice_campo > 0 && indice_campo < 4) {
                        snprintf(partes[indice_campo], 256, "%s", nuevo_valor);
                    }
                    fprintf(temporal, "%s,%s,%s,%s\n", partes[0], partes[1], partes[2], partes[3]);
                } else {
                    fputs(linea, temporal); // Si no es válido, escribe la línea original.
                }
            }
        } else {
            fputs(linea, temporal);
        }
    }
    fclose(original); fclose(temporal);
    
    if (encontrado && modificacion_valida) {
        remove(NOMBRE_ARCHIVO_BD); rename(NOMBRE_ARCHIVO_TEMP, NOMBRE_ARCHIVO_BD);
        snprintf(respuesta, respuesta_len, "Registro %ld actualizado.", id_buscado);
    } else if (encontrado && !modificacion_valida) {
        // La respuesta ya tiene el mensaje de error de validación.
        remove(NOMBRE_ARCHIVO_TEMP);
    } else { // No encontrado
        remove(NOMBRE_ARCHIVO_TEMP);
        snprintf(respuesta, respuesta_len, "ERROR|ID %ld no encontrado.", id_buscado);
    }
}

// Busca el ID más alto en el archivo para determinar el siguiente.
long obtener_max_id() {
    FILE* archivo = fopen(NOMBRE_ARCHIVO_BD, "r");
    if (!archivo) return -1;
    char linea[TAMANIO_BUFFER];
    long max_id = -1;
    long id_actual;
    // Salta la cabecera.
    if (fgets(linea, sizeof(linea), archivo) == NULL) {
        fclose(archivo);
        return 0;
    }
    while (fgets(linea, sizeof(linea), archivo)) {
        if (sscanf(linea, "%ld,", &id_actual) == 1) {
            if (id_actual > max_id) {
                max_id = id_actual;
            }
        }
    }
    fclose(archivo);
    return max_id;
}

// Agrega un nuevo registro al final del archivo.
void agregar_registro(const char* datos_registro, char* respuesta, size_t respuesta_len) {
    char nombre_producto[256];
    char cantidad_str[50];
    char precio_str[50];
    double precio;
    int cantidad;

    // Parsea los datos del registro: "NOMBRE,CANTIDAD,PRECIO"
    if (sscanf(datos_registro, "%255[^,],%49[^,],%49s", nombre_producto, cantidad_str, precio_str) != 3) {
        snprintf(respuesta, respuesta_len, "ERROR|Formato incorrecto. Uso: ADD <NOMBRE_PRODUCTO>,<CANTIDAD>,<PRECIO>");
        return;
    }

    // Valida que la cantidad no sea decimal y que el precio no use comas.
    if (strchr(cantidad_str, '.') != NULL || strchr(cantidad_str, ',') != NULL) {
        snprintf(respuesta, respuesta_len, "ERROR|La cantidad no puede ser un número decimal.");
        return;
    }
    if (strchr(precio_str, ',') != NULL) {
        snprintf(respuesta, respuesta_len, "ERROR|El precio debe usar un punto (.) como separador decimal, no una coma (,).");
        return;
    }
    cantidad = atoi(cantidad_str);
    precio = atof(precio_str);

    // Valida que los datos no sean negativos.
    if (cantidad < 0 || precio < 0.0) {
        snprintf(respuesta, respuesta_len, "ERROR|La cantidad y el precio no pueden ser negativos.");
        return;
    }

    long nuevo_id = obtener_max_id() + 1;
    // Abre el archivo en modo "append" para añadir al final.
    FILE* archivo = fopen(NOMBRE_ARCHIVO_BD, "a");
    if (!archivo) {
        snprintf(respuesta, respuesta_len, "ERROR|No se pudo abrir la base de datos para escribir.");
        return;
    }
    fprintf(archivo, "%ld,%s,%d,%.2f\n", nuevo_id, nombre_producto, cantidad, precio);
    fclose(archivo);
    snprintf(respuesta, respuesta_len, "Registro agregado con ID %ld.", nuevo_id);
}

// Elimina un registro usando la estrategia de archivo temporal.
void eliminar_registro_por_id(long id_buscado, char* respuesta, size_t respuesta_len) {
    FILE* original = fopen(NOMBRE_ARCHIVO_BD, "r");
    FILE* temporal = fopen(NOMBRE_ARCHIVO_TEMP, "w");
    if (!original || !temporal) {
        snprintf(respuesta, respuesta_len, "ERROR|No se pudieron abrir archivos para eliminar.");
        if (original) fclose(original);
        if (temporal) fclose(temporal);
        return;
    }
    char linea[TAMANIO_BUFFER];
    long id_actual;
    bool encontrado = false;
    // Copia la cabecera.
    if (fgets(linea, sizeof(linea), original)) {
        fputs(linea, temporal);
    }
    // Procesa cada línea del archivo original.
    while (fgets(linea, sizeof(linea), original)) {
        if (sscanf(linea, "%ld,", &id_actual) == 1 && id_actual == id_buscado) {
            encontrado = true;
            // No copia la línea al archivo temporal, eliminándola efectivamente.
        } else {
            fputs(linea, temporal);
        }
    }
    fclose(original); fclose(temporal);
    if (encontrado) {
        remove(NOMBRE_ARCHIVO_BD);
        rename(NOMBRE_ARCHIVO_TEMP, NOMBRE_ARCHIVO_BD);
        snprintf(respuesta, respuesta_len, "Registro %ld eliminado.", id_buscado);
    } else {
        remove(NOMBRE_ARCHIVO_TEMP);
        snprintf(respuesta, respuesta_len, "ERROR|ID %ld no encontrado para eliminar.", id_buscado);
    }
}