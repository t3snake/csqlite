#include <string.h>
#include <stdio.h>
#include <stdlib.h>

typedef unsigned char u8;
typedef unsigned short u16;
typedef unsigned long u64;

typedef struct _ParseVarintResult {
    u64 value;
    u8 byte_span;
} ParseVarintResult;

/*
 * Parses varint as described by sqlite docs, given a file pointer with pointer set to position where varint needs to be derived.
 * Varints are 1 - 9 bytes. For the first 8 bytes Most Significant Bit of each byte is either 1 or 0, where 1 signals that sequence is not finished.
 * 0 signals the end of the sequence. The 9th byte uses all 8 bits. Max int covered is 8bytes.
 *
 * Note: This parses with the assumption that bytes are arranged in Big-Endian order.
 */
ParseVarintResult parseVarint(FILE* db_file) {
    // read 1 to 9 bytes
    u64 result = 0;

    u8 cur_byte[1];
    u8 i = 9;
    while (i > 0) {
        fread(cur_byte, 1, 1, db_file);

        if (i == 1) {
            // Note: This runs only when varint is 9 bytes long
            result <<= 8; // this is the 9th byte so shift 8 bits to append the next 8
            result |= cur_byte[0];
            break;
        }

        // move result 7 bytes to append the next 7 bytes
        // Note: 0 bit shifted is still 0 so initial case is fine
        result <<= 7;

        // Add Least significant 7 bits to the overall value
        result |= (cur_byte[0] & 0x7F); // 0x7F is 01111111; doing & will remove the last bit

        if ( !( cur_byte[0] & ( 1 << 7 ) ) ) {
            // Case MSB is not set, ie stop processing more bytes
            // Note: This only run for 1 to 8 byte long varints
            break;
        }

        i--;
    }

    ParseVarintResult res;
    res.value = result;
    res.byte_span = 10-i;
    return res;
}

/*
 * Runs .dbinfo dot command, prints the DB info and returns the usual return code.
 */
int runDbInfoCmd(const char* db_file_path) {
    FILE* database_file = fopen(db_file_path, "rb");
    if (!database_file) {
        fprintf(stderr, "Failed to open the database file\n");
        return 1;
    }

    // Skip the first 16 bytes of the header
    fseek(database_file, 16, SEEK_SET);

    // Read next 2 bytes to get page size
    u8 buffer[2];

    // Note: fread also skips bytes (like fseek), take this into account
    fread(buffer, 1, 2, database_file);
    u16 page_size = (buffer[1] | (buffer[0] << 8)); // big endian

    printf("database page size: %u\n", page_size);

    // Skip next 82 bytes ie. DB Header (100 total along with already skipped 18 bytes)
    fseek(database_file, 82, SEEK_CUR);

    // Next 8 to 12 bytes are b-tree page headers
    // At offset 3, the 2 byte integer gives number of cells on the page
    // Since this b-tree is the internal sqlite_schema table, the number of rows is the number of tables (if only tables in DB)
    fseek(database_file, 3, SEEK_CUR);

    fread(buffer, 1, 2, database_file);

    u16 cell_count = (buffer[1] | (buffer[0] << 8));

    printf("number of tables: %u\n", cell_count);

    fclose(database_file);
    return 0;
}

u64 getRecordSerialTypeSize(u64 value) {
    switch (value) {
        case 0:
            return 0;
        case 1:
            return 1;
        case 2:
            return 2;
        case 3:
            return 3;
        case 4:
            return 4;
        case 5:
            return 6;
        case 6:
            return 8;
        case 7:
            return 8;
        case 8:
        case 9:
            return 0;
        case 10:
        case 11:
            return 0; // reserved by sqlite, I assume this is never used in my usecases
    }

    if (value % 2 == 0) {
        return (value - 12) / 2;
    } else {
        return (value - 13) / 2;
    }
}

typedef struct _SchemaInfo {
    char* table_name;
    u16 root_page;
} SchemaInfo;

/*
 * Goes through a single record/cell/row in internal schema table and returns the table name and rootpage.
 * Assumes that db_file is currently pointing at the start of record.
 */
SchemaInfo getSchemaInfo(FILE* db_file) {
    SchemaInfo result;
    ParseVarintResult varint;

    // Read size of the record / cell
    varint = parseVarint(db_file);
    u64 record_size = varint.value;

    // Read rowid (not used)
    parseVarint(db_file);

    // Read record header size
    varint = parseVarint(db_file);
    u64 record_hdr_size = varint.value - 1;

    u64* col_sizes = malloc(record_hdr_size * sizeof(long)); // this is fine since for schema table this wont be that long
    u64 col_len = 0;

    while (record_hdr_size > 0) {
        varint = parseVarint(db_file);

        *(col_sizes + col_len) = getRecordSerialTypeSize(varint.value);
        col_len += 1;

        record_hdr_size -= varint.byte_span;
    }

    u64 tbl_name_size = *(col_sizes + 2);
    u64 rootpage_size = *(col_sizes + 3);
    result.table_name = malloc(tbl_name_size * sizeof(char));
    for (int i = 0; i < col_len; i++) {
        u64 col_size = *(col_sizes + i);

        if (i == 2){
            fread(result.table_name, 1, tbl_name_size, db_file);
        } else if (i == 3) {
            u8* rootpage_str;
            fread(rootpage_str, 1, rootpage_size, db_file);
            // TODO parse big endian int (reverse byte order and cast to appropriate int)
        } else {
            fseek(db_file, col_size, SEEK_CUR);
        }
    }

    free(col_sizes);
    return result;
}

/*
 * Stores total count and offsets from start of the db file of all tables entries in sqlite_schema table.
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
SqliteSchemaEntries getInternalSchemaTableRowAddr(FILE* db_file) {
    // Skip DB header to reach B-Tree Page Header
    fseek(db_file, 100, SEEK_SET);

    // Go to offset 3 to get cell count
    fseek(db_file, 3, SEEK_CUR);

    u8 buffer[2];
    fread(buffer, 1, 2, db_file);

    u16 cell_count = (buffer[1]) | (buffer[0] << 8);
    fseek(db_file, 3, SEEK_CUR); // Skip 3 bytes to skip header and reach cell ptr array

    u16* cell_content_addrs = malloc(cell_count * sizeof(u16));
    for (int i = 0; i < cell_count; i++) {
        // Read next 2 bytes to get address of cell content area
        fread(buffer, 1, 2, db_file);
        *(cell_content_addrs + i) = buffer[1] | (buffer[0] << 8);
    }

    SqliteSchemaEntries entries;
    entries.count = cell_count;
    entries.offsets = cell_content_addrs;
    return entries;
}

/*
 * Runs .tables command, prints all tables in the DB and returns the usual return code.
 */
int runTablesCmd(const char* db_file_path) {
    FILE* database_file = fopen(db_file_path, "rb");
    if (!database_file) {
        fprintf(stderr, "Failed to open the database file\n");
        return 1;
    }

    SqliteSchemaEntries entries = getInternalSchemaTableRowAddr(database_file);

    for (int i = 0; i < entries.count; i++) {
        // Seek to address from start of the page
        u16 cell_content_addr = *(entries.offsets + i);
        if (cell_content_addr == 0) {
            fseek(database_file, 65536, SEEK_SET);
        } else {
            fseek(database_file, cell_content_addr, SEEK_SET);
        }

        printf("%s", getSchemaInfo(database_file));
    }
    printf("\n");

    free(entries.offsets);

    fclose(database_file);
    return 0;
}

typedef struct _ParseQueryResult {
    char* sql_cmd;
    char* table;
    char** props;
    u8 prop_len;
} ParseQueryResult;

ParseQueryResult parseQuery(const char* query) {
    ParseQueryResult result;

    u8 temp_len = 0;
    u8 word_index = 0;
    char* temp_word = malloc(100 * sizeof(char));

    result.props = (char**)malloc(100 * sizeof(int)); // upto 100 properties. need more?

    for (int i=0; i < strlen(query); i++) {
        char cur_char = *(query + i);
        if (cur_char == ' ') {
            if (word_index == 0) {
                memcpy(result.sql_cmd, temp_word, temp_len);
            } else if (word_index == 1) {
                // TODO comma specific handling
                result.props[0] = (char*)malloc(temp_len);
                memcpy(result.props[0], temp_word, temp_len);
            } else if (word_index == 2) {
                // TODO assert "from"
            } else if (word_index == 3) {
                memcpy(result.table, temp_word, temp_len);
            }
            temp_len = 0;
            word_index++;
            continue;
        }
        *(temp_word + temp_len) = cur_char;
        temp_len++;
    }
    return result;
}

int runSelectQuery(const char* db_file_path, const char* query) {
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
    SqliteSchemaEntries entries = getInternalSchemaTableRowAddr(database_file);

    for (int i = 0; i < entries.count; i++) {
        u16 offset = *(entries.offsets + i);

        if (offset == 0) {
            fseek(database_file, 65536, SEEK_SET);
        } else {
            fseek(database_file, offset, SEEK_SET);
        }

        // check if entry is the table from query
        // get rootpage back and seek page_size times rootpage
        // get all rows in the page



    }

    free(entries.offsets);
    fclose(database_file);
}

int main(int argc, char *argv[]) {
    if (argc != 3) {
        fprintf(stderr, "Usage: ./your_program.sh <database path> <command>\n");
        return 1;
    }

    const char *database_file_path = argv[1];
    const char *command = argv[2];

    fprintf(stderr, "Logs from your program will appear here!\n");

    if (strcmp(command, ".dbinfo") == 0) {
       int retcode = runDbInfoCmd(database_file_path);
       return retcode;
    } else if (strcmp(command, ".tables") == 0) {
        int retcode = runTablesCmd(database_file_path);
        return retcode;
    } else {
        if (strncmp(command, "SELECT", 6)) {
            // select stm
            int retcode = runSelectQuery(database_file_path, command);
        } else {
            fprintf(stderr, "Unknown command %s\n", command);
            return 1;
        }
    }

    return 0;
}
