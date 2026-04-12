#include <string.h>
#include <stdlib.h>

#include "parser.h"
#include "utils.h"
#include "schematab.h"


ParseQueryResult parseQuery(const char* query) {
    fprintf(stderr, "debug_info: parseQuery called\n");
    ParseQueryResult result;
    result.sql_cmd = NULL;
    result.props = NULL;
    result.table = NULL;
    result.prop_len = 0;

    u8 temp_len = 0;
    u8 word_index = 0;
    // printf("debug_info: malloc line 343\n");
    char* temp_word = malloc((strlen(query) + 1) * sizeof(char));

    // printf("debug_info: malloc line 346\n");
    result.props = (char**)malloc(100 * sizeof(char*)); // upto 100 properties. need more?

    for (int i=0; i < strlen(query); i++) {
        char cur_char = *(query + i);
        if (cur_char == ' ') {
            if( (i >= 0) && *(query + i - 1) == ' '){
                continue;
            }
            if (word_index == 0) {
                result.sql_cmd = malloc(temp_len + 1);
                memcpy(result.sql_cmd, temp_word, temp_len);
                result.sql_cmd[temp_len] = '\0';
            } else if (word_index == 1) {
                // TODO comma specific handling
                // printf("debug_info: malloc line 356\n");
                result.props[0] = (char*)malloc(temp_len + 1);
                memcpy(result.props[0], temp_word, temp_len);
                result.props[0][temp_len] = '\0';
            } else if (word_index == 2) {
                // TODO assert "from"
            }
            temp_len = 0;
            word_index++;
            continue;
        }
        *(temp_word + temp_len) = cur_char;
        temp_len++;
    }

    if (word_index == 3) {
        result.table = malloc(temp_len + 1);
        memcpy(result.table, temp_word, temp_len);
        result.table[temp_len] = '\0';
    }

    free(temp_word);
    return result;
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
    fprintf(stderr, "debug_info: getInternalSchemaTableRowAddr called\n");
    SqliteSchemaEntries entries = getSchemaTabRowAddr(database_file);

    for (int i = 0; i < entries.count; i++) {
        u16 offset = *(entries.offsets + i);

        if (offset == 0) {
            fseek(database_file, 65536, SEEK_SET);
        } else {
            fseek(database_file, offset, SEEK_SET);
        }

        // check if entry is the table from query
        fprintf(stderr, "debug_info: getSchemaInfo called\n");
        SchemaInfo info = getSchemaInfo(database_file);

        char* lc_schema_tbl_name = toLowerCase(info.table_name);
        char* lc_query_tbl_name = toLowerCase(query_res.table);
        u8 cmp_res = strcmp(lc_schema_tbl_name, lc_query_tbl_name) != 0;
        free(lc_query_tbl_name);
        free(lc_schema_tbl_name);
        if (cmp_res) {
            fprintf(stderr, "debug_info: freeing info.table_name %s\n", info.table_name);
            free(info.table_name);
            continue;
        }
        fprintf(stderr, "debug_info: table name found.\n");
        // get rootpage back and seek page_size times rootpage
        fseek(database_file, page_size * (info.root_page - 1), SEEK_SET);

        // get all rows in the page
        fseek(database_file, 3, SEEK_CUR); // go to offset 3 to get cell count of the b-tree page

        fread(buffer, 1, 2, database_file);
        u16 row_count = (buffer[1] | (buffer[0] << 8));

        fprintf(stderr, "debug_info: row count %d\n", row_count);
        printf("%d\n", row_count);
        free(info.table_name);
        break;
    }
    if (query_res.props != NULL) {
        free(query_res.props);
    }
    if (query_res.sql_cmd != NULL) {
        free(query_res.sql_cmd);
    }
    if (query_res.table != NULL) {
        free(query_res.table);
    }

    if (entries.offsets != NULL) {
        free(entries.offsets);
    }

    fclose(database_file);
    return 0;
}
