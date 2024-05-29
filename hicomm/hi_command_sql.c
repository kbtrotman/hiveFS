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

void execute_sql(char* sql_string) 
{
    PGresult *res = PQexec(sqldb.hive_conn, sql_string);
    if (PQresultStatus(res) != PGRES_COMMAND_OK) {
        hifs_err("SQL execution failed: %s\n", PQerrorMessage(sqldb.hive_conn));
        PQclear(res);
        //PQfinish(sql.hive_conn);
        return;
    }

    PQclear(res);
    //PQfinish(sql.hive_conn);
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

int register_hive_host(void) 
{
    //char *ins_sql = "INSERT INTO machines (host_name, host_ip, host_port, host_vers) VALUES ('%s', '%s', '%s', '%s');";
    //char *quoted_sql = (char *) malloc(strlen(ins_sql) + quoted_host.length() + quoted_ip.length() + quoted_port.length() + quoted_vers.length() + 1);
    //sprintf(quoted_sql, ins_sql, quoted_host.c_str(), quoted_ip.c_str(), quoted_port.c_str(), quoted_vers.c_str());
    //PGresult *res = PQexec(sql.hive_conn, quoted_sql);
    //if (PQresultStatus(res) != PGRES_COMMAND_OK) {
    //    sql.rows = 0;
        /* PROBLEM */
    //} else {
    //    sql.rows = atoi(PQcmdTuples(res));
    //    sql.last_res = res;
    //}

    //PQclear(res);
    return 0;
}
