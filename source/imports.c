#include <string.h>

#include "imports.h"

static const struct {
    const import_entry *table;
    const int *count;
} tables[] = {
    {darwin_imports, &darwin_imports_count},
};

uintptr_t imports_lookup(const char *name) {
    for (unsigned t = 0; t < sizeof(tables) / sizeof(tables[0]); t++)
        for (int i = 0; i < *tables[t].count; i++)
            if (!strcmp(tables[t].table[i].name, name))
                return tables[t].table[i].addr;
    return 0;
}
