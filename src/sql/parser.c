#include <string.h>
#include <stdlib.h>

#include "parser.h"


#define freeMacro(var_name) if (var_name != NULL) { free(var_name); }

ParseQueryResult parseQuery(const char* query) {
    fprintf(stderr, "debug_info: parseQuery called\n");
    ParseQueryResult result;
    result.sql_command = NULL;
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
                result.sql_command = malloc(temp_len + 1);
                memcpy(result.sql_command, temp_word, temp_len);
                result.sql_command[temp_len] = '\0';
            } else if (word_index == 1) {
                // TODO comma specific handling
                // printf("debug_info: malloc line 356\n");
                result.props[0] = (char*)malloc(temp_len + 1);
                memcpy(result.props[0], temp_word, temp_len);
                result.props[0][temp_len] = '\0';
                result.prop_len++;
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

    freeMacro(temp_word);
    return result;
}

ColumnList parseCreateTblStmt(const char* statement) {
    ColumnList result;
    result.columns = malloc(100 * sizeof(ColumnData)); // assuming that there are not more than 100 properties
    // TODO: implement dynamic array
    result.num_columns = 0;

    s8 is_in_bracket = 0;
    u8 is_reading_col_name = 0;
    u8 is_reading_col_type = 0;

    char* temp_word = malloc(100 * sizeof(char)); // assuming that there are not more than 100 character column names
    u8 temp_len = 0;

    for (int i = 0; i < strlen(statement); i++) {
        if (statement[i] == ' ' && temp_len == 0) {
            continue;
        }

        if (statement[i] == '(') {
            is_in_bracket = 1;
            is_reading_col_name = 1;
            continue;
        }

        if (is_in_bracket) {
            if (statement[i] == ' ' && is_reading_col_name) {
                result.columns[result.num_columns].name = malloc(temp_len + 1);
                memcpy(result.columns[result.num_columns].name, temp_word, temp_len);
                // Dont increase num_column yet, increment after reading column type
                result.columns[result.num_columns].name[temp_len] = '\0';

                is_reading_col_name = 0;
                is_reading_col_type = 1;
                temp_len = 0;
                continue;
            }

            if ((statement[i] == ' ' || statement[i] == ',' || statement[i] == ')') && is_reading_col_type) {
                result.columns[result.num_columns].type = malloc(temp_len + 1);
                memcpy(result.columns[result.num_columns].type, temp_word, temp_len);
                result.columns[result.num_columns].type[temp_len] = '\0';

                result.num_columns++;

                is_reading_col_type = 0;
                temp_len = 0;

                if ( statement[i] == ')' ) {
                    break;
                } else if ( statement[i] == ',' ) {
                    is_reading_col_name = 1;
                }
                continue;
            }
            if ( statement[i] == ',' ) {
                // skips additional constraints eg: "id integer primary key"
                // we read "id" and "integer", and skip "primary" and "key"
                // only starting reading column again on
                is_reading_col_name = 1;
                temp_len = 0;
                continue;
            }

            // start reading columns
            temp_word[temp_len] = statement[i];
            temp_len++;
        }
    }

    // cpy from 100 size column list to the exact size to not waste memory
    ColumnList final_result;
    if (result.num_columns > 0) {
        final_result.columns = malloc(result.num_columns * sizeof(ColumnData));
    } else {
        final_result.columns = NULL;
    }

    final_result.num_columns = result.num_columns;

    memcpy(final_result.columns, result.columns, result.num_columns * sizeof(ColumnData));

    // free only extra memory after num_columns since memcpy is copying the ptrs to ColumnData
    for (int i = result.num_columns; i < 100; i++) {
        freeMacro(result.columns[i].name);
        freeMacro(result.columns[i].type);
    }
    freeMacro(result.columns);
    freeMacro(temp_word);

    return final_result;
}
