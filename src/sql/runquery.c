#include "runquery.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <assert.h>

#include "datatype.h"
#include "schematab.h"
#include "utils.h"


#define freeMacro(var_name) if (var_name != NULL) { free(var_name); }

TableInfo seekToTable(FILE* db_file, char* table_name, u16 page_size) {
    TableInfo tbl_info;
    tbl_info.is_table_found = 0;
    tbl_info.create_sql_stm = NULL;
    tbl_info.page_address = -1;

    // Find table in schema table
    fprintf(stderr, "debug_info: getSchemaTabRowAddr called\n");
    SqliteSchemaEntries entries = getSchemaTabRowAddr(db_file);

    for (int i = 0; i < entries.count; i++) {
        u16 offset = *(entries.offsets + i);

        if (offset == 0) {
            fseek(db_file, 65536, SEEK_SET);
        } else {
            fseek(db_file, offset, SEEK_SET);
        }

        // check if entry is the table from query
        fprintf(stderr, "debug_info: getSchemaRowInfo called\n");
        SchemaInfo info = getSchemaRowInfo(db_file);

        char* lc_schema_tbl_name = toLowerCase(info.table_name);
        char* lc_query_tbl_name = toLowerCase(table_name);

        u8 cmp_res = strcmp(lc_schema_tbl_name, lc_query_tbl_name) != 0;

        freeMacro(lc_query_tbl_name);
        freeMacro(lc_schema_tbl_name);

        if (cmp_res) {
            // fprintf(stderr, "debug_info: freeing info.table_name %s\n", info.table_name);
            freeMacro(info.table_name);
            freeMacro(info.sql_create_stm);
            continue;
        }

        fprintf(stderr, "debug_info: table name found.\n");

        tbl_info.is_table_found = 1;
        tbl_info.page_address = page_size * (info.root_page - 1); // the first rootpage starts at address 0, usually the internal table
        tbl_info.create_sql_stm = info.sql_create_stm;

        fseek(db_file, tbl_info.page_address, SEEK_SET); // Note: should this function do it, or the caller?

        freeMacro(info.table_name);
        freeMacro(entries.offsets);

        break;
    }

    return tbl_info;
}

int traverseBTree(FILE* db_file, ParseQueryResult query, s64 cur_pg_addr, ColumnList col_data, u16 page_size) {
	u8 buffer[4];

	// read page header
	// see if leaf or interior page
	fread(buffer, 1, 1, db_file);
	u8 is_interior = (buffer[0] == 0x05); // if is table b-tree interior page
	u8 is_leaf = (buffer[0] == 0x0D); // if is table b-tree leaf page

	// get all rows in the page
	fseek(db_file, 2, SEEK_CUR); // go to offset 3 (+2 after reading 1st byte) to get cell count of the b-tree page

	fread(buffer, 1, 2, db_file);
	u16 row_count = (buffer[1] | (buffer[0] << 8)); // to be used if count(*) property in sql query (for leaf page)
	if (is_leaf) {
	   	fprintf(stderr, "debug_info: row count %d\n", row_count);
	} else if (is_interior) {
		fprintf(stderr, "debug_info: cell count %d\n", row_count);
	}

	if (is_leaf) {
		// Mini optimization: if count * is only property, early return
		char* property = query.select_col_len > 0 ? toLowerCase(query.select_cols[0]) : "";
		if (query.select_col_len == 1 && strcmp(property, "count(*)") == 0) {
		    printf("%d\n", row_count);
		    return 0;
		}
		freeMacro(property);
	}

	// following line is sufficient in case of leaf node (8byte header).
	fseek(db_file, 3, SEEK_CUR); // skip 3 bytes to reach end of header and reach cell ptr array

	// for interior node it is 12 byte and we need to store right most child pointer (last 4 bytes)
	s64 rightmost_child = 0;
	if (is_interior) {
		fread(buffer, 1, 4, db_file);
		rightmost_child = (buffer[0] << 24) | (buffer[1] << 16) | (buffer[2] << 8) | buffer[3];
	}

	// get row offset in page for each row
	// Note: common for leaf and interior
	u16* row_offsets = malloc(row_count * sizeof(u16));
	for (int i = 0; i < row_count; i++) {
		// Read next 2 bytes to get address of cell content area
		fread(buffer, 1, 2, db_file);
		*(row_offsets + i) = buffer[1] | (buffer[0] << 8);
	}

	if (is_interior) {
		// TODO, crawl children of B-Tree
		for (int i = 0; i < row_count; i++) {
			ParseVarintResult varint;

			// Seek to row offset from beginning of the page to reach row.
			s64 row_offset = cur_pg_addr + row_offsets[i];
			fseek(db_file, row_offset, SEEK_SET);

			// Next 4 bytes are the left child of i-th key
			fread(buffer, 1, 4, db_file);
			s64 child = (buffer[0] << 24) | (buffer[1] << 16) | (buffer[2] << 8) | buffer[3]; // big-endian int page num

			// TODO parseVarint if index needed

			s64 page_address = page_size * (child - 1); // page num to page addr conversion

			fseek(db_file, page_address, SEEK_SET);

			int retcode = traverseBTree(db_file, query, page_address, col_data, page_size);

			if (retcode) {
				// err code
				return retcode;
			}
		}

		s64 page_address = page_size * (rightmost_child - 1); // last child pending - rightmost

		fseek(db_file, page_address, SEEK_SET);

		int retcode = traverseBTree(db_file, query, page_address, col_data, page_size);

		return retcode;
	}

	if (!is_leaf) {
		// expect this to be true if not interior (index pages not supported)
		fprintf(stderr, "Page is neither a B-Tree leaf nor interior\n");
		return 1;
	}

	// leaf page case: parse payload and print rows if where satisfied

	// go over each row
	for(int i = 0; i < row_count; i++) {
		ParseVarintResult varint;

		// Seek to row offset from beginning of the page to reach row.
		s64 row_offset = cur_pg_addr + row_offsets[i];
		fseek(db_file, row_offset, SEEK_SET);

		varint = parseVarint(db_file); // Size of record
		u64 record_size = varint.value;

		varint = parseVarint(db_file); // row id - required as value, if primary key
		s64 row_id = varint.value;

		varint = parseVarint(db_file); //record header size
		u64 record_hdr_size = varint.value - 1; // subtracting size of itself

		u64 col_len = col_data.num_columns;
		u64* col_sizes = malloc(col_len * sizeof(u64)); // Assumption less than 100 columns

		for (int j = 0; record_hdr_size > 0; j++) {
			varint = parseVarint(db_file);

			col_sizes[j] = getRecordSerialTypeSize(varint.value);
			record_hdr_size -= varint.byte_span;
		}

		// store all column values of the row
		for (int j = 0; j < col_len; j++) {
			// go over each column in the current row, order as stored in .db file

			// Note: we already have col name and type in struct when parsing create col,
			// we update val in same struct, to avoid extra memory
			u64 col_size = col_sizes[j];
			char* col_type = col_data.columns[j].type;
			// fprintf(stderr, "debug_info: column %s: %s\n", col_name, col_type);

			if (strcmp(col_type, "text") == 0) {
			    char* text = malloc((col_size + 1) * sizeof(char));
			    fread(text, 1, col_size, db_file);
			    text[col_size] = '\0';

			    col_data.columns[j].value = text;
			} else if (strcmp(col_type, "int") == 0 || strcmp(col_type, "integer") == 0) {
			    s64 int_value = 0;
			    if (col_data.columns[j].name == col_data.primary_key_colname) {
					// primary key case, value is not valid
					int_value = row_id;
				} else {
                    u8* bytes = malloc(col_size);
                    fread(bytes, 1, col_size, db_file);

                    int_value = parseSqlInt(bytes, col_size);
                    free(bytes);
				}

			    col_data.columns[j].value = malloc(100 * sizeof(char));
			    sprintf(col_data.columns[j].value, "%lld", int_value);
			} else {
			    fseek(db_file, col_size, SEEK_CUR);
			    col_data.columns[j].value = NULL;
			}
		}

		// check where condition to see if the row should be skipped
		u8 is_where_satisfied = isWhereSatisfied(query.where_tree, col_data);

		if (!is_where_satisfied) {
		    // go to next row
		    continue;
		}

		// print results if row to be printed
		for (int j = 0; j < query.select_col_len; j++) {
		    char* lc_col_name = toLowerCase(query.select_cols[j]);
		    if (strcmp(lc_col_name, "count(*)") == 0) {
		        printf("%d", row_count);
		    } else {
		        char* col_value;
		        for (int k = 0; k < col_len; k++) {
		            if (strcmp(col_data.columns[k].name, query.select_cols[j]) == 0) {
		                col_value = col_data.columns[k].value;
		            }
		        }
		        printf("%s", col_value);
		    }

		    if (j == query.select_col_len - 1) {
		        printf("\n");
		    } else {
		        printf("|");
		    }

		    freeMacro(lc_col_name);
		}
    }

	return 0;
}

u8 isWhereSatisfied(WhereTree* where_tree, ColumnList cols) {
    // since where condition could be on any column: need not be in select and could be last row
    // we have to go through all the columns
    // TODO possible optimization => short circuit or/and of where condition
    if (where_tree == NULL) {
        return 0;
    }

    u8 is_where_satisfied = 0;
    // check if row satisfies where condition
    // TODO where tree handling, currently assumes only one condition present at root
    char* l_col_name = where_tree->condition.l_col_name;
    char* r_col_name = NULL; // valid only if right side value represents column

    char* literal_value = NULL; // valid only if right side value is int or string (not column)

    u8 is_r_col = (where_tree->condition.r_value_mode == 2);
    if (is_r_col) {
        r_col_name = where_tree->condition.r_value;
    } else {
        literal_value = where_tree->condition.r_value;
    }

    char* l_col_value = NULL;
    char* r_col_value = NULL; // only valid if the right side value is a column and not literal

    // get l_col and r_col (if relevant) values
    for (int k = 0; k < cols.num_columns; k++) {
        if (strcmp(l_col_name, cols.columns[k].name) == 0) {
            l_col_value = cols.columns[k].value;
            if (!is_r_col) {
                // only break if there is r_value is not a column, else continue search for r_col
                break;
            }
        }

        if (is_r_col && strcmp(r_col_name, cols.columns[k].name) == 0) {
            r_col_value = cols.columns[k].value;
        }
    }

    // assert(l_col_value != NULL); // cant assert null if value is null

    switch (where_tree->condition.r_value_mode) {
        case 0:
            // literal string
            assert(literal_value != NULL);
            is_where_satisfied = (strcmp(l_col_value, literal_value) == 0);
            break;

        case 1:
            assert(literal_value != NULL);
            is_where_satisfied = (strcmp(l_col_value, literal_value) == 0);
            break;

        case 2:
            assert(is_r_col && r_col_value != NULL);
            is_where_satisfied = (strcmp(l_col_value, r_col_value) == 0);
            break;

        default:
            // invalid r_value
            fprintf(stderr, "Invalid r_value_mode in WhereCondition %d", where_tree->condition.r_value_mode);
    }

    return is_where_satisfied;
}
