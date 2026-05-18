#include "runquery.h"

#include <string.h>
#include <stdlib.h>
#include <assert.h>

#include "schematab.h"
#include "utils.h"


#define freeMacro(var_name) if (var_name != NULL) { free(var_name); }

TableInfo seekToTable(FILE* db_file, char* table_name, u16 page_size) {
    TableInfo tbl_info;
    tbl_info.is_table_found = 0;
    tbl_info.create_sql_stm = NULL;
    tbl_info.page_address = -1;

    // Find table in schema table
    fprintf(stderr, "debug_info: getSchemaTabRowAddr called\n");
    SqliteSchemaEntries entries = getSchemaTabRowAddr(db_file);

    for (int i = 0; i < entries.count; i++) {
        u16 offset = *(entries.offsets + i);

        if (offset == 0) {
            fseek(db_file, 65536, SEEK_SET);
        } else {
            fseek(db_file, offset, SEEK_SET);
        }

        // check if entry is the table from query
        fprintf(stderr, "debug_info: getSchemaRowInfo called\n");
        SchemaInfo info = getSchemaRowInfo(db_file);

        char* lc_schema_tbl_name = toLowerCase(info.table_name);
        char* lc_query_tbl_name = toLowerCase(table_name);

        u8 cmp_res = strcmp(lc_schema_tbl_name, lc_query_tbl_name) != 0;

        freeMacro(lc_query_tbl_name);
        freeMacro(lc_schema_tbl_name);

        if (cmp_res) {
            // fprintf(stderr, "debug_info: freeing info.table_name %s\n", info.table_name);
            freeMacro(info.table_name);
            freeMacro(info.sql_create_stm);
            continue;
        }

        fprintf(stderr, "debug_info: table name found.\n");

        tbl_info.is_table_found = 1;
        tbl_info.page_address = page_size * (info.root_page - 1); // the first rootpage starts at address 0, usually the internal table
        tbl_info.create_sql_stm = info.sql_create_stm;

        fseek(db_file, tbl_info.page_address, SEEK_SET); // Note: should this function do it, or the caller?

        freeMacro(info.table_name);
        freeMacro(entries.offsets);
    }

    return tbl_info;
}

u8 isWhereSatisfied(WhereTree* where_tree, ColumnList cols) {
    // since where condition could be on any column: need not be in select and could be last row
    // we have to go through all the columns
    // TODO possible optimization => short circuit or/and of where condition
    if (where_tree == NULL) {
        return 0;
    }

    u8 is_where_satisfied = 0;
    // check if row satisfies where condition
    // TODO where tree handling, currently assumes only one condition present at root
    char* l_col_name = where_tree->condition.l_col_name;
    char* r_col_name = NULL; // valid only if right side value represents column

    char* literal_value = NULL; // valid only if right side value is int or string (not column)

    u8 is_r_col = (where_tree->condition.r_value_mode == 2);
    if (is_r_col) {
        r_col_name = where_tree->condition.r_value;
    } else {
        literal_value = where_tree->condition.r_value;
    }

    char* l_col_value = NULL;
    char* r_col_value = NULL; // only valid if the right side value is a column and not literal

    // get l_col and r_col (if relevant) values
    for (int k = 0; k < cols.num_columns; k++) {
        if (strcmp(l_col_name, cols.columns[k].name) == 0) {
            l_col_value = cols.columns[k].value;
            if (!is_r_col) {
                // only break if there is r_value is not a column, else continue search for r_col
                break;
            }
        }

        if (is_r_col && strcmp(r_col_name, cols.columns[k].name) == 0) {
            r_col_value = cols.columns[k].value;
        }
    }

    assert(l_col_value != NULL);

    switch (where_tree->condition.r_value_mode) {
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
            fprintf(stderr, "Invalid r_value_mode in WhereCondition %d", where_tree->condition.r_value_mode);
    }

    return is_where_satisfied;
}
