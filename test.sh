#!/bin/bash

# Parámetros de prueba
GENERADORES=10
REGISTROS=50000
EJECUTABLE="generador"

echo "========================================="
echo "== SCRIPT DE PRUEBA Y VALIDACIÓN =="
echo "========================================="

echo -e "\n[1/4] Compilando el proyecto..."
make clean > /dev/null
make
if [ $? -ne 0 ]; then
    echo "Error: La compilación falló."
    exit 1
fi
echo "Compilación exitosa."

echo -e "\n[2/4] Ejecutando con $GENERADORES procesos y $REGISTROS registros..."
time ./$EJECUTABLE $GENERADORES $REGISTROS
if [ $? -ne 0 ]; then
    echo "Error: La ejecución del programa falló."
    exit 1
fi
echo "Ejecución finalizada."

echo -e "\n[3/4] Validando el archivo de salida..."
awk -f validate.awk output.csv

echo -e "\n========================================="
echo "== Prueba completada =="
echo "========================================="
