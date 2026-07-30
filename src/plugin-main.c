/*
POERecorder OBS Plugin
Copyright (C) <2026> <Roldan Gammad> <rdgammad@gmail.com>

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License along
with this program. If not, see <https://www.gnu.org/licenses/>
*/

#include <obs-module.h>
#include <obs-frontend-api.h>
#include <plugin-support.h>

#ifdef _WIN32
#include <windows.h>
#include <tlhelp32.h>
#include <wchar.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#endif

OBS_DECLARE_MODULE()
OBS_MODULE_USE_DEFAULT_LOCALE(PLUGIN_NAME, "en-US")

static volatile bool poe_log_monitor_should_stop = false;

static bool path_of_exile_is_running(void)
{
#ifdef _WIN32
	HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
	if (snapshot == INVALID_HANDLE_VALUE) {
		obs_log(LOG_ERROR, "failed to enumerate processes");
		return false;
	}

	PROCESSENTRY32W entry;
	entry.dwSize = sizeof(entry);

	bool found = false;
	if (Process32FirstW(snapshot, &entry)) {
		do {
			if (_wcsicmp(entry.szExeFile, L"pathofexilesteam.exe") == 0 || _wcsicmp(entry.szExeFile, L"pathofexile.exe") == 0) {
				found = true;
				break;
			}
		} while (Process32NextW(snapshot, &entry));
	}

	CloseHandle(snapshot);
	return found;
#else
	obs_log(LOG_WARNING, "process check is only implemented on Windows");
	return false;
#endif
}

#ifdef _WIN32
static DWORD WINAPI poe_log_monitor_thread(void *unused)
{
	(void)unused;

	const char *log_path = "C:\\Program Files (x86)\\Steam\\steamapps\\common\\Path of Exile\\logs\\latestclient.txt";
	long last_size = 0;

	while (!poe_log_monitor_should_stop) {
		if (!path_of_exile_is_running()) {
			if (obs_frontend_recording_active()) {
				obs_frontend_recording_stop();
				obs_log(LOG_INFO, "Recording stopped: Path of Exile closed");
			}
			Sleep(1000);
			continue;
		}

		FILE *log_file = fopen(log_path, "rb");
		if (!log_file) {
			Sleep(1000);
			continue;
		}

		if (fseek(log_file, 0, SEEK_END) != 0) {
			fclose(log_file);
			Sleep(1000);
			continue;
		}

		long current_size = ftell(log_file);
		if (current_size < 0) {
			fclose(log_file);
			Sleep(1000);
			continue;
		}

		if (current_size < last_size) {
			last_size = 0;
		}

		if (current_size > last_size) {
			if (fseek(log_file, last_size, SEEK_SET) != 0) {
				fclose(log_file);
				Sleep(1000);
				continue;
			}

			size_t bytes_to_read = (size_t)(current_size - last_size);
			char *buffer = malloc(bytes_to_read + 1);
			if (!buffer) {
				fclose(log_file);
				Sleep(1000);
				continue;
			}

			size_t bytes_read = fread(buffer, 1, bytes_to_read, log_file);
			buffer[bytes_read] = '\0';

			char *line_start = buffer;
			while (line_start != NULL && *line_start != '\0') {
				char *line_end = strchr(line_start, '\n');
				if (line_end != NULL) {
					*line_end = '\0';
				}

				if (strstr(line_start, "[SCENE] Set Source [(null)]") != NULL) {
					if (!obs_frontend_recording_active()){ // Start recording when entering a zone if not already recording
						obs_frontend_recording_start();
						obs_log(LOG_INFO, "Recording started: player entered a new zone:  %s", line_start);
					}
				}
				if (strstr(line_start, "has been slain.") != NULL) {
					if (obs_frontend_recording_active()) // Stop recording when the player has died
					{
						obs_frontend_recording_stop();
						obs_log(LOG_INFO, "Recording stopped: player has died: %s", line_start); 
					} 
					
					obs_frontend_recording_start(); // Reset recording state to true after death
				}

				if (line_end != NULL) {
					line_start = line_end + 1;
				} else {
					break;
				}
			}

			free(buffer);
		}

		last_size = current_size;
		fclose(log_file);
		Sleep(1000);
	}

	return 0;
}
#endif

bool obs_module_load(void)
{
	bool poe_running = path_of_exile_is_running();
	obs_log(LOG_INFO, "Path of Exile process check: %s", poe_running ? "running" : "not running");

#ifdef _WIN32
	poe_log_monitor_should_stop = false;
	CreateThread(NULL, 0, poe_log_monitor_thread, NULL, 0, NULL);
	obs_log(LOG_INFO, "monitoring Path of Exile client log for 'you have entered...' entries");
#endif

	obs_log(LOG_INFO, "plugin loaded successfully (version %s)", PLUGIN_VERSION);
	return true;
}

void obs_module_unload(void)
{
	poe_log_monitor_should_stop = true;
	obs_log(LOG_INFO, "plugin unloaded");
}
