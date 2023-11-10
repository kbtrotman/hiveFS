
#include <pq.h>

void execute_sql(char* sql) 
{
    PGconn *conn = PQconnectdb();
    if (PQstatus(conn) == CONNECTION_BAD) {
        fprintf(stderr, "Connection to database failed: %s\n", PQerrorMessage(conn));
        PQfinish(conn);
        return;
    }

    PGresult *res = PQexec(conn, sql);
    if (PQresultStatus(res) != PGRES_COMMAND_OK) {
        fprintf(stderr, "SQL execution failed: %s\n", PQerrorMessage(conn));
        PQclear(res);
        PQfinish(conn);
        return;
    }

    PQclear(res);
    PQfinish(conn);
}

init_hive_link() 
{

    if (!sql_init) {
        hive_conn = PQconnectdb(DBSTRING);
        if (PQstatus(hive_conn) == CONNECTION_BAD) {
            /*  ummm, bad mojo here. What'll we do?  */
        } else {
            register_host(); 
            sql_init = true;
        }
    }
}

close_hive_link () 
{
    PQclear(last_qury);
    PQclear(last_ins);
    PQfinish(hive_conn);  //Close the DB connection gracefully before we shutdown.
}

int get_hive_vers() 
{
        int hi_psql_vers = PQserverVersion(hive_conn);
        return hi_psql_vers;
}

int send_binary_data(char *data_Block, )
{
    const char *paramValues[1];
    int paramLengths[1];
    int paramFormats[1];
    // Assign the binary block to the parameter
    paramValues[0] = data_block;
    paramLengths[0] = sizeof(data_block);
    paramFormats[0] = 1; // 1 means binary format
    PGresult *res;
    int rows = 0;

    char *quoted_sql = (char *) malloc(strlen(ins_sql) + quoted_hash.length() + quoted_count.length() + 1);
    //  std::sprintf(quoted_sql, ins_sql, quoted_hash.c_str(), 1 );

    res = PQexecParams(conn, quoted_sql, 1, nullptr, paramValues, paramLengths, paramFormats, 0);

    if (PQresultStatus(res) != PGRES_COMMAND_OK) {
        rows = 0;
        /* PROBLEM */
    } else {
        rows = atoi(PQcmdTuples(res));
        last_res = res;
    }


        PQclear(res);
}
