#ifndef __APP_SIGNAL_H__
#define __APP_SIGNAL_H__
#include <signal.h>
#include <stdbool.h>
#include <stdlib.h>
#include "list.h"

typedef enum APP_SIGNAL_EXEC_TYPE {
	/*
	 * Execute handler in a new process.
	 * */
	APP_SIGNAL_EXEC_NEW = (1<<0),
	/*
	 * Execute handler in the same process.
	 * */
	APP_SIGNAL_EXEC_THIS,
	/*
	 * Execute handler in the signal handler.
	 * TODO: Implement.
	 * */
	APP_SIGNAL_EXEC_HANDLER,
	/*
	 * Execute the handler in a new process,
	 * but don't wait for it to finish.
	 * */
	APP_SIGNAL_EXEC_NEW_ASYNC,
} app_signal_exec_type_e;

#define APP_SIGNAL_FLAG_CLEAR_SIGNALS 		(1<<0)
#define APP_SIGNAL_FLAG_CLEAR_THIS_SIGNAL 	(1<<1)

struct app_signal;

typedef int (*app_sigaction_t)(struct app_signal*, int signum, siginfo_t *siginfo,
		void *);
struct app_signal {
	int signum;
	app_signal_exec_type_e how;
	app_sigaction_t sig_action;
	volatile sig_atomic_t pending;
	int flags;
	void *opaque;
	int max_siginfo; /*Maximum count of siginfo_t*/
	int nr_siginfo; /*Current total number of siginfo saved.*/
	siginfo_t *saved_siginfo; /*Array of siginfo_t. Present only if assigned.*/
	struct dl_list signal_list;
	struct dl_list signal_sibling;
	/*
	 * Only valid at the time handler
	 * is executed.
	 * */
	siginfo_t siginfo;
	/*
	 * Used to restore signal masks.
	 * post all handlers are done.
	 * */
	sigset_t old_sigmask;
};

bool app_signal_pending(int signum);
bool app_signal_registered(int signum);
int  app_register_signal(int signum, struct app_signal *);
void app_unregister_signal(int signum);
struct app_signal* app_signal_first_handler(int signum);
int app_signal_subsys_init(void);
void app_signal_subsys_exec_pending(void);
void app_unregister_all(void);
#endif /*__APP_SIGNAL_H__*/
