/**
 * HiveFS
 *
 * Hive Mind Filesystem
 * By K. B. Trotman
 * License: GNU GPL as of 2023
 *
 */

#include "hi_command.h"
#include "sql/hi_command_sql.h"

struct PSQL sqldb;


void init_hive_link(void) 
{

    if (!sqldb.sql_init) {
        sqldb.hive_conn = PQconnectdb(DBSTRING);
        if (PQstatus(sqldb.hive_conn) == CONNECTION_BAD) {
            hifs_err("hi_command: Connection to remote hive failed: %s\n", PQerrorMessage(sqldb.hive_conn));
            PQfinish(sqldb.hive_conn);
            return;
        } else {
            int sql_v;
            sql_v = get_hive_vers();
            hifs_notice("hi_command: Connected to remote hive version: %d\n", sql_v);
            register_hive_host(); 
            sqldb.sql_init = true;
        }
    }
    return;
}

void close_hive_link (void) 
{
    PQclear(sqldb.last_qury);
    PQclear(sqldb.last_ins);
    PQfinish(sqldb.hive_conn);  //Close the DB connection gracefully before we shutdown.
}

int get_hive_vers() 
{
        int hi_psql_vers = PQserverVersion(sqldb.hive_conn);
        return hi_psql_vers;
}

int execute_sql(char* sql_string) 
{
    PGresult *res = PQexec(sqldb.hive_conn, sql_string);
    if (PQresultStatus(res) != PGRES_COMMAND_OK) {
        hifs_err("SQL execution failed: %s\n", PQerrorMessage(sqldb.hive_conn));
        PQclear(res);
        //PQfinish(sql.hive_conn);
        return -1;
    }
    sqldb.last_qury = res;
    sqldb.rows = atoi(PQcmdTuples(res));
    return 0;
}

int save_binary_data(char *data_block, char *hash)
{
    //const char *paramValues[1];
    //int paramLengths[1];
    //int paramFormats[1];
    // Assign the binary block to the parameter
    //paramValues[0] = data_block;
    //paramLengths[0] = sizeof(data_block);
    //paramFormats[0] = 1; // 1 means binary format
    //PGresult *res;
    //int rows = 0;
    //char *ins_sql;

    //char *quoted_sql = (char *) malloc(strlen(ins_sql) + quoted_hash.length() + quoted_count.length() + 1);
    //res = PQexecParams(sql.hive_conn, quoted_sql, 1, NULL, paramValues, paramLengths, paramFormats, 0);

    //if (PQresultStatus(res) != PGRES_COMMAND_OK) {
    //    sql.rows = 0;
        /* PROBLEM */
    //} else {
    //    sql.rows = atoi(PQcmdTuples(res));
    //    sql.last_res = res;
    //}


    //    PQclear(res);
    return 0;
}


int get_hive_host_data(char *machine_id) 
{
    char sql_query[512];
    int res = 0;
    MACHINE_GETINFO(machine_id, sql_query);
    res = execute_sql(sql_query); 
    return res;
}

int register_hive_host(void) 
{
    char *hive_mach_id;
    long hive_host_id;
    int nFields;
    hive_mach_id = hifs_get_machine_id();
    hive_host_id = hifs_get_host_id();

    hifs_info("Hive machine ID is [%s] and host ID is [%ld]\n", hive_mach_id, hive_host_id);

    get_hive_host_data(hive_mach_id);
    if ( sqldb.rows > 0 ) {
        // Populate Host Data
        /* first, print out the attribute names */
        nFields = PQnfields(sqldb.last_qury);
        for (int i = 0; i < nFields; i++)
            printf("%-15s", PQfname(sqldb.last_qury, i));
        printf("\n\n");

        /* next, print out the rows */
        for (int i = 0; i < PQntuples(sqldb.last_qury); i++)
        {
            for (int j = 0; j < nFields; j++)
                printf("%-15s", PQgetvalue(sqldb.last_qury, i, j));
            printf("\n");
        }
    } else {
        char c;
        do{
            printf("This host does not exist in the hive. Filesystems cannot be mounted without a hive connection.\nDo you want to register this host to the hive now to obtain filesystems? (y/n)");
            scanf(" %c",&c); c = tolower(c);
        }while(c != 'n' && c != 'y');
        if (c == 'n') return 0;
        hifs_info("Registering machine to Hive.\n");
    }
    printf("\n\n");
    
    return 0;
}
