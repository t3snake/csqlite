#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>

#include "sql/datatype.h"
#include "sql/schematab.h"
#include "sql/parser.h"
#include "sql/utils.h"


#define freeMacro(var_name) if (var_name != NULL) { free(var_name); }

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

    // Find table in schema table
    fprintf(stderr, "debug_info: getSchemaTabRowAddr called\n");
    SqliteSchemaEntries entries = getSchemaTabRowAddr(database_file);

    for (int i = 0; i < entries.count; i++) {
        u16 offset = *(entries.offsets + i);

        if (offset == 0) {
            fseek(database_file, 65536, SEEK_SET);
        } else {
            fseek(database_file, offset, SEEK_SET);
        }

        // check if entry is the table from query
        fprintf(stderr, "debug_info: getSchemaRowInfo called\n");
        SchemaInfo info = getSchemaRowInfo(database_file);

        char* lc_schema_tbl_name = toLowerCase(info.table_name);
        char* lc_query_tbl_name = toLowerCase(query_res.table);

        u8 cmp_res = strcmp(lc_schema_tbl_name, lc_query_tbl_name) != 0;

        freeMacro(lc_query_tbl_name);
        freeMacro(lc_schema_tbl_name);

        if (cmp_res) {
            fprintf(stderr, "debug_info: freeing info.table_name %s\n", info.table_name);

            freeMacro(info.table_name);
            freeMacro(info.sql_create_stm);
            continue;
        }

        fprintf(stderr, "debug_info: table name found.\n");
        s64 page_address = page_size * (info.root_page - 1);
        // get rootpage back and seek page_size times rootpage
        fseek(database_file, page_address, SEEK_SET);

        // get all rows in the page
        fseek(database_file, 3, SEEK_CUR); // go to offset 3 to get cell count of the b-tree page

        fread(buffer, 1, 2, database_file);
        u16 row_count = (buffer[1] | (buffer[0] << 8));
        fprintf(stderr, "debug_info: row count %d\n", row_count);

        char* property = query_res.select_col_len >  0 ? toLowerCase(query_res.select_cols[0]) : "";
        if (query_res.select_col_len > 0 && strcmp(property, "count(*)") == 0) {
            printf("%d\n", row_count);
            // TODO: with multiple properties to select, need to check if this is the last one to break
            break;
        }

        fseek(database_file, 3, SEEK_CUR); // skip 3 bytes to reach end of header and reach cell ptr array

        u16* row_offsets = malloc(row_count * sizeof(u16));
        for (int j = 0; j < row_count; j++) {
            // Read next 2 bytes to get address of cell content area
            fread(buffer, 1, 2, database_file);
            *(row_offsets + j) = buffer[1] | (buffer[0] << 8);
        }

        fprintf(stderr, "debug_info: calling parseCreateTblStmt\n");
        fprintf(stderr, "debug_info: %s\n", info.sql_create_stm);
        // parse sql create statement to get column order
        ColumnList col_list = parseCreateTblStmt(info.sql_create_stm);
        fprintf(stderr, "debug_info: successfully parsed create statement\n");

        // TODO go over each row and get the required properties
        for(int j = 0; j < row_count; j++) {
            ParseVarintResult varint;

            // Seek to row offset from beginning of the page to reach row.
            s64 row_offset = page_address + row_offsets[j];
            fseek(database_file, row_offset, SEEK_SET);

            varint = parseVarint(database_file); // Size of record
            u64 record_size = varint.value;

            varint = parseVarint(database_file); // row id (not needed unless selected?)

            varint = parseVarint(database_file); //record header size
            u64 record_hdr_size = varint.value - 1; // subtracting size of itself

            u64 col_len = col_list.num_columns;
            u64* col_sizes = malloc(col_len * sizeof(u64)); // Assumption less than 100 columns

            for (int k = 0; record_hdr_size > 0; k++) {
                varint = parseVarint(database_file);

                col_sizes[k] = getRecordSerialTypeSize(varint.value);
                record_hdr_size -= varint.byte_span;
            }

            // Buffer of string to store values which are sequentially stored in db file
            char** col_print_vals = malloc(col_len * sizeof(char*));

            // Buffer to store the col names in order stored in db file
            char** col_names = malloc(col_len * sizeof(char*));

            // store all column values of the row
            // check where condition to see if the row should be skipped
            // since where condition could be on any column: need not be in select and could be last row
            // we have to go through all the columns
            // TODO possible optimization => short circuit or/and of where condition
            for (int k = 0; k < col_len; k++) {
                // go over each column in the current row, order as stored in .db file
                u64 col_size = col_sizes[k];
                char* col_name = col_list.columns[k].name;
                char* col_type = col_list.columns[k].type;
                // fprintf(stderr, "debug_info: column %s: %s\n", col_name, col_type);
                col_names[k] = col_name;

                if (strcmp(col_type, "text") == 0) {
                    char* text = malloc((col_size + 1) * sizeof(char));
                    fread(text, 1, col_size, database_file);
                    text[col_size] = '\0';

                    col_print_vals[k] = text;
                } else if (strcmp(col_type, "int") == 0 || strcmp(col_type, "integer") == 0) {
                    u8* bytes = malloc(col_size);
                    fread(bytes, 1, col_size, database_file);

                    s64 int_value = parseSqlInt(bytes, col_size);
                    free(bytes);

                    col_print_vals[k] = malloc(100 * sizeof(char));
                    sprintf(col_print_vals[k], "%lld", int_value);
                } else {
                    fseek(database_file, col_size, SEEK_CUR);
                    col_print_vals[k] = NULL;
                }
            }

            u8 is_where_satisfied = 0; // is where condition satisfied for this row

            if (query_res.where_tree != NULL) {
                // check if row satisfies where condition
                // TODO where tree handling, currently assumes only one condition present at root
                char* l_col_name = query_res.where_tree->condition.l_col_name;
                char* r_col_name = NULL; // valid only if right side value represents column

                char* literal_value = NULL; // valid only if right side value is int or string (not column)

                u8 is_r_col = (query_res.where_tree->condition.r_value_mode == 2);
                if (is_r_col) {
                    r_col_name = query_res.where_tree->condition.r_value;
                } else {
                    literal_value = query_res.where_tree->condition.r_value;
                }

                char* l_col_value = NULL;
                char* r_col_value = NULL; // only valid if the right side value is a column and not literal

                // get l_col and r_col (if relevant) values
                for (int k = 0; k < col_len; k++) {
                    if (strcmp(l_col_name, col_names[k]) == 0) {
                        l_col_value = col_print_vals[k];
                        if (!is_r_col) {
                            // only break if there is r_value is not a column, else continue search for r_col
                            break;
                        }
                    }

                    if (is_r_col && strcmp(r_col_name, col_names[k]) == 0) {
                        r_col_value = col_print_vals[k];
                    }
                }

                assert(l_col_value != NULL);


                switch (query_res.where_tree->condition.r_value_mode) {
                    case 0:
                        // literal string
                        assert(literal_value != NULL);
                        is_where_satisfied = (strcmp(l_col_value, literal_value) == 0);
                        break;

                    case 1:
                        assert(literal_value != NULL);
                        is_where_satisfied = (strcmp(l_col_value, literal_value) == 0);
                        break;

                    case 2:
                        assert(is_r_col && r_col_value != NULL);
                        is_where_satisfied = (strcmp(l_col_value, r_col_value) == 0);
                        break;

                    default:
                        // invalid r_value
                        fprintf(stderr, "Invalid r_value_mode in WhereCondition %d", query_res.where_tree->condition.r_value_mode);
                }
            } else {
                is_where_satisfied = 1;
            }

            if (!is_where_satisfied) {
                // go to next row
                continue;
            }

            for (int k = 0; k < query_res.select_col_len; k++) {
                char* col_value;
                for (int l = 0; l < col_len; l++) {
                    if (strcmp(col_names[l], query_res.select_cols[k]) == 0) {
                        col_value = col_print_vals[l];
                    }
                }
                printf("%s", col_value);

                if (k == query_res.select_col_len - 1) {
                    printf("\n");
                } else {
                    printf("|");
                }
            }
        }

        for (int i = 0; i < col_list.num_columns; i++) {
            freeMacro(col_list.columns[i].name);
            freeMacro(col_list.columns[i].type);
        }
        freeMacro(col_list.columns);

        freeMacro(info.table_name);
        freeMacro(info.sql_create_stm);

        break;
    }

    freeMacro(query_res.select_cols);
    freeMacro(query_res.sql_command);
    freeMacro(query_res.table);

    freeMacro(entries.offsets);

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
