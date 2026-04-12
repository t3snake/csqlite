#include "schematab.h"

#include <stdlib.h>

SchemaInfo getSchemaInfo(FILE* db_file) {
    SchemaInfo result;
    ParseVarintResult varint;

    // Read size of the record / cell
    varint = parseVarint(db_file);
    u64 record_size = varint.value;

    // Read rowid (not used)
    parseVarint(db_file);

    // Read record header size
    varint = parseVarint(db_file);
    u64 record_hdr_size = varint.value - 1;

    // printf("debug_info: malloc line 223\n");
    u64* col_sizes = malloc(100 * sizeof(u64)); // this is fine since for schema table this wont be that long
    u64 col_len = 0;

    while (record_hdr_size > 0) {
        varint = parseVarint(db_file);

        *(col_sizes + col_len) = getRecordSerialTypeSize(varint.value);
        col_len += 1;

        record_hdr_size -= varint.byte_span;
    }

    u64 tbl_name_size = *(col_sizes + 2);
    u64 rootpage_size = *(col_sizes + 3);
    fprintf(stderr, "debug_info: malloc line 238\n");
    result.table_name = malloc((tbl_name_size + 1) * sizeof(char)); // additional for null byte
    for (int i = 0; i < col_len; i++) {
        u64 col_size = *(col_sizes + i);

        if (i == 2){
            fread(result.table_name, 1, tbl_name_size, db_file);
            result.table_name[tbl_name_size] = '\0';
        } else if (i == 3) {
            u8* rootpage_bytes = malloc(rootpage_size);
            fread(rootpage_bytes, 1, rootpage_size, db_file);
            fprintf(stderr, "parseSqlInt called.\n");
            result.root_page = parseSqlInt(rootpage_bytes, rootpage_size);
            free(rootpage_bytes);
        } else {
            fseek(db_file, col_size, SEEK_CUR);
        }
    }

    free(col_sizes);
    return result;
}

SqliteSchemaEntries getSchemaTabRowAddr(FILE* db_file) {
    // Skip DB header to reach B-Tree Page Header
    fseek(db_file, 100, SEEK_SET);

    // Go to offset 3 to get cell count
    fseek(db_file, 3, SEEK_CUR);

    u8 buffer[2];
    fread(buffer, 1, 2, db_file);

    u16 cell_count = (buffer[1]) | (buffer[0] << 8);
    fseek(db_file, 3, SEEK_CUR); // Skip 3 bytes to skip header and reach cell ptr array

    // printf("debug_info: malloc line 285\n");
    u16* cell_content_addrs = malloc(cell_count * sizeof(u16));
    for (int i = 0; i < cell_count; i++) {
        // Read next 2 bytes to get address of cell content area
        fread(buffer, 1, 2, db_file);
        *(cell_content_addrs + i) = buffer[1] | (buffer[0] << 8);
    }

    SqliteSchemaEntries entries;
    entries.count = cell_count;
    entries.offsets = cell_content_addrs;
    return entries;
}
