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
            //register_hive_host(); 
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

PGresult *hifs_execute_sql(char* sql_string) 
{
    PGresult *res = PQexec(sqldb.hive_conn, sql_string);
    if (PQresultStatus(res) != PGRES_COMMAND_OK) {
        hifs_err("SQL execution failed: %s\n", PQerrorMessage(sqldb.hive_conn));
        PQclear(res);
        return NULL;
    }
    sqldb.rec_count = atoi(PQcmdTuples(res));
    return res;
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


PGresult *hifs_get_hive_host_data(char *machine_id) 
{
    char sql_query[512];
    PGresult *res;
    MACHINE_GETINFO(machine_id, sql_query);
    res = hifs_execute_sql(sql_query);
    sqldb.last_qury = res;
    sqldb.rows = atoi(PQcmdTuples(res)); 
    return res;
}

int register_hive_host(void) 
{
    PGresult *res;
    char *name, *hive_mach_id, *ip_address, *os_name, *os_version, *os_release, *machine_type;
    struct hostent *host_entry;
    struct in_addr h_addr;
    struct utsname uts;
    name = (char *) malloc(100);
    hive_mach_id = (char *) malloc(100);
    ip_address = (char *) malloc(30);
    os_name = (char *) malloc(100);
    os_version = (char *) malloc(100);
    os_release = (char *) malloc(65);
    machine_type = (char *) malloc(65);
    host_entry = malloc(sizeof(struct hostent));
    long hive_host_id;
    int nFields;

    gethostname(name, 100);
    hifs_debug("Host name is [%s]\n", name);
    if ((host_entry = gethostbyname(name)) == NULL) {
        hifs_crit("gethostbyname failed: %s\n", hstrerror(h_errno));
        return 0;
    }
    h_addr.s_addr = *((unsigned long *) host_entry->h_addr_list[0]);
    sprintf(ip_address, "%s", inet_ntoa(h_addr));
    hifs_debug("IP address is [%s]\n", ip_address);
    uname(&uts);
    
    os_name = uts.sysname;
    os_version = uts.version;
    os_release = uts.release;
    machine_type = uts.machine;

    hive_mach_id = hifs_get_machine_id();
    hive_host_id = hifs_get_host_id();

    hifs_info("Hive machine ID is [%s] and host ID is [%ld]\n", hive_mach_id, hive_host_id);

    res = hifs_get_hive_host_data(hive_mach_id);
    sqldb.last_qury = res;
    sqldb.rec_count = sqldb.rows = atoi(PQcmdTuples(res));
    
    if ( sqldb.rows > 0 ) {
        // Populate Host Data
        /* first, print out the attribute names */
        nFields = PQnfields(sqldb.last_qury);
        for (int i = 0; i < nFields; i++) {
            hifs_debug("%s", PQfname(sqldb.last_qury, i));
        }
        /* next, print out the rows */
        for (int i = 0; i < PQntuples(sqldb.last_qury); i++)
        {
            for (int j = 0; j < nFields; j++) {
                hifs_debug("%s", PQgetvalue(sqldb.last_qury, i, j));
            }
        }
        return 1;
    } else {
        PGresult *res;
        char *sql_query;
        int yes_no;
        sql_query = (char *) malloc(100);
        hifs_notice("This host does not exist in the hive. Filesystems cannot be mounted without a hive connection.");
        hifs_notice("Would you like to add this host to the hive? [y/n]\n");
        //yes_no = show_yes_no_dialog("This host is not part of the hive. Add it to the hive now?");
        yes_no = scanf("%d", &yes_no);
        hifs_debug("yes_no is [%d]\n", yes_no);
        if (yes_no == 1) {
            sqldb.host.name = name;
            sqldb.host.machine_id = hive_mach_id;
            sqldb.host.host_id = hive_host_id;
            sqldb.host.ip_address = ip_address;
            sqldb.host.os_name = os_name;
            sqldb.host.os_version = os_version;
            sqldb.host.os_release = os_release;
            sqldb.host.machine_type = machine_type;

            hifs_notice("Registering host to Hive.");
            MACHINE_INSERT(sql_query, name, hive_mach_id, hive_host_id, ip_address, os_name, os_version, os_release, machine_type);
            hifs_info("Registered host to hive: name [%s] machine ID [%s] host ID [%ld] IP address [%s] OS name [%s] OS version [%s] OS release [%s] machine type [%s]\n", name, hive_mach_id, hive_host_id, ip_address, os_name, os_version, os_release, machine_type);
            res = hifs_get_hive_host_data(sql_query);
            sqldb.last_ins = res;
            sqldb.rows_ins = atoi(PQcmdTuples(res));
            return 1;
        } else if (yes_no == 0) {
            hifs_notice("Not registering host to Hive.");
            return 0;
        }
    }
    
    return 0;
}

char *hifs_get_quoted_value(char *in_str) 
{
    size_t escaped_binary_field_size = strlen(in_str);
    unsigned char *esc_field = (unsigned char *) malloc(150);
    PQescapeBytea(esc_field, 0, &escaped_binary_field_size);

    if (!esc_field) {
        hifs_err("Failed to ESCAPE binary data FOR sql: {%s}", PQerrorMessage(sqldb.hive_conn));
        return NULL;
    } else {
        hifs_debug("unescaped string value for sql = {%s}", esc_field);
        return (char *)esc_field;
    }
    return NULL;
}

char *hifs_get_unquoted_value(char *in_str) 
{
    size_t unescaped_binary_field_size = strlen(in_str);
    unsigned char *unesc_field = (unsigned char *) malloc(150);
    PQunescapeBytea(unesc_field, &unescaped_binary_field_size);

    if (!unesc_field) {
        hifs_err("Failed to UN-ESCAPE binary data FOR sql: {%s}", PQerrorMessage(sqldb.hive_conn));
        return NULL;
    } else {
        hifs_debug("unescaped string value for sql = {%s}", unesc_field);
        return (char *)unesc_field;
    }
    return NULL;
}

void hifs_release_query (void) { PQclear(sqldb.last_qury); PQclear(sqldb.last_ins); }

PGresult *hifs_insert_data(char *q_string) 
{
    PGresult *res;
    char *quoted_query;
    quoted_query = (char *) malloc(100);

    quoted_query = hifs_get_quoted_value(q_string);
        
    res = hifs_execute_sql(quoted_query);

    if (sqldb.rows_ins == 0) {
        hifs_err("Failed to execute sql: {%s}", PQerrorMessage(sqldb.hive_conn));
        return res;
    } else {
        sqldb.rows_ins = atoi(PQcmdTuples(res));
        sqldb.last_ins = res;
        return res;
    }
    return res;
}

