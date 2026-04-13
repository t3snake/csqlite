#include "datatype.h"

s64 parseSqlInt(u8* big_end_bytes, u8 num_bytes) {
    s64 result = 0;
    for (int i = 0; i < num_bytes; i++) {
        result <<= 8;
        result |= big_end_bytes[i];
    }
    return result;
}

ParseVarintResult parseVarint(FILE* db_file) {
    // read 1 to 9 bytes
    s64 result = 0;

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


u64 getRecordSerialTypeSize(u64 serial_type) {
    switch (serial_type) {
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

    if (serial_type % 2 == 0) {
        return (serial_type - 12) / 2;
    } else {
        return (serial_type - 13) / 2;
    }
}
