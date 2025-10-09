## TP_SIST.OPERATIVOS

# Ejercicio 1: Generador de Datos de Prueba con Procesos y Memoria Compartida

Enunciado:
Se debe desarrollar un sistema de generación de datos mediante procesos en paralelo.
Un proceso coordinador administra la asignación de identificadores y la escritura de registros en un
archivo CSV, mientras que N procesos generadores producen datos de prueba.
Requisitos:
• El programa debe permitir especificar por parámetro cantidad de procesos generadores y
cantidad total de registros a generar.
• Cada generador solicita al proceso coordinador los próximos 10 IDs válidos.
• Con cada ID, genera un registro con datos aleatorios (por ejemplo: valores tomados de listas
predefinidas o números generados en un rango).
• Cada registro es enviado de a uno por vez al coordinador mediante memoria compartida (SHM).
• El coordinador guarda el registro recibido en el archivo CSV.
• El archivo CSV debe:
o Tener en la primera fila el nombre de las columnas.
o Incluir obligatoriamente el ID como primer campo.
o El resto de los campos pueden ser definidos libremente.
• No es necesario que los IDs queden ordenados en el CSV; se registran en el orden en que son
recibidos.
Monitoreo en Linux:
• Uso de ipcs o /dev/shm, ps, htop, vmstat para evidenciar creación de procesos, memoria compartida
y concurrencia.
• Limpieza de recursos IPC al finalizar.
Criterios de corrección:
• Se verificará con un script AWK:
o Que los IDs sean correlativos y no presenten saltos en su numeración.
o Que no existan IDs duplicados.
• Se deben validar correctamente los parámetros y mostrar ayuda en caso que no se informen o sean
incorrectos.
• Ante la finalización prematura de un proceso, el resto de los procesos deben responder de forma
controlada, pudiendo continuar su ejecución o bien finalizar dependiendo qué proceso finalizó.
• No dejar abiertos recursos en el sistema (semáforos, memorias compartidas, archivos temporales,
etc.) una vez que finalicen todos los procesos.
