

#include <libxml/tree.h>
#include <libxml/parser.h>
#include <unistd.h>
#include <sys/stat.h>

#include "postgres.h"
#include "commands/resgroupcmds.h"
#include "catalog/gp_segment_config.h"

#include "utils/builtins.h"
#include "utils/guc.h"
#include "libpq/libpq-be.h"
#include "utils/acl.h"

#include "funcapi.h"

#include "common/comm_utils.h"
#include "common/comm_connectivity.h"
#include "plcontainer.h"
#include "plc_backend_api.h"
#include "plc_docker_api.h"
#include "plc_configuration.h"


PG_FUNCTION_INFO_V1(containers_summary);

PG_FUNCTION_INFO_V1(list_running_containers);


Datum
list_running_containers(pg_attribute_unused() PG_FUNCTION_ARGS) {

	FuncCallContext *funcctx;
	int call_cntr;
	int max_calls;
	int res;
	TupleDesc tupdesc;
	AttInMetadata *attinmeta;
	struct json_object *container_list = NULL;
	char *json_result;
	bool isFirstCall = true;


	/* Init the container list in the first call and get the results back */
	if (SRF_IS_FIRSTCALL()) {
		MemoryContext oldcontext;
		int arraylen;

		/* create a function context for cross-call persistence */
		funcctx = SRF_FIRSTCALL_INIT();

		/* switch to memory context appropriate for multiple function calls */
		oldcontext = MemoryContextSwitchTo(funcctx->multi_call_memory_ctx);

		res = plc_docker_list_container(&json_result);
		if (res < 0) {
			plc_elog(ERROR, "Docker container list error: %s", backend_error_message);
		}

		/* no container running */
		if (strcmp(json_result, "[]") == 0) {
			funcctx->max_calls = 0;
		}

		container_list = json_tokener_parse(json_result);

		if (container_list == NULL) {
			plc_elog(ERROR, "Parse JSON object error, cannot get the containers summary");
		}

		arraylen = json_object_array_length(container_list);

		/* total number of containers to be returned, each array contains one container */
		funcctx->max_calls = (uint32) arraylen;

		/*
		 * prepare attribute metadata for next calls that generate the tuple
		 */

		tupdesc = CreateTemplateTupleDesc(6, false);
		TupleDescInitEntry(tupdesc, (AttrNumber) 1, "SEGMENT_ID",
		                   TEXTOID, -1, 0);
		TupleDescInitEntry(tupdesc, (AttrNumber) 2, "CONTAINER_ID",
		                   TEXTOID, -1, 0);
		TupleDescInitEntry(tupdesc, (AttrNumber) 3, "UP_TIME",
		                   TEXTOID, -1, 0);
		TupleDescInitEntry(tupdesc, (AttrNumber) 4, "OWNER",
		                   TEXTOID, -1, 0);
		TupleDescInitEntry(tupdesc, (AttrNumber) 5, "MEMORY_USAGE(KB)",
		                   TEXTOID, -1, 0);
        TupleDescInitEntry(tupdesc, (AttrNumber) 6, "CPU_USAGE",
		                   TEXTOID, -1, 0);

		attinmeta = TupleDescGetAttInMetadata(tupdesc);
		funcctx->attinmeta = attinmeta;

		MemoryContextSwitchTo(oldcontext);
	} else {
		isFirstCall = false;
	}

	funcctx = SRF_PERCALL_SETUP();

	call_cntr = funcctx->call_cntr;
	max_calls = funcctx->max_calls;
	attinmeta = funcctx->attinmeta;

	if (isFirstCall) {
		funcctx->user_fctx = (void *) container_list;
	} else {
		container_list = (json_object *) funcctx->user_fctx;
	}
	/*if a record is not suitable, skip it and scan next record*/
	while (1) {
		/* send one tuple */
		if (call_cntr < max_calls) {
			char **values;
			HeapTuple tuple;
			Datum result;
			int res;
			char *containerState = NULL;
			struct json_object *containerObj = NULL;
			struct json_object *containerStateObj = NULL;
			
            /* Memory Usage */
            struct json_object *memoryObj = NULL;
			struct json_object *memoryUsageObj = NULL;
            struct json_object *memoryStateObj = NULL;
            struct json_object *memoryStateCacheObj = NULL;
            int64 containerMemoryTotal = 0;
            int64 containerMemoryCache = 0;
            int64 containerMemoryUsage = 0;

            /* CPU Usage */
            struct json_object *cpuObj = NULL;
            struct json_object *preCpuObj = NULL;
            struct json_object *cpuStatusObj = NULL;
            struct json_object *preCpuStatusObj = NULL;
			struct json_object *cpuStatusUsageObj = NULL;
            struct json_object *preCpuStatusUsageObj = NULL;
            struct json_object *cpuSystemUsageObj = NULL;
            struct json_object *preCpuSystemUsageObj = NULL;
            struct json_object *numberCpusObj = NULL;
            double containerMemoryTotal = 0;
            double containerMemoryCache = 0;
            double containerMemoryUsage = 0;

			struct json_object *statusObj = NULL;
			const char *statusStr;
            struct json_object *labelObj = NULL;
			struct json_object *ownerObj = NULL;
			const char *ownerStr;
			const char *username;
			struct json_object *dbidObj = NULL;
			const char *dbidStr;
			struct json_object *idObj = NULL;
			const char *idStr;
			struct json_object *memoryObj = NULL;
			struct json_object *memoryUsageObj = NULL;
            struct json_object *cpuObj = NULL;
			struct json_object *cpuUsageObj = NULL;
			
			/*
			 * Process json object by its key, and then get value
			 */

			containerObj = json_object_array_get_idx(container_list, call_cntr);
			if (containerObj == NULL) {
				plc_elog(ERROR, "Not a valid container.");
			}

			if (!json_object_object_get_ex(containerObj, "Status", &statusObj)) {
				plc_elog(ERROR, "failed to get json \"Status\" field.");
			}
			statusStr = json_object_get_string(statusObj);
			if (!json_object_object_get_ex(containerObj, "Labels", &labelObj)) {
				plc_elog(ERROR, "failed to get json \"Labels\" field.");
			}
			if (!json_object_object_get_ex(labelObj, "owner", &ownerObj)) {
				funcctx->call_cntr++;
				call_cntr++;
				plc_elog(LOG, "failed to get json \"owner\" field. Maybe this container is not started by PL/Container");
				continue;
			}
			ownerStr = json_object_get_string(ownerObj);
			username = GetUserNameFromId(GetUserId());
			if (strcmp(ownerStr, username) != 0 && superuser() == false) {
				funcctx->call_cntr++;
				call_cntr++;
				plc_elog(DEBUG1, "Current username %s (not super user) is not match conatiner owner %s, skip",
					 username, ownerStr);
				continue;
			}

			
			if (!json_object_object_get_ex(labelObj, "dbid", &dbidObj)) {
				funcctx->call_cntr++;
				call_cntr++;
				plc_elog(LOG, "failed to get json \"dbid\" field. Maybe this container is not started by PL/Container");
				continue;
			}
			dbidStr = json_object_get_string(dbidObj);
			
			if (!json_object_object_get_ex(containerObj, "Id", &idObj)) {
				plc_elog(ERROR, "failed to get json \"Id\" field.");
			}
			idStr = json_object_get_string(idObj);

			res = plc_docker_get_container_state(idStr, &containerState);
			if (res < 0) {
				plc_elog(ERROR, "Fail to get docker container state: %s", backend_error_message);
			}

			containerStateObj = json_tokener_parse(containerState);

            /* Parse memory info */
			if (!json_object_object_get_ex(containerStateObj, "memory_stats", &memoryObj)) {
				plc_elog(ERROR, "failed to get json \"memory_stats\" field.");
			}
			if (!json_object_object_get_ex(memoryObj, "usage", &memoryUsageObj)) {
				plc_elog(LOG, "failed to get json \"usage\" field.");
			} else {
				containerMemoryTotal = json_object_get_int64(memoryUsageObj) / 1024;
			}

            if (!json_object_object_get_ex(memoryStateObj, "stats", &memoryUsageObj)) {
				plc_elog(LOG, "failed to get json \"stats\" field.");
			}

            if (!json_object_object_get_ex(memoryStateCacheObj, "cache", &memoryStateObj)) {
				plc_elog(LOG, "failed to get json \"stats.cache\" field.");
			} else {
                containerMemoryCache = json_object_get_int64(memoryStateCacheObj) / 1024;
            }

            containerMemoryUsage = containerMemoryTotal - containerMemoryCache;

            /* Parse CPU info */
            if (!json_object_object_get_ex(containerStateObj, "cpu_stats", &cpuObj)) {
				plc_elog(ERROR, "failed to get json \"cpu_stats\" field.");
			}

            if (!json_object_object_get_ex(containerStateObj, "precpu_stats", &preCpuObj)) {
				plc_elog(ERROR, "failed to get json \"precpu_stats\" field.");
			}

			if (!json_object_object_get_ex(cpuObj, "usage", &cpuStatusObj)) {
				plc_elog(LOG, "failed to get json \"cpu.usage\" field.");
			}

            if (!json_object_object_get_ex(preCpuObj, "usage", &preCpuStatusObj)) {
				plc_elog(LOG, "failed to get json \"precpu.usage\" field.");
			}

            if (!json_object_object_get_ex(memoryStateObj, "stats", &memoryUsageObj)) {
				plc_elog(LOG, "failed to get json \"stats\" field.");
			}

            if (!json_object_object_get_ex(memoryStateCacheObj, "cache", &memoryStateObj)) {
				plc_elog(LOG, "failed to get json \"stats.cache\" field.");
			} else {
                containerMemoryCache = json_object_get_int64(memoryStateCacheObj) / 1024;
            }

            containerMemoryUsage = containerMemoryTotal - containerMemoryCache;


			values = (char **) palloc(5 * sizeof(char *));
			values[0] = (char *) palloc(8 * sizeof(char));
			values[1] = (char *) palloc(80 * sizeof(char));
			values[2] = (char *) palloc(64 * sizeof(char));
			values[3] = (char *) palloc(64 * sizeof(char));
			values[4] = (char *) palloc(32 * sizeof(char));

			snprintf(values[0], 8, "%s", dbidStr);
			snprintf(values[1], 80, "%s", idStr);
			snprintf(values[2], 64, "%s", statusStr);
			snprintf(values[3], 64, "%s", ownerStr);
			snprintf(values[4], 32, "%ld", containerMemoryUsage);

			/* build a tuple */
			tuple = BuildTupleFromCStrings(attinmeta, values);

			/* make the tuple into a datum */
			result = HeapTupleGetDatum(tuple);

			SRF_RETURN_NEXT(funcctx, result);
		} else {
			if (container_list != NULL) {
				json_object_put(container_list);
			}
			SRF_RETURN_DONE(funcctx);
		}
	}

}

Datum
containers_summary(pg_attribute_unused() PG_FUNCTION_ARGS) {

	FuncCallContext *funcctx;
	int call_cntr;
	int max_calls;
	int res;
	TupleDesc tupdesc;
	AttInMetadata *attinmeta;
	struct json_object *container_list = NULL;
	char *json_result;
	bool isFirstCall = true;


	/* Init the container list in the first call and get the results back */
	if (SRF_IS_FIRSTCALL()) {
		MemoryContext oldcontext;
		int arraylen;

		/* create a function context for cross-call persistence */
		funcctx = SRF_FIRSTCALL_INIT();

		/* switch to memory context appropriate for multiple function calls */
		oldcontext = MemoryContextSwitchTo(funcctx->multi_call_memory_ctx);

		res = plc_docker_list_container(&json_result);
		if (res < 0) {
			plc_elog(ERROR, "Docker container list error: %s", backend_error_message);
		}

		/* no container running */
		if (strcmp(json_result, "[]") == 0) {
			funcctx->max_calls = 0;
		}

		container_list = json_tokener_parse(json_result);

		if (container_list == NULL) {
			plc_elog(ERROR, "Parse JSON object error, cannot get the containers summary");
		}

		arraylen = json_object_array_length(container_list);

		/* total number of containers to be returned, each array contains one container */
		funcctx->max_calls = (uint32) arraylen;

		/*
		 * prepare attribute metadata for next calls that generate the tuple
		 */

		tupdesc = CreateTemplateTupleDesc(5, false);
		TupleDescInitEntry(tupdesc, (AttrNumber) 1, "SEGMENT_ID",
		                   TEXTOID, -1, 0);
		TupleDescInitEntry(tupdesc, (AttrNumber) 2, "CONTAINER_ID",
		                   TEXTOID, -1, 0);
		TupleDescInitEntry(tupdesc, (AttrNumber) 3, "UP_TIME",
		                   TEXTOID, -1, 0);
		TupleDescInitEntry(tupdesc, (AttrNumber) 4, "OWNER",
		                   TEXTOID, -1, 0);
		TupleDescInitEntry(tupdesc, (AttrNumber) 5, "MEMORY_USAGE(KB)",
		                   TEXTOID, -1, 0);

		attinmeta = TupleDescGetAttInMetadata(tupdesc);
		funcctx->attinmeta = attinmeta;

		MemoryContextSwitchTo(oldcontext);
	} else {
		isFirstCall = false;
	}

	funcctx = SRF_PERCALL_SETUP();

	call_cntr = funcctx->call_cntr;
	max_calls = funcctx->max_calls;
	attinmeta = funcctx->attinmeta;

	if (isFirstCall) {
		funcctx->user_fctx = (void *) container_list;
	} else {
		container_list = (json_object *) funcctx->user_fctx;
	}
	/*if a record is not suitable, skip it and scan next record*/
	while (1) {
		/* send one tuple */
		if (call_cntr < max_calls) {
			char **values;
			HeapTuple tuple;
			Datum result;
			int res;
			char *containerState = NULL;
			struct json_object *containerObj = NULL;
			struct json_object *containerStateObj = NULL;
			int64 containerMemoryUsage = 0;

			struct json_object *statusObj = NULL;
			const char *statusStr;
            struct json_object *labelObj = NULL;
			struct json_object *ownerObj = NULL;
			const char *ownerStr;
			const char *username;
			struct json_object *dbidObj = NULL;
			const char *dbidStr;
			struct json_object *idObj = NULL;
			const char *idStr;
			struct json_object *memoryObj = NULL;
			struct json_object *memoryUsageObj = NULL;
			
			/*
			 * Process json object by its key, and then get value
			 */

			containerObj = json_object_array_get_idx(container_list, call_cntr);
			if (containerObj == NULL) {
				plc_elog(ERROR, "Not a valid container.");
			}

			if (!json_object_object_get_ex(containerObj, "Status", &statusObj)) {
				plc_elog(ERROR, "failed to get json \"Status\" field.");
			}
			statusStr = json_object_get_string(statusObj);
			if (!json_object_object_get_ex(containerObj, "Labels", &labelObj)) {
				plc_elog(ERROR, "failed to get json \"Labels\" field.");
			}
			if (!json_object_object_get_ex(labelObj, "owner", &ownerObj)) {
				funcctx->call_cntr++;
				call_cntr++;
				plc_elog(LOG, "failed to get json \"owner\" field. Maybe this container is not started by PL/Container");
				continue;
			}
			ownerStr = json_object_get_string(ownerObj);
			username = GetUserNameFromId(GetUserId());
			if (strcmp(ownerStr, username) != 0 && superuser() == false) {
				funcctx->call_cntr++;
				call_cntr++;
				plc_elog(DEBUG1, "Current username %s (not super user) is not match conatiner owner %s, skip",
					 username, ownerStr);
				continue;
			}

			
			if (!json_object_object_get_ex(labelObj, "dbid", &dbidObj)) {
				funcctx->call_cntr++;
				call_cntr++;
				plc_elog(LOG, "failed to get json \"dbid\" field. Maybe this container is not started by PL/Container");
				continue;
			}
			dbidStr = json_object_get_string(dbidObj);
			
			if (!json_object_object_get_ex(containerObj, "Id", &idObj)) {
				plc_elog(ERROR, "failed to get json \"Id\" field.");
			}
			idStr = json_object_get_string(idObj);

			res = plc_docker_get_container_state(idStr, &containerState);
			if (res < 0) {
				plc_elog(ERROR, "Fail to get docker container state: %s", backend_error_message);
			}

			containerStateObj = json_tokener_parse(containerState);
			if (!json_object_object_get_ex(containerStateObj, "memory_stats", &memoryObj)) {
				plc_elog(ERROR, "failed to get json \"memory_stats\" field.");
			}
			if (!json_object_object_get_ex(memoryObj, "usage", &memoryUsageObj)) {
				plc_elog(LOG, "failed to get json \"usage\" field.");
			} else {
				containerMemoryUsage = json_object_get_int64(memoryUsageObj) / 1024;
			}

			values = (char **) palloc(5 * sizeof(char *));
			values[0] = (char *) palloc(8 * sizeof(char));
			values[1] = (char *) palloc(80 * sizeof(char));
			values[2] = (char *) palloc(64 * sizeof(char));
			values[3] = (char *) palloc(64 * sizeof(char));
			values[4] = (char *) palloc(32 * sizeof(char));

			snprintf(values[0], 8, "%s", dbidStr);
			snprintf(values[1], 80, "%s", idStr);
			snprintf(values[2], 64, "%s", statusStr);
			snprintf(values[3], 64, "%s", ownerStr);
			snprintf(values[4], 32, "%ld", containerMemoryUsage);

			/* build a tuple */
			tuple = BuildTupleFromCStrings(attinmeta, values);

			/* make the tuple into a datum */
			result = HeapTupleGetDatum(tuple);

			SRF_RETURN_NEXT(funcctx, result);
		} else {
			if (container_list != NULL) {
				json_object_put(container_list);
			}
			SRF_RETURN_DONE(funcctx);
		}
	}

}