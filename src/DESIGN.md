# Sysprof Overview

## libsysprof-capture

This static-library contains everything you need to read and write the raw
sysprof capture format.

It does not depend on modern tooling like GLib precisely because it is also
used by GLib to implement capturing profiling data.

That roughly means POSIX and C11.

## libsysprof

This shared library (can also be used statically, internally) provides a
high-level API to the capture format (`SysprofDocument`) as well as a
convenient profiler API (`SysprofProfiler` and `SysprofInstrument`).

It heavily relies on GLib, GObject, and libdex for futures and fibers. This
empowers the heavy use of "await" even though we are in C.

Instruments provide data to the recording and ultimately land in a capture.

Symbolizers also live here to take instruction pointers and convert them into
symbol names. There is a decent amount of complexity around this to handle
mount namespaces, flatpaks, containers, and other modern Linux features.

## preload

Some profiler instruments require code running in the target process. This
directory contains `*.so` modules which can be used with `LD_PRELOAD`.

They will, once used in the target process, use the sysprof collector API to
communicate with a Sysprof instance (sysprof-cli, the full UI process, etc).

## sysprof

This is the UI process.

It uses libsysprof to access capture files and visualize that information.

## sysprof-agent

This agent is installed in most GNOME SDKs and allows IDEs such as Builder to
run a profiler instance inside the SDK container. Such tooling is necessary
when you want to inject `LD_PRELOAD` modules into a process.

## sysprof-cat

This is a simple tool which helps you see what is inside a sysprof capture.

## sysprof-cli

This is a command line tool which allows you to do most of what the UI process
can do to record a profiling session. It is often handy to use this in scripts,
on servers, or other embedded devices.

## sysprofd

Most users cannot directly access Perf or even where their kernel symbols are
laid out in memory. We obviously need that to make a profiler so sysprofd gates
access to that via D-Bus and polkit.

The UI application, sysprof-cli, or any libsysprof-enabled application can
connect to sysprofd and request a new perf FD. That FD is passed to the calling
application which may `mmap()` it to read perf data.

It also provides unredacted access to sensitive `/proc` files for authenticated
clients.

## sysprof-diff

This is a simple tool which tries to find the difference in two captures as
far as samples are concerned.

## sysprof-live-unwinder

Many distros have switched to using frame-pointers out of practicality. Fedora
is the notable example leading that.

However, some platforms such as RHEL and CentOS do not as of this time of
writing.  To allow for some form of useful stack traces we have a
`sysprof-live-unwinder` which can take samples from `perf` and unwind them in
user-space in real-time by looking at the captured stack contents and register
data.

Libsysprof accesses this through the `SysprofLiveUnwinder` instrument.
