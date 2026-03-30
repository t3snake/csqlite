#include <string.h>
#include <stdio.h>
#include <stdlib.h>

int runDbInfoCmd(const char* db_file_path) {
    FILE *database_file = fopen(db_file_path, "rb");
    if (!database_file) {
        fprintf(stderr, "Failed to open the database file\n");
        return 1;
    }

    // Skip the first 16 bytes of the header
    fseek(database_file, 16, SEEK_SET);

    // Read next 2 bytes to get page size
    unsigned char buffer[2];

    // Note: fread also skips bytes (like fseek), take this into account
    fread(buffer, 1, 2, database_file);
    unsigned short page_size = (buffer[1] | (buffer[0] << 8)); // big endian

    printf("database page size: %u\n", page_size);

    // Skip next 82 bytes ie. DB Header (100 total along with already skipped 18 bytes)
    fseek(database_file, 82, SEEK_CUR);

    // Next 8 to 12 bytes are b-tree page headers
    // At offset 3, the 2 byte integer gives number of cells on the page
    // Since this b-tree is the internal sqlite_schema table, the number of rows is the number of tables (if only tables in DB)
    fseek(database_file, 3, SEEK_CUR);

    fread(buffer, 1, 2, database_file);

    unsigned short cell_count = (buffer[1] | (buffer[0] << 8));

    printf("number of tables: %u\n", cell_count);

    fclose(database_file);
    return 0;
}

int runTablesCmd(const char* db_file_path) {
    FILE *database_file = fopen(db_file_path, "rb");
    if (!database_file) {
        fprintf(stderr, "Failed to open the database file\n");
        return 1;
    }

    // Skip DB header to reach B-Tree Page Header
    fseek(database_file, 100, SEEK_SET);

    // Go to offset 3 to get cell count
    fseek(database_file, 3, SEEK_CUR);

    unsigned char buffer[2];
    fread(buffer, 1, 2, database_file);

    unsigned short cell_count = (buffer[1]) | (buffer[0] << 8);

    // Read next 2 bytes to get address of cell content area
    fread(buffer, 1, 2, database_file);
    unsigned short cell_content_addr = buffer[1] | (buffer[0] << 8);

    // Seek to address from start of the page
    if (cell_content_addr == 0) {
        fseek(database_file, 65536, SEEK_SET);
    } else {
        fseek(database_file, cell_content_addr, SEEK_SET);
    }


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
        fprintf(stderr, "Unknown command %s\n", command);
        return 1;
    }

    return 0;
}
