#ifndef SCHEMATAB_H_INCLUDED
#define SCHEMATAB_H_INCLUDED

#include "datatype.h"


/*
 * Represents row of internal sqlite_schema table.
 * - Table Name
 * - Root Page number
 *
 * See: `getSchemaTabRowAddr` method.
 */
typedef struct _SchemaInfo {
    char* table_name;
    s64 root_page;
    char* sql_create_stm;
} SchemaInfo;

/*
 * Stores total count and offsets from start of the db file of all tables entries in sqlite_schema table.
 *
 * See: `getSchemaInfo` method.
 */
typedef struct _SqliteSchemaEntries {
    u16 count;
    u16* offsets;
} SqliteSchemaEntries;

/*
 * Returns array of offsets for all tables in internal sqlite_schema table.
 * It is responsibility of the caller to free the returned offsets in result.
 * This also seeks the db file ptr.
 */
SqliteSchemaEntries getSchemaTabRowAddr(FILE* db_file);

/*
 * Goes through a single record/cell/row in internal schema table and returns the table name, rootpage and create sql statement.
 * Assumes that db_file is currently pointing at the start of record.
 */
SchemaInfo getSchemaRowInfo(FILE* db_file);

#endif
