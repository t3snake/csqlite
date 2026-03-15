#include <string.h>
#include <stdio.h>
#include <stdlib.h>

int main(int argc, char *argv[]) {
    if (argc != 3) {
        fprintf(stderr, "Usage: ./your_program.sh <database path> <command>\n");
        return 1;
    }

    const char *database_file_path = argv[1];
    const char *command = argv[2];

    if (strcmp(command, ".dbinfo") == 0) {
        FILE *database_file = fopen(database_file_path, "rb");
        if (!database_file) {
            fprintf(stderr, "Failed to open the database file\n");
            return 1;
        }

        fprintf(stderr, "Logs from your program will appear here!\n");

        // Skip the first 16 bytes of the header
        fseek(database_file, 16, SEEK_SET);

        // Read next 2 bytes to get page size
        unsigned char buffer[2];
        fread(buffer, 1, 2, database_file);
        unsigned short page_size = (buffer[1] | (buffer[0] << 8)); // big endian

        printf("database page size: %u\n", page_size);

        // Skip next 84 bytes ie. DB Header (100 total along with already skipped 16 bytes)
        fseek(database_file, 84, SEEK_CUR);

        // Next 8 to 12 bytes are b-tree page headers
        // At offset 3, the 2 byte integer gives number of cells on the page
        // Since this b-tree is the internal sqlite_schema table, the number of rows is the number of tables (if only tables in DB)
        fseek(database_file, 3, SEEK_CUR);
        fread(buffer, 1, 2, database_file);

        unsigned short cell_count = (buffer[1] | (buffer[0] << 8));

        printf("number of tables: %u\n", cell_count);

        fclose(database_file);
    } else {
        fprintf(stderr, "Unknown command %s\n", command);
        return 1;
    }

    return 0;
}
