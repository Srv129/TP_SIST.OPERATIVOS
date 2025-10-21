#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <errno.h>

#define TAMANIO_BUFFER 1024

// Función para leer una línea completa del socket.
int leer_linea_del_socket(int socket, char* buffer, size_t tamanio) {
    memset(buffer, 0, tamanio);
    size_t total_leido = 0;
    char c;
    while (total_leido < tamanio - 1) {
        int bytes_leidos = read(socket, &c, 1);
        if (bytes_leidos <= 0) return bytes_leidos; // Error o conexión cerrada
        buffer[total_leido++] = c;
        if (c == '\n') break; // Fin de la línea, mensaje completo recibido
    }
    return total_leido;
}

int main(int argc, char *argv[]) {
    // Valida que se hayan pasado la IP y el puerto como argumentos.
    if (argc != 3) {
        fprintf(stderr, "Uso: %s <IP_servidor> <puerto>\n", argv[0]);
        return 1;
    }

    char *ip_servidor = argv[1];
    int puerto = atoi(argv[2]);

    int socket_cliente;
    struct sockaddr_in direccion_servidor;
    char buffer[TAMANIO_BUFFER];

    // 1. Crear el socket del cliente.
    socket_cliente = socket(AF_INET, SOCK_STREAM, 0);
    if (socket_cliente < 0) {
        perror("Error al crear el socket");
        return 1;
    }

    // 2. Preparar la dirección del servidor para la conexión.
    memset(&direccion_servidor, 0, sizeof(direccion_servidor));
    direccion_servidor.sin_family = AF_INET;
    direccion_servidor.sin_port = htons(puerto);
    if (inet_pton(AF_INET, ip_servidor, &direccion_servidor.sin_addr) <= 0) {
        perror("Dirección IP inválida");
        close(socket_cliente);
        return 1;
    }

    // 3. Conectar al servidor.
    if (connect(socket_cliente, (struct sockaddr *)&direccion_servidor, sizeof(direccion_servidor)) < 0) {
        if (errno == ECONNREFUSED) {
            fprintf(stderr, "Error: Conexión rechazada. Verifique que el servidor esté en línea en la IP y puerto correctos.\n");
        } else if (errno == ETIMEDOUT) {
            fprintf(stderr, "Error: El servidor está lleno o no responde. Intente más tarde.\n");
        } else {
            perror("Error en connect");
        }
        close(socket_cliente);
        return 1;
    }

    // 4. Esperar el mensaje inicial del servidor para saber nuestro estado.
    int bytes_leidos = leer_linea_del_socket(socket_cliente, buffer, sizeof(buffer));
    if (bytes_leidos <= 0) {
        printf("El servidor cerró la conexión inesperadamente.\n");
        close(socket_cliente);
        return 1;
    }

    if (strncmp(buffer, "REJECT", 6) == 0) {
        printf("Servidor lleno. Intente mas tarde.\n");
        close(socket_cliente);
        return 0;
    } else if (strncmp(buffer, "WAIT", 4) == 0) {
        printf("Servidor lleno. En Espera...\n");
        // Nos quedamos esperando a que el servidor nos dé luz verde ("OK_CONNECT").
        bytes_leidos = leer_linea_del_socket(socket_cliente, buffer, sizeof(buffer));
        if (bytes_leidos <= 0 || strncmp(buffer, "OK_CONNECT", 10) != 0) {
            printf("No se pudo establecer la conexión final.\n");
            close(socket_cliente);
            return 1;
        }
    }

    printf("Conectado al servidor. Escribe 'EXIT' para salir.\n");
    
    // 5. Bucle interactivo para enviar y recibir mensajes.
    while (1) {
        printf("> ");
        fflush(stdout);

        // Lee el comando del usuario desde el teclado.
        if (fgets(buffer, sizeof(buffer), stdin) == NULL) {
            break; // Si el usuario presiona Ctrl+D, termina.
        }

        // Envía el comando al servidor (fgets incluye el '\n').
        if (write(socket_cliente, buffer, strlen(buffer)) < 0) {
            perror("Error al enviar datos");
            break;
        }

        // Si el comando es EXIT, salimos del bucle.
        if (strncmp(buffer, "EXIT", 4) == 0) {
            break;
        }

        // Lee la respuesta completa del servidor.
        bytes_leidos = leer_linea_del_socket(socket_cliente, buffer, sizeof(buffer));
        
        if (bytes_leidos <= 0) {
            printf("\nEl servidor cerró la conexión.\n");
            break;
        }
        
        // Muestra la respuesta en la pantalla.
        printf("Servidor: %s", buffer);
    }

    // 6. Cierra la conexión.
    close(socket_cliente);
    printf("Desconectado.\n");
    return 0;
}