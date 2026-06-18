#include "runquery.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <assert.h>

#include "datatype.h"
#include "schematab.h"
#include "utils.h"


#define freeMacro(var_name) free(var_name);

TableInfo getTableDetails(FILE* db_file, char* table_name, u16 page_size) {
    TableInfo tbl_info;
    tbl_info.is_table_found = 0;
    tbl_info.is_idx_found = 0;
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

        if (info.type == 2) {
            fprintf(stderr, "Rows other than table and index are not supported currently, skipping.\n");
            continue;
        }

        char* tbl_type = info.type == 1 ? "index" : "table";
        char* name = info.type == 1 ? info.index_name : info.table_name;
        fprintf(stderr, "debug_info: %s - %s found.\n", tbl_type, name);

        tbl_info.is_table_found = tbl_info.is_table_found || (info.type == 0); // once found, should be always true afterwards
        tbl_info.is_idx_found = tbl_info.is_idx_found || (info.type); // once found, should be always true
        s64 page_addr = page_size * (info.root_page - 1); // the first rootpage starts at address 0, usually the internal table

        if (info.type == 1) {
            tbl_info.idx_page_address = page_addr;
            tbl_info.create_idx_stm = info.sql_create_stm;
        } else {
            tbl_info.page_address = page_addr;
            tbl_info.create_sql_stm = info.sql_create_stm;
        }

        freeMacro(info.table_name);
        freeMacro(info.index_name);

        // dont break since we have to scan not only tables but also indexes if present.
    }

    freeMacro(entries.offsets);

    return tbl_info;
}

/*
 * Checks if index search is finished, *if* index search is relevant.
 * If index search is not relevant always returns false/0.
 */
u8 isIndexSearchOver(IndexSearchResult* idx_search) {
	if (!idx_search->is_index_relevant) {
		return 0;
	}

	return (idx_search->current_search_idx >= idx_search->row_ids.len);
}

int traverseTableBTree(FileState file_state, ParseQueryResult query, ColumnList col_data, IndexSearchResult* idx_search_results) {
	u8 buffer[4];

	// read page header
	// see if leaf or interior page
	fread(buffer, 1, 1, file_state.db_file);
	u8 is_interior = (buffer[0] == 0x05); // if is table b-tree interior page
	u8 is_leaf = (buffer[0] == 0x0D); // if is table b-tree leaf page

	// get all rows in the page
	fseek(file_state.db_file, 2, SEEK_CUR); // go to offset 3 (+2 after reading 1st byte) to get cell count of the b-tree page

	fread(buffer, 1, 2, file_state.db_file);
	u16 row_count = (buffer[1] | (buffer[0] << 8)); // to be used if count(*) property in sql query (for leaf page)

	// if (is_leaf) {
	//    	fprintf(stderr, "debug_info: row count %d\n", row_count);
	// } else if (is_interior) {
	// 	fprintf(stderr, "debug_info: cell count %d\n", row_count);
	// }

	if (is_leaf && query.where_tree == NULL) {
	    // fprintf(stderr, "traverse leaf\n");
		// Mini optimization: if count * is only property, early return
		char* property = query.select_col_len > 0 ? toLowerCase(query.select_cols[0]) : "";
		if (query.select_col_len == 1 && strcmp(property, "count(*)") == 0) {
		    printf("%d\n", row_count);
			freeMacro(property);
		    return 0;
		}
		freeMacro(property);
	}

	// following line is sufficient in case of leaf node (8byte header).
	fseek(file_state.db_file, 3, SEEK_CUR); // skip 3 bytes to reach end of header and reach cell ptr array

	// for interior node it is 12 byte and we need to store right most child pointer (last 4 bytes)
	s64 rightmost_child = 0;
	if (is_interior) {
        // fprintf(stderr, "traverse interior\n");
		fread(buffer, 1, 4, file_state.db_file);
		rightmost_child = (buffer[0] << 24) | (buffer[1] << 16) | (buffer[2] << 8) | buffer[3];
	}

	// get row offset in page for each row
	// Note: common for leaf and interior
	u16* row_offsets = malloc(row_count * sizeof(u16));
	for (int i = 0; i < row_count; i++) {
		// Read next 2 bytes to get address of cell content area
		fread(buffer, 1, 2, file_state.db_file);
		*(row_offsets + i) = buffer[1] | (buffer[0] << 8);
	}

	// OPTIMIZATION - for both leaf and interior nodes, check if last cell
	// is less than currently searching row_id. If so skip current cell
	if (isIndexSearchOver(idx_search_results)) {
		// OPTIMIZATION - search already over
		fprintf(stderr, "skipped, search done\n");
		return 0;

	} else if (idx_search_results->is_index_relevant) {
		s64 last_offset = file_state.page_address + row_offsets[row_count - 1];

		s64 cur_row_id = idx_search_results->row_ids.row_ids[idx_search_results->current_search_idx];

		ParseVarintResult varint;
		s64 last_rowid = -99999;

		if (is_interior) {
			// seek to last cell and skip first 4 bytes
			fseek(file_state.db_file, last_offset + 4, SEEK_SET);

			varint = parseVarint(file_state.db_file); // rowid
			last_rowid = varint.value;

			if(cur_row_id >= last_rowid) {
				// skip processing this node, nothing to find here
				// fprintf(stderr, "skip to right ptr\n");
				s64 page_address = file_state.page_size * (rightmost_child - 1); // last child pending - rightmost

				fseek(file_state.db_file, page_address, SEEK_SET);

				FileState new_state;
				new_state.db_file = file_state.db_file;
				new_state.page_size = file_state.page_size;
				new_state.page_address = page_address;

				int retcode = traverseTableBTree(new_state, query, col_data, idx_search_results);

				return retcode;
			}

		} else if (is_leaf) {
			// seek to last cell
			fseek(file_state.db_file, last_offset, SEEK_SET);

			varint = parseVarint(file_state.db_file); // ignore

			varint = parseVarint(file_state.db_file); // row-id
			last_rowid = varint.value;

			if (cur_row_id >= last_rowid) {
				// skip processing this node, nothing to find here
				// fprintf(stderr, "skipped\n");
				return 0;
			}
		}
	}

	if (is_interior) {
		// crawl children of B-Tree
		for (int i = 0; i < row_count; i++) {
			ParseVarintResult varint;

			// Seek to row offset from beginning of the page to reach row.
			s64 row_offset = file_state.page_address + row_offsets[i];
			fseek(file_state.db_file, row_offset, SEEK_SET);

			// Next 4 bytes are the left child of i-th key
			fread(buffer, 1, 4, file_state.db_file);
			s64 child = (buffer[0] << 24) | (buffer[1] << 16) | (buffer[2] << 8) | buffer[3]; // big-endian int page num

			// row id, required if index or row id where condition in place
			varint = parseVarint(file_state.db_file);
			s64 row_id = varint.value;

			if (idx_search_results->is_index_relevant && (idx_search_results->current_search_idx < idx_search_results->row_ids.len) ) {
				s64 row_id_to_search = idx_search_results->row_ids.row_ids[idx_search_results->current_search_idx];
				if (row_id_to_search >= row_id) {
					// Note: OPTIMIZATION - if row_id to search next is not less than key, need not go to left child pointer,
					// so skip to next key
					continue;
				}
			}

			// seek to left child and traverse the b-tree
			s64 left_child_pg_addr = file_state.page_size * (child - 1); // page num to page addr conversion

			fseek(file_state.db_file, left_child_pg_addr, SEEK_SET);

			FileState new_state;
			new_state.db_file = file_state.db_file;
			new_state.page_size = file_state.page_size;
			new_state.page_address = left_child_pg_addr;

			int retcode = traverseTableBTree(new_state, query, col_data, idx_search_results);

			if (retcode) {
				// err code
				return retcode;
			}

			if (isIndexSearchOver(idx_search_results)) {
				return 0;
			}
		}

		s64 page_address = file_state.page_size * (rightmost_child - 1); // last child pending - rightmost

		fseek(file_state.db_file, page_address, SEEK_SET);

		FileState new_state;
		new_state.db_file = file_state.db_file;
		new_state.page_size = file_state.page_size;
		new_state.page_address = page_address;

		int retcode = traverseTableBTree(new_state, query, col_data, idx_search_results);
		return retcode; // no need to check index search over since here the traver
	}

	if (!is_leaf) {
		// expect this to be true if not interior (index pages not supported)
		fprintf(stderr, "Unexpected: Page is neither a B-Tree leaf nor interior\n");
		return 1;
	}

	// leaf page case: parse payload and print rows if where satisfied

	// go over each row
	for(int i = 0; i < row_count; i++) {
		ParseVarintResult varint;

		// Seek to row offset from beginning of the page to reach row.
		s64 row_offset = file_state.page_address + row_offsets[i];
		fseek(file_state.db_file, row_offset, SEEK_SET);

		varint = parseVarint(file_state.db_file); // Size of record - can be ignored

		varint = parseVarint(file_state.db_file); // row id - required as value, if primary key
		s64 row_id = varint.value;

		if (idx_search_results->is_index_relevant && (idx_search_results->current_search_idx < idx_search_results->row_ids.len) ) {
			s64 row_id_to_search = idx_search_results->row_ids.row_ids[idx_search_results->current_search_idx];
			if (row_id_to_search != row_id) {
				// Note: OPTIMIZATION - for leaf node if row_id does not match, skip the row
				continue;
			}
			// in case it is a match, update current idx ptr to search the next rowid
			// Also, PRINT this row
			idx_search_results->current_search_idx++;
		}

		varint = parseVarint(file_state.db_file); //record header size
		u64 record_hdr_size = varint.value - 1; // subtracting size of itself

		u64 col_len = col_data.num_columns;
		u64* col_sizes = malloc(col_len * sizeof(u64)); // Assumption less than 100 columns

		for (int j = 0; record_hdr_size > 0; j++) {
			varint = parseVarint(file_state.db_file);

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
			    fread(text, 1, col_size, file_state.db_file);
			    text[col_size] = '\0';

			    col_data.columns[j].value = text;
			} else if (strcmp(col_type, "int") == 0 || strcmp(col_type, "integer") == 0) {
			    s64 int_value = 0;
			    if (col_data.columns[j].name == col_data.primary_key_colname) {
					// primary key case, value is not valid
					int_value = row_id;
				} else {
                    u8* bytes = malloc(col_size);
                    fread(bytes, 1, col_size, file_state.db_file);

                    int_value = parseSqlInt(bytes, col_size);
                    free(bytes);
				}

			    col_data.columns[j].value = malloc(100 * sizeof(char));
			    sprintf(col_data.columns[j].value, "%lld", int_value);
			} else {
			    fseek(file_state.db_file, col_size, SEEK_CUR);
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
		        char* col_value = NULL;
		        for (int k = 0; k < col_len; k++) {
		            if (strcmp(col_data.columns[k].name, query.select_cols[j]) == 0) {
		                col_value = col_data.columns[k].value;
		            }
		        }
				if (col_value != NULL) {
					printf("%s", col_value);
				}
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

int traverseIndexBTree(FileState file_state, IndexSearchParams search_params, RowIds* result) {
    u8 buffer[4];

	// read page header
	// see if leaf or interior page
	fread(buffer, 1, 1, file_state.db_file);
	u8 is_interior = (buffer[0] == 0x02); // if is table b-tree interior page
	u8 is_leaf = (buffer[0] == 0x0A); // if is table b-tree leaf page

	// get all rows in the page
	fseek(file_state.db_file, 2, SEEK_CUR); // go to offset 3 (+2 after reading 1st byte) to get cell count of the b-tree page

	fread(buffer, 1, 2, file_state.db_file);
	u16 cell_count = (buffer[1] | (buffer[0] << 8)); // to be used if count(*) property in sql query (for leaf page)

	// following line is sufficient in case of leaf node (8byte header).
	fseek(file_state.db_file, 3, SEEK_CUR); // skip 3 bytes to reach end of header and reach cell ptr array

	// for interior node it is 12 byte and we need to store right most child pointer (last 4 bytes)
	s64 rightmost_child = 0;
	if (is_interior) {
        // fprintf(stderr, "traverse idx interior\n");
		fread(buffer, 1, 4, file_state.db_file);
		rightmost_child = (buffer[0] << 24) | (buffer[1] << 16) | (buffer[2] << 8) | buffer[3];
	} else {
		// fprintf(stderr, "traverse idx leaf\n");
	}

	// get row offset in page for each row
	// Note: common for leaf and interior
	u16* cell_offsets = malloc(cell_count * sizeof(u16));
	for (int i = 0; i < cell_count; i++) {
		// Read next 2 bytes to get address of cell content area
		fread(buffer, 1, 2, file_state.db_file);
		*(cell_offsets + i) = buffer[1] | (buffer[0] << 8);
	}

	// crawl children of B-Tree: all keys in both interior and leaf nodes
	for (int i = 0; i < cell_count; i++) {
		ParseVarintResult varint;

		// Seek to row offset from beginning of the page to reach row.
		s64 row_offset = file_state.page_address + cell_offsets[i];
		fseek(file_state.db_file, row_offset, SEEK_SET);

		s64 left_child_pg_num = 0;
		s64 left_child_page_address = 0;
		if (is_interior) {
            // Next 4 bytes are the left child of i-th key
            fread(buffer, 1, 4, file_state.db_file);
            left_child_pg_num = (buffer[0] << 24) | (buffer[1] << 16) | (buffer[2] << 8) | buffer[3]; // big-endian int page num

            left_child_page_address = file_state.page_size * (left_child_pg_num - 1); // page num to page addr conversion
		}

		// payload of interior and leaf both contain same structure: key and rowid, where key is all the columns used to create index
		// key of interior can be used to traverse to the appropriate child

		varint = parseVarint(file_state.db_file); // Size of cell in bytes - can be ignored

		varint = parseVarint(file_state.db_file); //record header size
		u64 record_hdr_size = varint.value - 1; // subtracting size of itself

		u64 col_len = search_params.idx_cols.cols_len; // here this col_len is wrong for payload since there is also row_id after col_val
		u64* col_sizes = malloc(col_len * sizeof(u64)); // Assumption less than 100 columns

		u8 payload_len = 0;
		for (int j = 0; record_hdr_size > 0; j++) {
			varint = parseVarint(file_state.db_file);

			col_sizes[j] = getRecordSerialTypeSize(varint.value);
			record_hdr_size -= varint.byte_span;
			payload_len++;
		}

		// Note: currently is_match only works this way is because there is only support for a single column
		// if multiple columns need to match, is_match needs to be 1 initially and the subsequent check needs to be && every time
		// if a single column check fails, is_match becomes 0

		u8 is_match = 0; // signals if the key is equal to the where condition value, thus if row id needs to be stored
		s64 row_id = 0;
		char* key_val_string = NULL;
		s64 key_val_int = 0;

		// go over all columns that contribute to the key in both leaf and interior
		for (int j = 0; j < payload_len; j++) { // col_len would only cover the column values and not row id
			if (j == payload_len - 1) {
				// row id case
				u64 col_size = col_sizes[j];
				u8* bytes = malloc(col_size);
				fread(bytes, 1, col_size, file_state.db_file);

				row_id = parseSqlInt(bytes, col_size);
				free(bytes);

				if (is_match) {
					// append row_id
					result->row_ids[result->len] = row_id;
					result->len++;
				}

				break; // last column
			}

			// go over each column in the current row, order as stored in .db file
			u64 col_size = col_sizes[j];

			if (search_params.where_col_mode == 0) { // string
                char* text = malloc((col_size + 1) * sizeof(char));
                fread(text, 1, col_size, file_state.db_file);
                text[col_size] = '\0';

                is_match = (strcmp(text, search_params.where_col_value) == 0);
                key_val_string = text;
			} else if (search_params.where_col_mode == 1) { // int
                s64 int_value = 0;
                u8* bytes = malloc(col_size);
                fread(bytes, 1, col_size, file_state.db_file);

                int_value = parseSqlInt(bytes, col_size);
                free(bytes);

                char* idx_col_val = malloc(100 * sizeof(char));
                sprintf(idx_col_val, "%lld", int_value);

                is_match = strcmp(idx_col_val, search_params.where_col_value);
                key_val_int = int_value;
			} else {
			    fprintf(stderr, "Columns other than int and str not supported in indexes.\n");
			}
		}

		if (is_interior) {
			// TODO: traverse to left child ideally if value of key is > value in where condition
			// if value of key is greater, continue to next key for their left child or rightmost child if this is last key
			//
			// but for equal keys there can be multiple entries, where do they exist? left or right, have to search all in that case
			// the file format spec does not specify for equal case in index b-trees

			if (search_params.where_col_mode == 0 && search_params.where_col_value >= key_val_string) {
				// Optimization - skip left child traversal since the value to find is not in left
				continue;
			}
			if (search_params.where_col_mode == 1 && search_params.where_col_value >= key_val_string) {
				// Optimization - skip left child traversal since the value to find is not in left
				// continue;
				// int check TODO
			}

			FileState fs;
			fs.db_file = file_state.db_file;
			fs.page_size = file_state.page_size;
			fs.page_address = left_child_page_address;

			fseek(file_state.db_file, left_child_page_address, SEEK_SET);

			int retcode = traverseIndexBTree(fs, search_params, result);
			if (retcode) {
				return retcode;
			}
			continue;
		}
	}

	if (is_interior) {
		// search finally rightmost child
		FileState fs;
		fs.db_file = file_state.db_file;
		fs.page_size = file_state.page_size;
		s64 right_page_address = file_state.page_size * (rightmost_child - 1); // page num to page addr conversion
		fs.page_address = right_page_address;

		fseek(file_state.db_file, right_page_address, SEEK_SET);

		int retcode = traverseIndexBTree(fs, search_params, result);
		if (retcode) {
			return retcode;
		}
	}

    return 0;
}

u8 isWhereSatisfied(WhereTree* where_tree, ColumnList cols) {
    // since where condition could be on any column: need not be in select and could be last row
    // we have to go through all the columns
    // TODO possible optimization => short circuit or/and of where condition
    if (where_tree == NULL) {
        return 1;
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

void merge(s64* arr, u32 left, u32 mid, u32 right) {
    int i, j, k;
    int n1 = mid - left + 1;
    int n2 = right - mid;

    // Create temporary arrays
    int leftArr[50000], rightArr[50000];

    // Copy data to temporary arrays
    for (i = 0; i < n1; i++)
        leftArr[i] = arr[left + i];
    for (j = 0; j < n2; j++)
        rightArr[j] = arr[mid + 1 + j];

    // Merge the temporary arrays back into arr[left..right]
    i = 0;
    j = 0;
    k = left;
    while (i < n1 && j < n2) {
        if (leftArr[i] <= rightArr[j]) {
            arr[k] = leftArr[i];
            i++;
        }
        else {
            arr[k] = rightArr[j];
            j++;
        }
        k++;
    }

    // Copy the remaining elements of leftArr[], if any
    while (i < n1) {
        arr[k] = leftArr[i];
        i++;
        k++;
    }

    // Copy the remaining elements of rightArr[], if any
    while (j < n2) {
        arr[k] = rightArr[j];
        j++;
        k++;
    }
}

// The subarray to be sorted is in the index range [left-right]
void mergeSort(s64* arr, u32 left, u32 right) {
    if (left < right) {

        // Calculate the midpoint
        int mid = left + (right - left) / 2;

        // Sort first and second halves
        mergeSort(arr, left, mid);
        mergeSort(arr, mid + 1, right);

        // Merge the sorted halves
        merge(arr, left, mid, right);
    }
}
