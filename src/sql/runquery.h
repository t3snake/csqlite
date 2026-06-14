#ifndef RUNQUERY_H_INCLUDED
#define RUNQUERY_H_INCLUDED
#include <stdio.h>

#include "datatype.h"
#include "parser.h"

// READ: https://saveriomiroddi.github.io/SQLIte-database-file-format-diagrams/

/*
 * Exposes relevant details about the table found and if the table was found.
 *
 * see function: seekToTable
 */
typedef struct {
    u8 is_table_found; //
    u8 is_idx_found;
    s64 page_address;
    s64 idx_page_address; // assumes single index being present
    char* create_sql_stm;
    char* create_idx_stm; // assumes single index being present
} TableInfo;

typedef struct {
    s64* row_ids;
    u32 len;
} RowIds;

typedef struct {
    FILE* db_file; // File ptr reference
    s64 page_address; // Current page address
    u16 page_size; // page size of DB / OS
} FileState;

typedef struct {
    Columns idx_cols; // columns as parsed from CREATE INDEX statement
    char* where_col_value; // Value from where statement right side that index is being searched for
    u8 where_col_mode; // Mode that tells whether is int or string
} IndexSearchParams;

typedef struct {
	u8 is_index_relevant; // is this result relevant in the main query
	u32 current_search_idx; // starts with 0, maintains which row_id is to be found next while traversing table b-tree
	RowIds row_ids; // all row ids, by design should be in ascending order
} IndexSearchResult;

/*
 * Returns page_address to the given table_name. Returns if the table found.
 * Goes over all sqlite_schema table rows and details of index if relevant to table and query.
 */
TableInfo getTableDetails(FILE* db_file, char* table_name, u16 page_size);

/*
 * Crawls the Table B-Tree which can span multiple pages and **prints** out row if where condition is satisfied.
 * Recursive function which expects db_file to be set to the appropriate page address.
 * Returns standard retcode.
 * If there is an index result relevant to speed up search, that is used to selectively search
 */
int traverseTableBTree(FileState file_state, ParseQueryResult query, ColumnList col_data, IndexSearchResult* idx_search_results);

/*
 * Crawls the Index B-Tree and **returns a list** of row ids which can be used to crawl Table B-Tree faster.
 * Recursive function which expects file_state.db_file to be set to the appropriate page address (stored in file_state.page_address).
 * col_value and col_value_mode in search_params describe the value and type of the where condition literal to be checked in the index.
 */
int traverseIndexBTree(FileState file_state, IndexSearchParams search_params, RowIds* result);

/*
 * Gives a bool (0 or 1) based on if where was satisfied for the current row values in cols.
 */
u8 isWhereSatisfied(WhereTree* where_tree, ColumnList cols);

void sort(RowIds* rows);

#endif
