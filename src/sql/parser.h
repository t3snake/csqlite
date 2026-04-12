#ifndef SQLPARSER_H_INCLUDED // include guard
#define SQLPARSER_H_INCLUDED


#include "datatype.h"

/*
 * Represents an SQL query string as a struct.
 *
 * See: `parseQuery` method.
 */
typedef struct _ParseQueryResult {
    char* sql_cmd;
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
 * Parses SQL Query statement and gets components of the statement.
 * - SQL Command (eg: select)
 * - Table Name
 * - Properties to select
 * - Number of properties
 */
ParseQueryResult parseQuery(const char* query);

int runSelectQuery(const char* db_file_path, const char* query);


#endif // include guard end
