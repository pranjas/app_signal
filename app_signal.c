#include "app_signal.h"
#include <errno.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <string.h>
#include <unistd.h>
#include <stdio.h>

static DEFINE_DL_LIST(app_registered_signals);
static sigset_t app_pending_signals;

static int app_signals_lock(sigset_t *oldset)
{
	sigset_t sigset;
	int ret = 0;
	/*
	 * We don't want any signals at this time.
	 * Block all signals as this list is accessed
	 * in the signal handler.
	 * */
	sigfillset(&sigset);
	ret = sigprocmask(SIG_BLOCK, &sigset, oldset);
	return ret;
}

static void app_signal_unlock(int signal ,sigset_t *restore_set)
{
	sigset_t sigset;

	sigprocmask(SIG_SETMASK, restore_set, NULL);
	if (signal > 0) {
		sigemptyset(&sigset);
		sigaddset(&sigset, signal);
		sigprocmask(SIG_UNBLOCK, &sigset, NULL);
	}
}

int app_signal_subsys_init()
{
	/*
	 * Do something useful here later,
	 * if required.
	 * */
	sigemptyset(&app_pending_signals);
	return 0;
}

struct app_signal* app_signal_first_handler(int signum)
{
	struct app_signal *item, *tmp;
	dl_list_for_each_safe(item, tmp,
		&app_registered_signals, struct app_signal,
		signal_list) {
		if (item->signum == signum)
			return item;
	}
	return NULL;
}

bool app_signal_registered(int signum)
{
	return !!app_signal_first_handler(signum);
}

static void app_signal_dispatcher(int signum, siginfo_t *siginfo, void *ucontext)
{
	struct app_signal *first_handler =
		app_signal_first_handler(signum);
	int max_siginfo = 0;
	int next_siginfo = 0;

	/*
	 * Signal arrived but no signal
	 * handler present. Shouldn't happen
	 * though..!
	 * */
	if (!first_handler)
		return;

	max_siginfo = first_handler->max_siginfo;
	next_siginfo = first_handler->nr_siginfo;

	if (sigismember(&app_pending_signals, signum)) {
		if (max_siginfo > 0 && (next_siginfo < max_siginfo)) {
			first_handler->saved_siginfo[next_siginfo] = *siginfo;
			first_handler->nr_siginfo++;
		}
	} else
		first_handler->siginfo = *siginfo;
	/*
	 * Mark signal as seen.
	 * */
	sigaddset(&app_pending_signals, signum);
}

static int app_install_sighandler(int signum)
{
	/*
	 * The only signal we can block is the
	 * signum itself.
	 * */
	struct sigaction sa;
	sa.sa_sigaction = app_signal_dispatcher;
	sigemptyset(&sa.sa_mask);
	sa.sa_flags = SA_SIGINFO;
	if(sigaction(signum, &sa, NULL))
		return errno;
	return 0;
}

int app_register_signal(int signum, struct app_signal *app_signal)
{
	struct app_signal *item, *tmp;
	bool found = false;
	sigset_t oldset;
	int ret = 0;
	/*
	 * We don't want any signals at this time.
	 * Block all signals as this list is accessed
	 * in the signal handler.
	 * */
	ret = app_signals_lock(&oldset);
	if (ret)
		return -1;

	if (!dl_list_empty(&app_registered_signals)) {
		dl_list_for_each_safe(item, tmp, &app_registered_signals,
				struct app_signal, signal_list) {
			if (item->signum == signum) {
				found = true;
				break;
			}
		}
	}
	if (found) {
		dl_list_add(&item->signal_sibling,
				&app_signal->signal_sibling);
	} else {
		/*
		 * See if we can install the global
		 * signal handler for this signal.
		 * */
		int err = app_install_sighandler(signum);

		if (err) {
			app_signal_unlock(-1, &oldset);
			return err;
		}
		app_signal->signum = signum;
		dl_list_init(&app_signal->signal_sibling);
		dl_list_add(&app_registered_signals, &app_signal->signal_list);
	}
	app_signal_unlock(signum, &oldset);
	return 0;
}

/*
 * Should be done very earlier in code, for example
 * after fork and while trying to setup signals for
 * the new process and removing inherited signals.
 * 
 * Since this function can't have locks, don't introduce
 * any locking while calling this function.
 * */
void app_unregister_signal(int signum)
{
	struct app_signal *item, *tmp;
	sigset_t sigset, oldset;
	int ret = 0;
	/*
	 * We don't want any signals at this time.
	 * */
	sigfillset(&sigset);
	ret = sigprocmask(SIG_BLOCK, &sigset, &oldset);
	
	if (ret)
		goto out; /*Can't touch this list without blocking signals.*/
	dl_list_for_each_safe(item, tmp, &app_registered_signals,
			struct app_signal, signal_list) {
		if (item->signum == signum || signum == -1) {
			struct app_signal *sibling_sig, *sibling_tmp;
			dl_list_for_each_safe(sibling_sig, sibling_tmp,
					&item->signal_sibling, struct app_signal, signal_sibling) {
				dl_list_del(&sibling_sig->signal_sibling);
			}
			dl_list_del(&item->signal_list);
			/*
			 * Remove the mask from the original
			 * set.
			 * */
			sigdelset(&oldset, item->signum);
			if (signum != -1)
				break;
		}
	}
	sigprocmask(SIG_SETMASK, &oldset, NULL);
out:
	return;
}

void app_unregister_all()
{
	/*
	 * Unregister all signal handlers.
	 * */
	app_unregister_signal(-1);
}

static void app_signal_exec_handler(struct app_signal *app_signal)
{
	struct app_signal *sibling;
	siginfo_t *siginfo = &app_signal->siginfo;

	app_signal->sig_action(app_signal, app_signal->signum, siginfo, NULL);
	if (!dl_list_empty(&app_signal->signal_sibling)) {
		dl_list_for_each(sibling, &app_signal->signal_sibling, struct app_signal,
				signal_sibling) {
			sibling->sig_action(sibling, app_signal->signum, siginfo, NULL);
		}
	}
	/*
	 * Do we've anything saved?
	 * */
	if (app_signal->max_siginfo > 0) {
		int nr_saved = app_signal->nr_siginfo;

		while(nr_saved > 0) {
			siginfo_t *saved_siginfo = 
				&app_signal->saved_siginfo[--nr_saved];
			app_signal->sig_action(app_signal, 
					app_signal->signum, saved_siginfo, NULL);
			
			if (!dl_list_empty(&app_signal->signal_sibling)) {
				dl_list_for_each(sibling, &app_signal->signal_sibling, struct app_signal,
						signal_sibling) {
					sibling->sig_action(sibling,
							app_signal->signum, saved_siginfo, NULL);
				}
			}
		}
		app_signal->nr_siginfo = 0;
	}
}

static void app_signal_clear_pending(struct app_signal *app_signal)
{
	sigset_t sigmask;

	memset(&app_signal->siginfo, 0, sizeof(app_signal->siginfo));
	/*
	 * Restore the blocked signal.
	 * */
	sigemptyset(&sigmask);
	sigdelset(&app_pending_signals, app_signal->signum);
	sigaddset(&sigmask, app_signal->signum);
	app_signal->pending = 0;
	sigprocmask(SIG_UNBLOCK, &sigmask, NULL);
	sigemptyset(&app_signal->old_sigmask);
}

void app_signal_subsys_exec_pending()
{
	struct app_signal *item, *tmp;
	sigset_t sigmask;

	if (!dl_list_empty(&app_registered_signals)) {
		dl_list_for_each_safe(item, tmp, &app_registered_signals,
				struct app_signal, signal_list) {
			int sock_pair[2];
			int child_status = -1;
			pid_t child_pid = -1;
			/*
			 * First block the signal.
			 * */
			if (!item->pending && 
					sigismember(&app_pending_signals, item->signum)) {
				sigemptyset(&sigmask);
				sigaddset(&sigmask, item->signum);
				/*
				 * We're sure now that this signal isn't
				 * going to bother us anymore.
				 * */
				/*
				 * We'll handle the signal later
				 * after we block the signal we want to handle.
				 * */
				if (!sigprocmask(SIG_BLOCK, &sigmask,
							&item->old_sigmask)) {
					item->pending = 1;
				}
			}

			if (!item->pending)
				continue;

			switch (item->how) {
				case APP_SIGNAL_EXEC_NEW:

					if (socketpair(AF_UNIX, SOCK_STREAM, 0, sock_pair))
						goto cleanup_signal;

					child_pid = fork();
					if (child_pid < 0) {
						close(sock_pair[0]);
						close(sock_pair[1]);
						goto cleanup_signal;
					}

					if (!child_pid) {
						int status = 0;

						if (item->flags & APP_SIGNAL_FLAG_CLEAR_SIGNALS) 
							app_unregister_all();
						else if (item->flags & APP_SIGNAL_FLAG_CLEAR_THIS_SIGNAL)
							app_unregister_signal(item->signum);
						close(sock_pair[0]);
						app_signal_exec_handler(item);
						/*
						 * Send status to parent that all handlers
						 * are done.
						 * */
						write(sock_pair[1], &status, sizeof(status));
						close(sock_pair[1]);
						_exit(0);
					} 
					/*
					 * close child part.
					 * */
					close(sock_pair[1]);
read_again:
					if (read(sock_pair[0], (char*)(&child_status),
								sizeof(child_status) < 0)) {
						if (errno == EINTR)
							goto read_again;
					}
					close(sock_pair[0]);
					break;
				case APP_SIGNAL_EXEC_THIS:
					app_signal_exec_handler(item);
					break;
				case APP_SIGNAL_EXEC_NEW_ASYNC:
					child_pid = fork();
					if (!child_pid) {
						app_signal_exec_handler(item);
						_exit(0);
					}
				default:
					break;
			}
cleanup_signal:
			/*
			 * Cleanup all handlers
			 * */
			app_signal_clear_pending(item);
		}
	}
}
