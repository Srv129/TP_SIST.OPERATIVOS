#!/bin/bash

# Nombre del script de verificación AWK
AWK_SCRIPT="./verificar_ids.awk"
# Nombre del programa coordinador
COORDINADOR="./coordinador"
# Nombre del archivo de salida
CSV_FILE="datos_prueba.csv"

# --- Función para ejecutar una prueba ---
run_test() {
    local GENERATORS=$1
    local TOTAL_RECORDS=$2

    echo "======================================================"
    echo "CORRIENDO PRUEBA: $GENERATORS generadores | $TOTAL_RECORDS registros"
    echo "======================================================"

    # 1. Ejecutar el coordinador
    if $COORDINADOR $GENERATORS $TOTAL_RECORDS; then
        echo "Ejecución de $COORDINADOR finalizada con éxito."
    else
        echo "ERROR: La ejecución de $COORDINADOR falló. Saltando verificación."
        return 1
    fi

    # 2. Ejecutar la verificación AWK
    echo ""
    echo "--- INICIANDO VERIFICACIÓN AWK ---"

    awk -f $AWK_SCRIPT $CSV_FILE

    if [ $? -eq 0 ]; then
        echo "------------------------------------------------------"
        echo "PRUEBA [$GENERATORS G | $TOTAL_RECORDS R]: ¡EXITOSA!"
        echo "------------------------------------------------------"
    else
        echo "------------------------------------------------------"
        echo "PRUEBA [$GENERATORS G | $TOTAL_RECORDS R]: ¡FALLIDA! Verificación AWK falló."
        echo "------------------------------------------------------"
    fi
    echo ""
}

# --- Ejecutar Múltiples Pruebas ---
# Prueba Ligera
run_test 3 100

# Prueba Estándar de Carga Media
run_test 5 500

# Prueba de Estrés
run_test 8 2000

echo "TODAS LAS PRUEBAS AUTOMATIZADAS HAN FINALIZADO."
