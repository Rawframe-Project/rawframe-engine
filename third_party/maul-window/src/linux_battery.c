// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// A Linux gamepad's battery, from sysfs.

#include "linux_battery.h"

#include <dirent.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

bool mwinLinuxFindBattery(const char* root, int node, mwinLinuxBattery* batteryOut)
{
    batteryOut->path[0] = '\0';
    char folder[MWIN_LINUX_BATTERY_PATH];
    int length = snprintf(folder, sizeof(folder),
                          "%s/class/input/event%d/device/device/power_supply", root, node);
    if (length <= 0 || (size_t)length >= sizeof(folder))
    {
        return false;
    }
    DIR* directory = opendir(folder);
    bool found = false;
    for (struct dirent* entry = directory != nullptr ? readdir(directory) : nullptr;
         entry != nullptr && !found; entry = readdir(directory))
    {
        char path[MWIN_LINUX_BATTERY_PATH];
        int written = snprintf(path, sizeof(path), "%s/%s/capacity", folder, entry->d_name);
        found = entry->d_name[0] != '.' && written > 0 && (size_t)written < sizeof(path) &&
                access(path, R_OK) == 0;
        if (found)
        {
            memcpy(batteryOut->path, path, (size_t)written + 1);
        }
    }
    if (directory != nullptr)
    {
        closedir(directory);
    }
    return found;
}

int8_t mwinLinuxReadBattery(const mwinLinuxBattery* battery)
{
    FILE* file = battery->path[0] != '\0' ? fopen(battery->path, "r") : nullptr;
    char text[8] = {0};
    size_t length = file != nullptr ? fread(text, 1, sizeof(text) - 1, file) : 0;
    if (file != nullptr)
    {
        (void)fclose(file);
    }
    int percent = 0;
    size_t digits = 0;
    while (digits < length && digits < 3 && text[digits] >= '0' && text[digits] <= '9')
    {
        percent = percent * 10 + (text[digits] - '0');
        digits++;
    }
    bool ended = digits < length && (text[digits] == '\n' || text[digits] == '\0');
    bool whole = digits == length || ended;
    return digits > 0 && whole && percent <= 100 ? (int8_t)percent : -1;
}
