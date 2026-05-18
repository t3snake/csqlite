#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>

#include "sql/datatype.h"
#include "sql/schematab.h"
#include "sql/parser.h"
#include "sql/runquery.h"
#include "sql/utils.h"


#define freeMacro(var_name) if (var_name != NULL) { free(var_name); var_name = NULL; }

/*
 * Runs .dbinfo dot command, prints the DB info and returns the usual return code.
 */
int runDbInfoCmd(const char* db_file_path) {
    FILE* database_file = fopen(db_file_path, "rb");
    if (!database_file) {
        fprintf(stderr, "Failed to open the database file\n");
        return 1;
    }

    // Skip the first 16 bytes of the header
    fseek(database_file, 16, SEEK_SET);

    // Read next 2 bytes to get page size
    u8 buffer[2];

    // Note: fread also skips bytes (like fseek), take this into account
    fread(buffer, 1, 2, database_file);
    u16 page_size = (buffer[1] | (buffer[0] << 8)); // big endian

    printf("database page size: %u\n", page_size);

    // Skip next 82 bytes ie. DB Header (100 total along with already skipped 18 bytes)
    fseek(database_file, 82, SEEK_CUR);

    // Next 8 to 12 bytes are b-tree page headers
    // At offset 3, the 2 byte integer gives number of cells on the page
    // Since this b-tree is the internal sqlite_schema table, the number of rows is the number of tables (if only tables in DB)
    fseek(database_file, 3, SEEK_CUR);

    fread(buffer, 1, 2, database_file);

    u16 cell_count = (buffer[1] | (buffer[0] << 8));

    printf("number of tables: %u\n", cell_count);

    fclose(database_file);
    return 0;
}

/*
 * Runs .tables command, prints all tables in the DB and returns the usual return code.
 */
int runTablesCmd(const char* db_file_path) {
    FILE* database_file = fopen(db_file_path, "rb");
    if (!database_file) {
        fprintf(stderr, "Failed to open the database file\n");
        return 1;
    }

    SqliteSchemaEntries entries = getSchemaTabRowAddr(database_file);

    for (int i = 0; i < entries.count; i++) {
        // Seek to address from start of the page
        u16 cell_content_addr = *(entries.offsets + i);
        if (cell_content_addr == 0) {
            fseek(database_file, 65536, SEEK_SET);
        } else {
            fseek(database_file, cell_content_addr, SEEK_SET);
        }

        SchemaInfo info = getSchemaRowInfo(database_file);
        printf("%s", info.table_name);
        if (i < entries.count - 1) {
            printf(" ");
        }
    }
    printf("\n");

    free(entries.offsets);

    fclose(database_file);
    return 0;
}

int runSelectQuery(const char* db_file_path, const char* query) {
    fprintf(stderr, "debug_info: runSelectQuery called\n");
    FILE* database_file = fopen(db_file_path, "rb");
    if (!database_file) {
        fprintf(stderr, "Failed to open the database file\n");
        return 1;
    }

    // Get page size
    fseek(database_file, 16, SEEK_SET);

    u8 buffer[2];
    fread(buffer, 1, 2, database_file);
    u16 page_size = (buffer[1] | (buffer[0] << 8));

    ParseQueryResult query_res = parseQuery(query);

    TableInfo tbl_info = seekToTable(database_file, query_res.table, page_size);

    if (!tbl_info.is_table_found) {
        fprintf(stderr, "table %s not found in the database.\n", query_res.table);
        return 1;
    }

    // read page header

    // TODO: see if leaf or interior page

    // get all rows in the page
    fseek(database_file, 3, SEEK_CUR); // go to offset 3 to get cell count of the b-tree page

    fread(buffer, 1, 2, database_file);
    u16 row_count = (buffer[1] | (buffer[0] << 8)); // to be used if count(*) property in sql query
    fprintf(stderr, "debug_info: row count %d\n", row_count);

    // Mini optimization: if count * is only property, early return
    char* property = query_res.select_col_len > 0 ? toLowerCase(query_res.select_cols[0]) : "";
    if (query_res.select_col_len == 1 && strcmp(property, "count(*)") == 0) {
        printf("%d\n", row_count);
        return 0;
    }
    freeMacro(property);

    // TODO following line only in case of leaf node (8byte header).
    // for interior node it is 12 byte and we need to store right most child pointer
    fseek(database_file, 3, SEEK_CUR); // skip 3 bytes to reach end of header and reach cell ptr array

    // get row offset in page for each row
    u16* row_offsets = malloc(row_count * sizeof(u16));
    for (int j = 0; j < row_count; j++) {
        // Read next 2 bytes to get address of cell content area
        fread(buffer, 1, 2, database_file);
        *(row_offsets + j) = buffer[1] | (buffer[0] << 8);
    }

    fprintf(stderr, "debug_info: calling parseCreateTblStmt\n");
    fprintf(stderr, "debug_info: %s\n", tbl_info.create_sql_stm);

    // parse sql create statement to get order of how columns are stored
    ColumnList col_data = parseCreateTblStmt(tbl_info.create_sql_stm);
    fprintf(stderr, "debug_info: successfully parsed create statement\n");

    // go over each row
    for(int i = 0; i < row_count; i++) {
        ParseVarintResult varint;

        // Seek to row offset from beginning of the page to reach row.
        s64 row_offset = tbl_info.page_address + row_offsets[i];
        fseek(database_file, row_offset, SEEK_SET);

        varint = parseVarint(database_file); // Size of record
        u64 record_size = varint.value;

        varint = parseVarint(database_file); // row id (not needed unless selected?)

        varint = parseVarint(database_file); //record header size
        u64 record_hdr_size = varint.value - 1; // subtracting size of itself

        u64 col_len = col_data.num_columns;
        u64* col_sizes = malloc(col_len * sizeof(u64)); // Assumption less than 100 columns

        for (int k = 0; record_hdr_size > 0; k++) {
            varint = parseVarint(database_file);

            col_sizes[k] = getRecordSerialTypeSize(varint.value);
            record_hdr_size -= varint.byte_span;
        }

        // store all column values of the row
        for (int j = 0; j < col_len; j++) {
            // go over each column in the current row, order as stored in .db file

            // Note: we already have col name and type in struct when parsing create col,
            // we update val in same struct, to avoid extra memory
            u64 col_size = col_sizes[j];
            char* col_type = col_data.columns[j].type;
            // fprintf(stderr, "debug_info: column %s: %s\n", col_name, col_type);

            if (strcmp(col_type, "text") == 0) {
                char* text = malloc((col_size + 1) * sizeof(char));
                fread(text, 1, col_size, database_file);
                text[col_size] = '\0';

                col_data.columns[j].value = text;
            } else if (strcmp(col_type, "int") == 0 || strcmp(col_type, "integer") == 0) {
                u8* bytes = malloc(col_size);
                fread(bytes, 1, col_size, database_file);

                s64 int_value = parseSqlInt(bytes, col_size);
                free(bytes);

                col_data.columns[j].value = malloc(100 * sizeof(char));
                sprintf(col_data.columns[j].value, "%lld", int_value);
            } else {
                fseek(database_file, col_size, SEEK_CUR);
                col_data.columns[j].value = NULL;
            }
        }

        // check where condition to see if the row should be skipped
        u8 is_where_satisfied = isWhereSatisfied(query_res.where_tree, col_data);

        if (!is_where_satisfied) {
            // go to next row
            continue;
        }

        // print results if row to be printed
        char* count_star = "count(*)";
        for (int j = 0; j < query_res.select_col_len; j++) {
            char* lc_col_name = toLowerCase(query_res.select_cols[j]);
            if (strcmp(lc_col_name, count_star) == 0) {
                printf("%d", row_count);
            } else {
                char* col_value;
                for (int k = 0; k < col_len; k++) {
                    if (strcmp(col_data.columns[k].name, query_res.select_cols[j]) == 0) {
                        col_value = col_data.columns[k].value;
                    }
                }
                printf("%s", col_value);
            }

            if (j == query_res.select_col_len - 1) {
                printf("\n");
            } else {
                printf("|");
            }

            freeMacro(lc_col_name);
        }
        freeMacro(count_star);
    }

    for (int i = 0; i < col_data.num_columns; i++) {
        freeMacro(col_data.columns[i].name);
        freeMacro(col_data.columns[i].type);
        freeMacro(col_data.columns[i].value);
    }
    freeMacro(col_data.columns);

    freeMacro(tbl_info.create_sql_stm);

    freeMacro(query_res.select_cols);
    freeMacro(query_res.sql_command);
    freeMacro(query_res.table);

    // Note: only single node since current implementation only supports one condition.
    freeMacro(query_res.where_tree->left);
    freeMacro(query_res.where_tree->right);
    freeMacro(query_res.where_tree->condition.comparator);
    freeMacro(query_res.where_tree->condition.l_col_name);
    freeMacro(query_res.where_tree->condition.r_value);
    freeMacro(query_res.where_tree);

    fclose(database_file);
    return 0;
}

int main(int argc, char *argv[]) {
    if (argc != 3) {
        fprintf(stderr, "Usage: ./your_program.sh <database path> <command>\n");
        return 1;
    }

    const char *database_file_path = argv[1];
    const char *command = argv[2];

    fprintf(stderr, "Logs from your program will appear here!\n");

    if (strcmp(command, ".dbinfo") == 0) {
       int retcode = runDbInfoCmd(database_file_path);
       return retcode;
    } else if (strcmp(command, ".tables") == 0) {
        int retcode = runTablesCmd(database_file_path);
        return retcode;
    } else {
        char* lc_cmd = toLowerCase(command);
        if (strncmp(lc_cmd, "select", 6) == 0) {

            // select stm
            free(lc_cmd);
            int retcode = runSelectQuery(database_file_path, command);
            return retcode;
        } else {
            fprintf(stderr, "Unknown command %s\n", command);
            return 1;
        }
    }

    return 0;
}
