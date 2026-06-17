#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>

#include "sql/datatype.h"
#include "sql/utils.h"
#include "sql/schematab.h"
#include "sql/parser.h"
#include "sql/runquery.h"


#define freeMacro(var_name) free(var_name);

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

        SchemaInfo info = getSchemaRowInfo(database_file);
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

int runSelectQuery(const char* db_file_path, const char* query) {
    fprintf(stderr, "debug_info: runSelectQuery called\n");
    FILE* database_file = fopen(db_file_path, "rb");
    if (!database_file) {
        fprintf(stderr, "Failed to open the database file: %s\n", db_file_path);
        return 1;
    }

    // Get page size
    fseek(database_file, 16, SEEK_SET);

    u8 buffer[2];
    fread(buffer, 1, 2, database_file);
    u16 page_size = (buffer[1] | (buffer[0] << 8));

    ParseQueryResult query_res = parseQuery(query);

    TableInfo tbl_info = getTableDetails(database_file, query_res.table, page_size);

    if (!tbl_info.is_table_found) {
        fprintf(stderr, "table %s not found in the database %s.\n", query_res.table, db_file_path);
        return 1;
    }

    fprintf(stderr, "debug_info: calling parseCreateTblStmt\n");
    fprintf(stderr, "debug_info: %s\n", tbl_info.create_sql_stm);

    // parse sql create statement to get order of how columns are stored
    // only need to be called once, values will be updated in col_data for each row
    ColumnList col_data = parseCreateTblStmt(tbl_info.create_sql_stm);
    fprintf(stderr, "debug_info: successfully parsed create statement\n");


    IndexSearchResult idx_search_results;
    idx_search_results.is_index_relevant = 0;
    idx_search_results.current_search_idx = 0;
    idx_search_results.row_ids.row_ids = NULL;
    idx_search_results.row_ids.len = 0;

    // if there is no where condition, skip index processing
	if (tbl_info.is_idx_found && query_res.where_tree != NULL) {
		idx_search_results.is_index_relevant = 1;
		fprintf(stderr, "debug_info: calling parseCreateIdxStmt\n");

		Columns idx_cols = parseCreateIdxStmt(tbl_info.create_idx_stm);

		// Note: currently there is only one condition supported, multiple conditions would require more complex checks.
		char* where_col_name = query_res.where_tree->condition.l_col_name;
		u8 is_match = 0;

		fprintf(stderr, "debug_info: successfully parsed create index stmt\n");
		for (int coli = 0; coli < idx_cols.cols_len; coli++) {
		    fprintf(stderr, "%s, ", idx_cols.columns[coli]);

		    if (strcmp(idx_cols.columns[coli], where_col_name) == 0) {
		        is_match = 1;
		    }
		}
		fprintf(stderr, "\n");

		// TODO parse Index B-Tree if where condition contains column name in index
		if (is_match) {
			char* col_value = query_res.where_tree->condition.r_value;
			u8 col_val_mode = query_res.where_tree->condition.r_value_mode; // 0 - string, 1 - int, 2 - another column

			FileState file_state;
			file_state.db_file = database_file;
			file_state.page_size = page_size;
			file_state.page_address = tbl_info.idx_page_address;

			IndexSearchParams search_params;
			search_params.idx_cols = idx_cols;
			search_params.where_col_mode = col_val_mode;
			search_params.where_col_value = col_value;

			fseek(database_file, tbl_info.idx_page_address, SEEK_SET);

			RowIds row_ids;
			row_ids.row_ids = (s64*) malloc(100000 * sizeof(s64));
    		row_ids.len = 0;

			int retcode = traverseIndexBTree(file_state, search_params, &row_ids);
			if (retcode) {
				fprintf(stderr, "Unknown Error while traversing index B-Tree");
				return retcode;
			}

			// bubble sort?
			mergeSort(row_ids.row_ids, 0, row_ids.len - 1);

			fprintf(stderr, "debug info: \n");
			for (int po = 0; po < row_ids.len; po++) {
				fprintf(stderr, "%lld\n", row_ids.row_ids[po]);
			}

			idx_search_results.row_ids = row_ids;
		}
	}

    FileState file_state;
    file_state.db_file = database_file;
    file_state.page_address = tbl_info.page_address;
    file_state.page_size = page_size;

    fseek(database_file, tbl_info.page_address, SEEK_SET);

    // crawl b-tree
    int retcode = traverseTableBTree(file_state, query_res, col_data, &idx_search_results);

    fprintf(stderr, "traversal done. retcode: %d", retcode);

    for (int i = 0; i < col_data.num_columns; i++) {
        freeMacro(col_data.columns[i].name);
        freeMacro(col_data.columns[i].type);
        freeMacro(col_data.columns[i].value);
    }
    freeMacro(col_data.columns);

    freeMacro(tbl_info.create_sql_stm);

    freeMacro(query_res.select_cols);
    freeMacro(query_res.sql_command);
    freeMacro(query_res.table);

    // Note: only single node since current implementation only supports one condition.
    if (query_res.where_tree != NULL) {
        freeMacro(query_res.where_tree->left);
        freeMacro(query_res.where_tree->right);
        freeMacro(query_res.where_tree->condition.comparator);
        freeMacro(query_res.where_tree->condition.l_col_name);
        freeMacro(query_res.where_tree->condition.r_value);
        freeMacro(query_res.where_tree);
    }

    fclose(database_file);
    return retcode;
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
