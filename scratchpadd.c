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

int main(void)
{
	daemon(0, 0);

	while (1)
	{
		if (access("/tmp/scratchpad-closed", F_OK) == -1)
		{
			system("scratchpad clear");
			usleep(CHECK_INTERVAL_US);
			continue;
		}

		// --- Get all windows ---
		char *windows_json = get_cmd_output("niri msg -j windows");
		if (!windows_json)
		{
			usleep(CHECK_INTERVAL_US);
			continue;
		}

		json_error_t error;
		json_t *windows = json_loads(windows_json, 0, &error);
		if (!windows)
		{
			usleep(CHECK_INTERVAL_US);
			continue;
		}

		// Find window with title "cm"
		json_t *window;
		int window_id = -1;
		size_t i;
		json_array_foreach(windows, i, window)
		{
		    json_t *title = json_object_get(window, "title");

			if(!title)
				break;

		    if (strcmp(json_string_value(title), "cm") == 0)
			{
		        json_t *id = json_object_get(window, "id");
		        if (id)
					window_id = json_integer_value(id);

		        break;
		    }
		}
		json_decref(windows);

		if (window_id == -1)
		{
			system("scratchpad clear");
			usleep(CHECK_INTERVAL_US);
			continue;
		}

		// --- Get workspaces ---
		char *workspaces_json = get_cmd_output("niri msg -j workspaces");
		if (!workspaces_json)
		{
			usleep(CHECK_INTERVAL_US);
			continue;
		}

		json_t *workspaces = json_loads(workspaces_json, 0, &error);
		if (!workspaces)
		{
			usleep(CHECK_INTERVAL_US);
			continue;
		}

		int window_workspace_idx = -1;
		int current_workspace_idx = -1;
		json_t *ws;
		json_array_foreach(workspaces, i, ws)
		{
			json_t *active_win = json_object_get(ws, "active_window_id");
			json_t *idx = json_object_get(ws, "idx");
			json_t *focused = json_object_get(ws, "is_focused");

			if (active_win && json_integer_value(active_win) == window_id)
		    	window_workspace_idx = json_integer_value(idx);

			if (focused && json_is_true(focused))
		    	current_workspace_idx = json_integer_value(idx);
		}
		json_decref(workspaces);

		// --- Compare and act ---
		if (window_workspace_idx == current_workspace_idx && window_workspace_idx >= 0)
			system("niri msg action move-window-to-workspace-down --focus false");

		usleep(CHECK_INTERVAL_US);
	}

	return 0;
}
