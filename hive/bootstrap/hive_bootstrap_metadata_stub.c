/**
 * Minimal synchronous implementations of the metadata async helpers
 * so the bootstrap binary can link against hive_common_sql without
 * pulling in the hive_guard worker thread.
 */

#include "../common/hive_common_sql.h"

bool hifs_metadata_async_execute(const char *sql_string)
{
	if (!sql_string || !*sql_string)
		return false;

	MYSQL_RES *res = NULL;
	bool ok = hifs_execute_query(sql_string, &res);
	if (res)
		mysql_free_result(res);
	return ok;
}

void hifs_metadata_async_shutdown(void)
{
	/* Bootstrap never spawns the async worker, so nothing to do. */
}
