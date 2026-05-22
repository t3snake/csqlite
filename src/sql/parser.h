#ifndef PARSER_H_INCLUDED // include guard
#define PARSER_H_INCLUDED

#include "datatype.h"


/*
 * Represents a single Where condition.
 *
 * See: `parseQuery` method.
 */
typedef struct _WhereCondition {
    // leftside of comparison, usually column name
    char* l_col_name;
    // comparator used
    char* comparator;
    // rightside of comparison, could be literal or another column
    char* r_value;
    // Type of r_value: literal string (0), integer (1), another column (2)
    u8 r_value_mode;
} WhereCondition;

/*
 * Represents the whole where clause as a tree.
 * Leaf nodes are where conditions. They only have a valid WhereCondition other values are not to be used.
 * Non leaf node represent `and` or `or` of 2 sub-trees with a left and right ptr.
 *
 * See: `parseQuery` method.
 */
typedef struct _WhereTree {
    // Represents `and` (1) or `or` (2) of two different WhereConditions. (0) if node is a leaf node.
    u8 node_andor;
    // Representation of single where condition, only if leaf node. In this case there are no further left or right ptrs.
    WhereCondition condition;
    // Left sub tree. Null if leaf node.
    struct _WhereTree* left;
    // Right sub tree. Null if leaf node.
    struct _WhereTree* right;
} WhereTree;

/*
 * Represents an SQL query string as a struct.
 *
 * See: `parseQuery` method.
 */
typedef struct {
    char* sql_command;
    char* table;
    char** select_cols;
    u8 select_col_len;
    WhereTree* where_tree;
} ParseQueryResult;

/*
 * Holds result information after running a select query.
 *
 * See: `runSelectQuery` method.
 */
typedef struct {

    // return code as int
    int err;

} SelectQueryResults;

/*
 * Name and type of a SQLite table column.
 *
 * See: `ColumnList` struct and `parseCreateTblStmt` method.
 */
typedef struct {
    char* name; // column name derived from create tbl statement
    char* type; // column type derived from create tbl statement
    char* value; // column value, filled later while reading row data
} ColumnData;

/*
 * Holds list of all column in a SQLite table.
 *
 * See: method `parseCreateTblStmt`
 */
typedef struct {
    ColumnData* columns;
    u8 num_columns;
    char* primary_key_colname; // name of the column with primary column
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
