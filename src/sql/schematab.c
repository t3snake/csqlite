#include "schematab.h"

#include <stdlib.h>
#include <string.h>

SchemaInfo getSchemaRowInfo(FILE* db_file) {
    SchemaInfo result;
    result.index_name = NULL;
    result.type = 2; // others by default
    result.table_name = NULL;
    result.sql_create_stm = NULL;

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

    while (record_hdr_size > 0 && record_hdr_size < 10000) {
        varint = parseVarint(db_file);

        *(col_sizes + col_len) = getRecordSerialTypeSize(varint.value);
        col_len += 1;

        record_hdr_size -= varint.byte_span;
    }

    for (int i = 0; i < col_len; i++) {
        u64 col_size = *(col_sizes + i);

        if (i == 0) {
            char* schema_type = malloc(col_size + 1);
            fread(schema_type, 1, col_size, db_file);
            schema_type[col_size] = '\0';

            if (strcmp(schema_type, "index") == 0) {
                result.type = 1;
            } else if (strcmp(schema_type, "table") == 0) {
                result.type = 0;
            }

            free(schema_type);
        } else if (i == 1) {
            u64 name_size = col_size;
            char* name = malloc((name_size + 1) * sizeof(char)); // additional for null byte
            fread(name, 1, name_size, db_file);
            name[name_size] = '\0';

            if (result.type == 0) {
                result.table_name = name;
            } else if (result.type == 1) {
                result.index_name = name;
            }
        } else if (i == 2) {
            if (result.type == 1) { // if index type then this has table name
                char* name = malloc((col_size + 1) * sizeof(char)); // additional for null byte
                fread(name, 1, col_size, db_file);
                name[col_size] = '\0';

                result.table_name = name;
            } else { // if table type, then this is duplicate (tbl_name), skip for these and others (unsupported)
                fseek(db_file, col_size, SEEK_CUR);
            }
        } else if (i == 3) {
            u64 rootpage_size = col_size;
            u8* rootpage_bytes = malloc(rootpage_size);
            fread(rootpage_bytes, 1, rootpage_size, db_file);
            fprintf(stderr, "parseSqlInt called.\n");
            result.root_page = parseSqlInt(rootpage_bytes, rootpage_size);

            free(rootpage_bytes);
        } else if (i == 4) {
            u64 sql_stm_size = col_size;
            result.sql_create_stm = malloc((sql_stm_size + 1) * sizeof(char));
            fread(result.sql_create_stm, 1, sql_stm_size, db_file);
            result.sql_create_stm[sql_stm_size] = '\0';
        } else {
            // skip column that is not already handled, unexpected i
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
