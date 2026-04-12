#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#include "sql/datatype.h"
#include "sql/schematab.h"
#include "sql/parser.h"
#include "sql/utils.h"



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

/*
 * Runs .tables command, prints all tables in the DB and returns the usual return code.
 */
int runTablesCmd(const char* db_file_path) {
    FILE* database_file = fopen(db_file_path, "rb");
    if (!database_file) {
        fprintf(stderr, "Failed to open the database file\n");
        return 1;
    }

    SqliteSchemaEntries entries = getSchemaTabRowAddr(database_file);

    for (int i = 0; i < entries.count; i++) {
        // Seek to address from start of the page
        u16 cell_content_addr = *(entries.offsets + i);
        if (cell_content_addr == 0) {
            fseek(database_file, 65536, SEEK_SET);
        } else {
            fseek(database_file, cell_content_addr, SEEK_SET);
        }

        SchemaInfo info = getSchemaInfo(database_file);
        printf("%s", info.table_name);
        if (i < entries.count - 1) {
            printf(" ");
        }
    }
    printf("\n");

    free(entries.offsets);

    fclose(database_file);
    return 0;
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
        char* lc_cmd = toLowerCase(command);
        if (strncmp(lc_cmd, "select", 6) == 0) {

            // select stm
            free(lc_cmd);
            int retcode = runSelectQuery(database_file_path, command);
            return retcode;
        } else {
            fprintf(stderr, "Unknown command %s\n", command);
            return 1;
        }
    }

    return 0;
}
