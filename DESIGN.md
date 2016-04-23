# Sysprof 2.x

## Design

### libsysprof-2

This library provides various profiling infrastructure for Sysprof.
This includes the core interfaces for data sources, capture readers
and writers, and the SpProfiler implementation. It also includes
the SpProfile interface and its various implementations, such as
SpCallgraphProfile. If you need a no-frills library to perform
captures this is all you need.

### libsysprof-ui-2

This library provides various UI components and profiling tools as a library.
It is intended to be used from IDEs such as GNOME Builder.

### sysprofd

Sometimes, your current user does not have the required capabilities
such as (`CAP_SYS_ADMIN`) to access performance counters. This daemon provides
a way to get access to those counters while going through the normal
authorization flow users are familiar with.

### sysprof-cli

This is a command line tool to help you capture in an automated fashion or
when you don't have access to a UI or want to remove the UI overhead from
system traces.

### sysprof

This is the tranditional Sysprof interface. It uses libsysprof-2 to setup
and manage performance counters and display results.

