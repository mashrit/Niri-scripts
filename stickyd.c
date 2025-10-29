#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <jansson.h>

#define CHECK_INTERVAL_US 1000000  // 1s between checks

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

void sticky_free(int x)
{
	char buff[100];
	sprintf(buff,"sticky free %d",x);
	system(buff);
}

void update_sticky_windows(int currWorkId, int currWorkIdx)
{
	// printf("updating sticky windows\n");
	//--- get windows ---
	char *windows_json = get_cmd_output("niri msg -j windows");
	if (!windows_json)
		return;

	json_error_t error;
	json_t *windows = json_loads(windows_json, 0, &error);
	if (!windows)
		return;
	
	char stickyId[100];
	FILE *idFile = fopen("/tmp/stickied-windows", "r");
	if (idFile == NULL)
		return;

	while(fgets(stickyId, 100, idFile))
	{
		stickyId[strcspn(stickyId, "\n")] = 0;

		json_t *window;
		int workId = -1;
		size_t i = 0;
		json_array_foreach(windows, i, window)
		{
			json_t *id = json_object_get(window,"id");

			if (id && json_integer_value(id) == atoi(stickyId))
			{
				json_t *wId = json_object_get(window, "workspace_id");
				if (wId)
					workId = json_integer_value(wId);

				break;
			}
		}
		
		if (workId == -1)
		{
			sticky_free(atoi(stickyId));
			continue;
		}

		if (workId == currWorkId)
			continue;

		char buff[100];
		sprintf(buff,"niri msg action move-window-to-workspace --window-id %s %d",stickyId,currWorkIdx);
		system(buff);
	}
	fclose(idFile);
	json_decref(windows);
}

int get_curr_work_idx(int currWorkId)
{
	// --- Get workspaces ---
	char *workspaces_json = get_cmd_output("niri msg -j workspaces");
	if (!workspaces_json)
		return -1;

	json_error_t error;
	json_t *workspaces = json_loads(workspaces_json, 0, &error);
	if (!workspaces)
		return -1;

	int currWorkIdx = -1;
	json_t *ws;
	size_t i;
	json_array_foreach(workspaces, i, ws)
	{
		json_t *idx = json_object_get(ws, "idx");
		json_t *id = json_object_get(ws, "id");

		if (json_integer_value(id) == currWorkId)
			currWorkIdx = json_integer_value(idx);
	}
	json_decref(workspaces);

	return currWorkIdx;
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

int main()
{
	daemon(0, 0);
	
	FILE *pipe = popen("niri msg -j event-stream","r");
	if (!pipe)
	{
	    perror("Failed to open niri event stream");
	    return 1;
	}
	
	char buffer[8192];
	json_error_t error;
	
	while (fgets(buffer, sizeof(buffer), pipe))
	{
		if(access("/tmp/stickied-windows", F_OK) == -1)
			continue;

	    json_t *event = json_loads(buffer, 0, &error);
	    if (!event)
	    {
			fprintf(stderr, "JSON parse error: %s\n", error.text);
			continue;
	    }
		
		int currWorkId = -1;
		int currWorkIdx = -1;
		currWorkId = get_curr_work_id(event);
		// printf("id: %d\n",currWorkId);
		if (currWorkId == -1)
			continue;
		currWorkIdx = get_curr_work_idx(currWorkId);
		// printf("idx: %d\n",currWorkIdx);
		if (currWorkIdx == -1)
			continue;

		update_sticky_windows(currWorkId,currWorkIdx);

		json_decref(event);
	}
	
	pclose(pipe);


	return 0;
}

