#include <string.h>
#include <stdio.h>
#include <stdlib.h>

typedef unsigned short u8;

typedef struct _ParseVarintResult {
    long value;
    int byte_span;
} ParseVarintResult;

ParseVarintResult parseVarint(FILE* db_file) {
    // read 1 to 9 bytes
    long result = 0;
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

        // Add Least significant 7 bits to the overall value
        result <<= 7; // move result 7 bytes to append the next 7 bytes
        // Note: 0 bit shifted is still 0 so initial case is fine
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

int runDbInfoCmd(const char* db_file_path) {
    FILE* database_file = fopen(db_file_path, "rb");
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
    u8 page_size = (buffer[1] | (buffer[0] << 8)); // big endian

    printf("database page size: %u\n", page_size);

    // Skip next 82 bytes ie. DB Header (100 total along with already skipped 18 bytes)
    fseek(database_file, 82, SEEK_CUR);

    // Next 8 to 12 bytes are b-tree page headers
    // At offset 3, the 2 byte integer gives number of cells on the page
    // Since this b-tree is the internal sqlite_schema table, the number of rows is the number of tables (if only tables in DB)
    fseek(database_file, 3, SEEK_CUR);

    fread(buffer, 1, 2, database_file);

    u8 cell_count = (buffer[1] | (buffer[0] << 8));

    printf("number of tables: %u\n", cell_count);

    fclose(database_file);
    return 0;
}

int runTablesCmd(const char* db_file_path) {
    FILE* database_file = fopen(db_file_path, "rb");
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

    u8 cell_count = (buffer[1]) | (buffer[0] << 8);

    // Read next 2 bytes to get address of cell content area
    fread(buffer, 1, 2, database_file);
    u8 cell_content_addr = buffer[1] | (buffer[0] << 8);

    // Seek to address from start of the page
    if (cell_content_addr == 0) {
        fseek(database_file, 65536, SEEK_SET);
    } else {
        fseek(database_file, cell_content_addr, SEEK_SET);
    }

    // read varint to get record size
    // read all column size data/2complements
    // parse record body


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
