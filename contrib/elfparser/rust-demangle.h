/*
Imported from https://github.com/LykenSol/rust-demangle.c commit 4283d46e4064a7e1c54bc9918a07b066cb43fca3
Modifications from upstream:
* Add sysprof_ prefix to exported symbols and mark them as hidden
* Add pragma once
* Use glib begin/end decls
*/

#pragma once

#include <glib.h>

G_BEGIN_DECLS

#include <stdbool.h>
#include <stddef.h>

#define RUST_DEMANGLE_FLAG_VERBOSE 1

G_GNUC_INTERNAL
bool sysprof_rust_demangle_with_callback(
    const char *mangled, int flags,
    void (*callback)(const char *data, size_t len, void *opaque), void *opaque
);

G_GNUC_INTERNAL
char *sysprof_rust_demangle(const char *mangled, int flags);

G_END_DECLS
