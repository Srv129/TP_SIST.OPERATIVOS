#!/usr/bin/awk -f
BEGIN {
    FS=","; max_id = -1; count = 0; duplicates_found = 0;
    print "--- Iniciando validación de output.csv ---";
}
NR > 1 {
    id = $1;
    if (id in ids) {
        printf "Error: ID duplicado -> %d\n", id;
        duplicates_found=1;
    }
    ids[id] = 1;
    if (id > max_id) { max_id = id; }
    count++;
}
END {
    if (!duplicates_found) { print "OK: No se encontraron IDs duplicados."; }
    expected_count = max_id + 1;
    if (count == expected_count) {
        printf "OK: Los IDs son correlativos. Total: %d (ID máx: %d). \n", count, max_id;
    } else {
        printf "Error: Faltan IDs. Total: %d. Se esperaba: %d. \n", count, expected_count;
    }
    print "--- Fin de la validación ---";
}
