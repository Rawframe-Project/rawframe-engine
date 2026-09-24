#include "project.h"

#include <stdio.h>
#include <string.h>

#include "kest.h"

// A line of the file, with the ends taken off. Lines are read in place: the
// whole file is in the arena and a line is a piece of it with a nought written
// where the end was.
static char *next_line(char **at) {
    if (*at == NULL || **at == '\0') {
        return NULL;
    }
    char *line = *at;
    char *end = strchr(line, '\n');
    if (end == NULL) {
        *at = line + strlen(line);
    } else {
        *end = '\0';
        *at = end + 1;
        // A file written on the other kind of machine ends its lines with two
        // characters, and the first of them is not part of the line.
        if (end > line && end[-1] == '\r') {
            end[-1] = '\0';
        }
    }
    return line;
}

// The word at the front and what follows it, with the space between taken off.
// Answers NULL for a line that says nothing.
static const char *split_line(char *line, const char **value) {
    while (*line == ' ' || *line == '\t') {
        line++;
    }
    if (*line == '\0' || *line == '#') {
        return NULL;
    }
    char *at = line;
    while (*at != '\0' && *at != ' ' && *at != '\t') {
        at++;
    }
    if (*at == '\0') {
        *value = at;
        return line;
    }
    *at++ = '\0';
    while (*at == ' ' || *at == '\t') {
        at++;
    }
    *value = at;
    return line;
}

static char *read_whole(KestArena *arena, const char *path, bool *there) {
    *there = false;
    FILE *file = fopen(path, "rb");
    if (file == NULL) {
        return NULL;
    }
    *there = true;
    fseek(file, 0, SEEK_END);
    long size = ftell(file);
    rewind(file);
    // A manifest is a page. Anything longer is something else that happens to
    // be called one, and reading it would be reading whatever was handed over.
    if (size < 0 || size > (1 << 20)) {
        fclose(file);
        return NULL;
    }
    char *text = kest_arena_alloc(arena, (size_t)size + 1, 1);
    if (text == NULL) {
        fclose(file);
        return NULL;
    }
    size_t read = fread(text, 1, (size_t)size, file);
    fclose(file);
    text[read] = '\0';
    return text;
}

void kest_project_path(const char *where, char *path, size_t room) {
    size_t length = strlen(where);
    size_t named = strlen(KEST_PROJECT_FILE);
    if (length >= named &&
        strcmp(where + length - named, KEST_PROJECT_FILE) == 0) {
        snprintf(path, room, "%s", where);
    } else if (length == 0) {
        snprintf(path, room, "%s", KEST_PROJECT_FILE);
    } else {
        snprintf(path, room, "%s%s%s", where,
                 where[length - 1] == '/' ? "" : "/", KEST_PROJECT_FILE);
    }
}

KestProject *kest_project_read(KestArena *arena, const char *where,
                               const char **why) {
    return kest_project_from(arena, where, NULL, why);
}

KestProject *kest_project_from(KestArena *arena, const char *where,
                               const char *handed, const char **why) {
    *why = NULL;
    if (arena == NULL || where == NULL) {
        return NULL;
    }
    char path[1024];
    kest_project_path(where, path, sizeof(path));

    // A manifest handed over is the one there is, and nothing is looked for on
    // a disk. See D1172.
    bool there = handed != NULL;
    char *text = handed != NULL
                     ? kest_arena_strndup(arena, handed, strlen(handed))
                     : read_whole(arena, path, &there);
    if (text == NULL) {
        // A file that is not there is not a mistake: a program is a file and
        // needs no project around it. A file that is there and will not be
        // read is.
        *why = there ? "there is a project here and it could not be read"
                     : NULL;
        return NULL;
    }

    KestProject *project = KEST_ARENA_NEW(arena, KestProject);
    if (project == NULL) {
        *why = "there was no room to read the project";
        return NULL;
    }
    memset(project, 0, sizeof(*project));
    project->path = kest_arena_strndup(arena, path, strlen(path));
    project->name = "";
    project->entry = "";
    project->needs_kest = "";
    project->profile = "";
    project->tests = "";

    char *at = text;
    char *line = NULL;
    while ((line = next_line(&at)) != NULL) {
        const char *value = NULL;
        const char *name = split_line(line, &value);
        if (name == NULL) {
            continue;
        }
        if (strcmp(name, "project") == 0) {
            project->name = value;
        } else if (strcmp(name, "entry") == 0) {
            project->entry = value;
        } else if (strcmp(name, "kest") == 0) {
            project->needs_kest = value;
        } else if (strcmp(name, "profile") == 0) {
            project->profile = value;
        } else if (strcmp(name, "tests") == 0) {
            project->tests = value;
        } else if (strcmp(name, "source") == 0) {
            if (project->source_count >= KEST_MOST_SOURCES) {
                *why = "a project says more places to look than this reads";
                return NULL;
            }
            project->sources[project->source_count++] = value;
        } else {
            *why = "a project says something this does not know";
            return NULL;
        }
    }
    return project;
}

const char *kest_project_written(KestArena *arena, const char *name) {
    char room[1024];
    int written = snprintf(
        room, sizeof(room),
        "# What this project is, in `name value` lines. `kest doctor` reads\n"
        "# it back and says what it found.\n"
        "project %s\n"
        "\n"
        "# What to run, check or build when nothing else is named.\n"
        "entry src/main.kest\n"
        "\n"
        "# Where this project's own modules are. A module's name has to match\n"
        "# where its file is under one of these. A dependency is another of\n"
        "# these lines pointing at wherever it was put: there is no registry.\n"
        "source src\n"
        "\n"
        "# Where the programs that check this project are. `kest test` runs\n"
        "# each of them and reads what it answered.\n"
        "tests tests\n"
        "\n"
        "# What it was written against, and which deterministic profile it is\n"
        "# written under.\n"
        "kest %s\n"
        "profile %s %u\n",
        name, KEST_VERSION_STRING, KEST_PROFILE_NAME,
        (unsigned)KEST_PROFILE_VERSION);
    if (written < 0 || (size_t)written >= sizeof(room)) {
        return NULL;
    }
    return kest_arena_strndup(arena, room, (size_t)written);
}
