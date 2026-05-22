#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <assert.h>

#include "parser.h"
#include "utils.h"


#define freeMacro(var_name) if (var_name != NULL) { free(var_name); var_name = NULL;}

ParseQueryResult parseQuery(const char* query) {
    fprintf(stderr, "debug_info: parseQuery called\n");
    ParseQueryResult result;
    result.sql_command = NULL;
    result.select_cols = NULL;
    result.table = NULL;
    result.select_col_len = 0;

    u8 temp_len = 0;
    u8 word_index = 0;
    // printf("debug_info: malloc line 343\n");
    char* temp_word = malloc((strlen(query) + 1) * sizeof(char));

    // Select parsing

    // If is currently parsing comma separated properties
    u8 is_parsing_prop = 0;

    // True if is parsing separator during processing of properties (sequence of space and comma)
    u8 is_parsing_separator = 0;

    // True if comma is encountered while processing separator (track in a sequence of space and comma)
    // Default is_parsed_comma is 1 to handle the first property.
    u8 is_parsed_comma = 1;

    // True while parsing not space, non comma characters.
    // This is used to not repeat the check at start of word.
    u8 is_parsing_word = 0;

    // Where parsing

    // If is currently parsing where clause
    u8 is_parsing_where = 0;

    // Parse Mode in effect when `is_parsing_where` is true.
    // 0 when parsing col_name, 1 when parsing comparator, 2 when parsing literal / other col, ? 3 when parsing separator ?
    u8 where_parse_mode = 0;

    // True when parse mode is 2 and parsing literal
    // If true at end of parsing, means that the literal was not closed.
    u8 is_literal_where = 0;

    u8 is_in_quotes = 0;

    // fprintf(stderr, "debug_info: malloc line 27 parser.c\n");
    result.select_cols = (char**) malloc(100 * sizeof(char*)); // upto 100 properties. need more?

    result.where_tree = NULL;

    for (int i=0; i <= strlen(query); i++) { // <= len because will check \0 for last word handling
        char cur_char = query[i];

        if (is_parsing_where && cur_char == '\'') {
			is_in_quotes = is_in_quotes ? 0 : 1; // toggle
        }

        if ((cur_char != ' ' && cur_char != ',' && cur_char != '\0') || (is_parsing_where && is_in_quotes)) {
            if ( is_parsing_prop ) {
                if ( !is_parsing_word && !is_parsed_comma && result.select_col_len != 0 ) {
                    // While parsing properties if a space was encountered but a comma was not,
                    // That means that parsing of properties has ended.
                    // This is also true when parsing the 1st property so check if prop_len is not 0
                    is_parsing_prop = 0;
                    word_index++;
                }

                is_parsing_separator = 0;
                is_parsed_comma = 0;
                is_parsing_word = 1;
            }

            *(temp_word + temp_len) = cur_char;
            temp_len++;

            continue;
        }

        // space or comma or null(end of string) case after this

        if ( (i > 0) && query[i-1] == ' ' ){
            // multiple spaces case
            continue;
        }

        // Select property parsing - separator case
        if (is_parsing_prop) {
            if (!is_parsing_separator) {
                // printf("debug_info: malloc line 38 parser.c\n");
                result.select_cols[result.select_col_len] = (char*)malloc(temp_len + 1);
                memcpy(result.select_cols[result.select_col_len], temp_word, temp_len);
                result.select_cols[result.select_col_len][temp_len] = '\0';
                result.select_col_len++;
                temp_len = 0;
            }

            is_parsing_separator = 1;
            is_parsing_word = 0;

            if (cur_char == ',') {
                is_parsed_comma = 1;
            }

            continue;
        }

        // Where property parsing - separator case
        if (is_parsing_where) {
            if (where_parse_mode == 0) {
                // parsing col_name
                result.where_tree->condition.l_col_name = (char*) malloc(temp_len + 1);
                memcpy(result.where_tree->condition.l_col_name, temp_word, temp_len);
                result.where_tree->condition.l_col_name[temp_len] = '\0';
            } else if (where_parse_mode == 1) {
                // comparator
                result.where_tree->condition.comparator = (char*) malloc(temp_len + 1);
                memcpy(result.where_tree->condition.comparator, temp_word, temp_len);
                result.where_tree->condition.comparator[temp_len] = '\0';
            } else if (where_parse_mode == 2) {
                // literal or another column
                if (temp_word[0] == '\'' && temp_word[temp_len - 1] == '\'') {
                    // if in 'quotes'
                    result.where_tree->condition.r_value_mode = 0;

                    result.where_tree->condition.r_value = (char*) malloc(temp_len + 1 - 2); // remove the quotes
                    memcpy(result.where_tree->condition.r_value, (temp_word + 1), (temp_len - 1));
                    result.where_tree->condition.r_value[temp_len - 2] = '\0';
                } else if (isNum(temp_word[0])) {
                    // if is number
                    // assumption if digit starts with number, it cant be a var - assume it is num
                    result.where_tree->condition.r_value_mode = 1;

                    result.where_tree->condition.r_value = (char*) malloc(temp_len + 1);
                    memcpy(result.where_tree->condition.r_value, temp_word, temp_len);
                    result.where_tree->condition.r_value[temp_len] = '\0';
                } else {
                    // if a col_name
                    result.where_tree->condition.r_value_mode = 2;

                    result.where_tree->condition.r_value = (char*) malloc(temp_len + 1);
                    memcpy(result.where_tree->condition.r_value, temp_word, temp_len);
                    result.where_tree->condition.r_value[temp_len] = '\0';
                }

            }

            where_parse_mode = (where_parse_mode + 1) % 3;
        }

        // different section handling
        if (word_index == 0) {
            // SELECT
            result.sql_command = malloc(temp_len + 1);
            memcpy(result.sql_command, temp_word, temp_len);
            result.sql_command[temp_len] = '\0';

            char* lc_result = toLowerCase(result.sql_command);
            assert(strcmp(lc_result, "select") == 0);
            freeMacro(lc_result);

            is_parsing_prop = 1;
        } else if (word_index == 1) {
            // comma separated properties to be separated
            // This wont hit since parsing of comma separated props happens above
        } else if (word_index == 2) {
            // FROM
            is_parsing_prop = 0;
            char* from = malloc(temp_len + 1);
            memcpy(from, temp_word, temp_len);
            from[temp_len] = '\0';

            char* lc_from = toLowerCase(from);
            assert(strcmp(lc_from, "from") == 0);

            freeMacro(from);
            freeMacro(lc_from);
        } else if (word_index == 3) {
            // table name
            result.table = malloc(temp_len + 1);
            memcpy(result.table, temp_word, temp_len);
            result.table[temp_len] = '\0';
        } else if (word_index == 4) {
            // WHERE
            char* where = malloc(temp_len + 1);
            memcpy(where, temp_word, temp_len);
            where[temp_len] = '\0';

            char* lc_where = toLowerCase(where);
            assert(strcmp(lc_where, "where") == 0);

            result.where_tree = (WhereTree*) malloc(sizeof(WhereTree));
            result.where_tree->node_andor = 0;
            result.where_tree->left = 0;
            result.where_tree->right = 0;

            is_parsing_where = 1;
            where_parse_mode = 0;

            freeMacro(where);
            freeMacro(lc_where);
        }

        temp_len = 0;
        word_index++;
        continue;
    }

    // debug
    for (int i=0; i < result.select_col_len; i++) {
        fprintf(stderr, "%s, ", result.select_cols[i]);
    }
    fprintf(stderr, "\n");

    freeMacro(temp_word);
    return result;
}

ColumnList parseCreateTblStmt(const char* statement) {
    ColumnList result;
    result.columns = (ColumnData*) malloc(100 * sizeof(ColumnData)); // assuming that there are not more than 100 properties
    // TODO: implement dynamic array
    result.num_columns = 0;
    result.primary_key_colname = NULL;

    char* last_read_colname = NULL;

    s8 is_in_bracket = 0;
    u8 is_reading_col_name = 0;
    u8 is_reading_col_type = 0;

    u8 is_read_primary = 0; // flag for if primary is read, when reading "primary key"

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

        if (!is_in_bracket) {
            continue;
        }

        if (statement[i] == ' ' && is_reading_col_name) {
            result.columns[result.num_columns].name = malloc(temp_len + 1);
            memcpy(result.columns[result.num_columns].name, temp_word, temp_len);
            // Dont increase num_column yet, increment after reading column type
            result.columns[result.num_columns].name[temp_len] = '\0';

            // ptr to read colname in case it turns out to be primary key
            last_read_colname = result.columns[result.num_columns].name;

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

        if ( !is_reading_col_name && !is_reading_col_type ) {
            if ( statement[i] == ',' || statement[i] == ' ' || statement[i] == ')' ) {
                // check if "primary" or "key"
                char* x = malloc(temp_len + 1);
                memcpy(x, temp_word, temp_len);
                x[temp_len] = '\0';

                if (strcmp(x, "primary") == 0) {
                    is_read_primary = 1;
                } else if (is_read_primary && (strcmp(x, "key") == 0)) {
                    result.primary_key_colname = last_read_colname;
                    is_read_primary = 0;
                    fprintf(stderr, "primary col name: %s\n", last_read_colname);
                }
                temp_len = 0;

                if ( statement[i] == ',' ) {
                    // only starting reading column name again on reading comma
                    is_reading_col_name = 1;
                }
                continue;
            }
        }

        temp_word[temp_len] = statement[i];
        temp_len++;
    }

    freeMacro(temp_word);

    return result;
}
