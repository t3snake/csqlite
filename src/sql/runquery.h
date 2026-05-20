#ifndef RUNQUERY_H_INCLUDED
#define RUNQUERY_H_INCLUDED
#include <stdio.h>

#include "datatype.h"
#include "parser.h"


/*
 * Exposes relevant details about the table found and if the table was found.
 *
 * see function: seekToTable
 */
typedef struct {
    u8 is_table_found; //
    s64 page_address;
    char* create_sql_stm;
} TableInfo;

/*
 * Seeks given File pointer to the given table_name. Returns 1 if the table found, else 0.
 * Goes over all sqlite_schema table rows and seeks to the
 * address of the page in which the table exists.
 */
TableInfo seekToTable(FILE* db_file, char* table_name, u16 page_size);

/*
 * Crawls the B-Tree which can span multiple pages and prints out row if where condition is satisfied.
 * Recursive function which expects db_file to be set to the appropriate page address.
 * Returns standard retcode.
 */
int traverseBTree(FILE* db_file, ParseQueryResult query, s64 cur_pg_addr, ColumnList col_data, u16 page_size);

/*
 * Gives a bool (0 or 1) based on if where was satisfied for the current row values in cols.
 */
u8 isWhereSatisfied(WhereTree* where_tree, ColumnList cols);

#endif
