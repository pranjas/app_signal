# app_signal
A Signal framework for writing generic signal handlers.

This is very minimal approach on how signal handlers can be written for an application.
The execution of handlers can either be done in a separate process or it can also be the
same process. There can also be multiple handlers installed for a signal which gets executed
in the order they were installed.

The handlers can use any function without requiring them to be signal safe. The only requirement
is that the application is able to call the **app_signal_subsys_exec_pending** in a tight loop.

Any comments and enhancements are welcome.
