#ifndef SQLPARSER_H_INCLUDED // include guard
#define SQLPARSER_H_INCLUDED

#include "datatype.h"


/*
 * Represents an SQL query string as a struct.
 *
 * See: `parseQuery` method.
 */
typedef struct _ParseQueryResult {
    char* sql_command;
    char* table;
    char** props;
    u8 prop_len;
} ParseQueryResult;

/*
 * Holds result information after running a select query.
 *
 * See: `runSelectQuery` method.
 */
typedef struct _SelectQueryResults {

    // return code as int
    int err;

} SelectQueryResults;

/*
 * Column name and type for SQLite.
 *
 * See: `ColumnList` struct and `parseCreateTblStmt` method.
 */
typedef struct _ColumnData {
    char* name;
    char* type;
} ColumnData;

/*
 * Holds list of column names
 */
typedef struct _ColumnList {
    ColumnData* columns;
    u8 num_columns;
} ColumnList;

/*
 * Parses SQL Query statement and gets components of the statement.
 * - SQL Command (eg: select)
 * - Table Name
 * - Properties to select
 * - Number of properties
 */
ParseQueryResult parseQuery(const char* query);

/*
 * Parses CREATE TABLE statement to get list of columns (strings) in the table.
 */
ColumnList parseCreateTblStmt(const char* statement);

#endif // include guard end
