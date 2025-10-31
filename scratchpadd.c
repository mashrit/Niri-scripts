#include <stdlib.h>
#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <jansson.h>

#define CHECK_INTERVAL_US 200000  // 200ms between checks

static char* get_cmd_output(const char *cmd)
{
	FILE *pipe = popen(cmd, "r");
	if (!pipe) return NULL;

	static char buffer[4096];
	if (!fgets(buffer, sizeof(buffer), pipe))
	{
	    pclose(pipe);
	    return NULL;
	}
	pclose(pipe);

	buffer[strcspn(buffer, "\n")] = 0;
	return buffer;
}

void get_work_idx(int currWorkId, int *currWorkIdx, int *lastWorkIdx)
{
	// --- Get workspaces ---
	char *workspaces_json = get_cmd_output("niri msg -j workspaces");
	if (!workspaces_json)
	{
		*currWorkIdx = -1;
		*lastWorkIdx = -1;
	}

	json_error_t error;
	json_t *workspaces = json_loads(workspaces_json, 0, &error);
	if (!workspaces)
	{
		*currWorkIdx = -1;
		*lastWorkIdx = -1;
	}

	json_t *ws;
	size_t i;
	json_array_foreach(workspaces, i, ws)
	{
		json_t *idx = json_object_get(ws, "idx");
		json_t *id = json_object_get(ws, "id");
		json_t *activeWin = json_object_get(ws, "active_window_id");

		if (!json_integer_value(activeWin))
			*lastWorkIdx = json_integer_value(idx);

		if (json_integer_value(id) == currWorkId)
			*currWorkIdx = json_integer_value(idx);
	}
	json_decref(workspaces);
}

int get_curr_work_id(json_t *event)
{
	const char *type;
	json_t *data;

	void *iter = json_object_iter(event);
	if (!iter)
	    return -1;
	
	type = json_object_iter_key(iter);
	data = json_object_iter_value(iter);
	
	if (strcmp(type, "WorkspaceActivated") == 0) 
	{
	    json_t *id = json_object_get(data, "id");
	    if (json_is_integer(id)) 
			return json_integer_value(id);
	}

	return -1;
}

int main(void)
{
	daemon(0, 0);

	FILE *pipe = popen("niri msg -j event-stream","r");
	if (!pipe)
	{
	    printf("Failed to open niri event stream");
	    return 1;
	}
	

	json_error_t error;
	int cmId = -1;

	char *windows_json = get_cmd_output("niri msg -j windows");
	if (!windows_json)
		return 1;

	json_t *windows = json_loads(windows_json, 0, &error);
	if (!windows)
	{
		fprintf(stderr, "JSON parse error: %s\n", error.text);
		return 1;
	}

	json_t *window;
	size_t i = 0;
	json_array_foreach(windows, i, window)
	{
		json_t *title = json_object_get(window,"title");
		json_t *id = json_object_get(window, "id");

		if(title && strcmp(json_string_value(title), "cm") == 0)
		{
			cmId = json_integer_value(id);
			break;
		}
	}
	json_decref(windows);


	char buffer[8192];
	while(fgets(buffer, sizeof(buffer), pipe))
	{
		if(access("/tmp/scratchpad-closed", F_OK) == -1)
		{
			system("scratchpad clear");
			continue;
		}

		char *pgrepAns = get_cmd_output("pgrep cm");
		if (!pgrepAns)
		{
			system("scratchpad clear");
			continue;
		}

		json_t *event = json_loads(buffer, 0, &error);
		if (!event)
		{
			fprintf(stderr, "JSON event parse error: %s\n", error.text);
			continue;
		}

		int currWorkId = -1;
		currWorkId = get_curr_work_id(event);
		if (currWorkId == -1)
			continue;

		int lastWorkIdx = -1;
		int currWorkIdx = -1;
		get_work_idx(currWorkId, &currWorkIdx, &lastWorkIdx);
		if (lastWorkIdx == -1 || currWorkIdx == -1)
			continue;	

		if (currWorkIdx >= lastWorkIdx - 1)
		{
			char command[100];
			sprintf(command, "niri msg action move-window-to-workspace --window-id %d --focus false %d", cmId, lastWorkIdx);
			system(command);
		}
		
		json_decref(event);
	}
	

	return 0;
}
