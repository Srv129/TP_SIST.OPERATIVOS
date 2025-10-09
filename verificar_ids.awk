BEGIN {
    FS=",";                # Establecer la coma como separador de campos (CSV)
    OFS="\t";              # Establecer tabulador como separador de salida
    last_id = 0;           # Inicializar el último ID procesado a 0
    errors = 0;            # Contador de errores encontrados
    print "--- INICIANDO VERIFICACIÓN DE IDs ---";
}

NR > 1 { # Procesar todas las líneas excepto la primera (cabecera)
    current_id = $1;     # El ID está en el primer campo ($1)

    # 1. Verificar si el ID actual es un número válido (Opcional, pero robusto)
    if (current_id ~ /[^0-9]/ || current_id == "") {
        # Si el campo no contiene solo números o está vacío, lo ignoramos para la correlatividad
        next; 
    }

    # 2. Verificar Duplicados
    if (current_id in seen_ids) {
        print "ERROR: ID DUPLICADO detectado:", current_id, "en la línea:", NR;
        errors++;
        next; # Pasar a la siguiente línea
    }
    seen_ids[current_id] = 1; # Marcar el ID como visto

    # 3. Verificar Correlatividad y Saltos
    # Solo verificamos si ya hemos procesado al menos un ID válido
    if (last_id != 0) {
        expected_id = last_id + 1;
        
        # AWK maneja los números como texto, así que forzamos la comparación numérica
        if (current_id + 0 != expected_id + 0) {
            # Se ha encontrado un salto
            print "ERROR: SALTO DE ID. Línea:", NR, "Actual:", current_id, "Esperado:", expected_id;
            errors++;
        }
    }

    last_id = current_id; # Actualizar el último ID para la siguiente iteración
}

END {
    print "--- RESUMEN DE VERIFICACIÓN ---";
    if (errors == 0) {
        print "VERIFICACIÓN EXITOSA: Los IDs son correlativos y no hay duplicados.";
    } else {
        print "VERIFICACIÓN FALLIDA:", errors, "errores encontrados.";
    }
}