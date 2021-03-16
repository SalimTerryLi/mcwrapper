#include <fcntl.h>
#include <poll.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "ServerInfo.h"
#include "parser.h"
#include "perf_monitor.h"
#include "utils.h"
#include "webserv/http_server.h"

pid_t sub_pid;  // sub-process pid
char **sub_argv;// sub-process argv, manually malloc

int main(int argc, char *argv[]);

int main(int argc, char *argv[]) {
	int ret = 0;
	// printout cmd line
	for (int i = 1; i < argc; ++i) {
		printf("%s", argv[i]);
		if (i != argc - 1) {
			printf(" ");
		}
	}
	printf("\n");
	if (argc < 2) {
		eprintf("no cmd specified.\n");
		return -1;
	}

	int _pipe_sub_stdin[2] = {}; // main writes to fork
	int _pipe_sub_stdout[2] = {};// fork writes to main
	int _pipe_sub_stderr[2] = {};// fork writes to main
	if (pipe2(_pipe_sub_stdout, O_NONBLOCK) == -1) {
		eprintf("pipe(_pipe_sub_stdout) failed\n");
		goto cleanup_pipe_sub_stdout;
	}
	if (pipe2(_pipe_sub_stdin, O_NONBLOCK) == -1) {
		eprintf("pipe(_pipe_sub_stdin) failed\n");
		goto cleanup_pipe_sub_stdin;
	}
	if (pipe2(_pipe_sub_stderr, O_NONBLOCK) == -1) {
		eprintf("pipe(_pipe_sub_stderr) failed\n");
		goto cleanup_pipe_sub_stderr;
	}

	sub_pid = fork();
	if (sub_pid == 0) {            // sub-process
		close(_pipe_sub_stdin[1]); // write side
		close(_pipe_sub_stdout[0]);// read side
		close(_pipe_sub_stderr[0]);// read side

		if (dup2(_pipe_sub_stdin[0], 0) == -1) {
			eprintf("dup2(_pipe_sub_stdin[0]) failed: errno=%d\n", errno);
			goto cleanup_dup;
		}
		if (dup2(_pipe_sub_stdout[1], 1) == -1) {
			eprintf("dup2(_pipe_sub_stdout[1]) failed: errno=%d\n", errno);
			goto cleanup_dup;
		}
		if (dup2(_pipe_sub_stderr[1], 2) == -1) {
			eprintf("dup2(_pipe_sub_stderr[1]) failed: errno=%d\n", errno);
			goto cleanup_dup;
		}

		struct pollfd fd[1] = {};
		fd[0].fd = _pipe_sub_stdin[0];
		fd[0].events = POLLIN;
		int pollret = poll(fd, 1, -1);// waiting for parent that asks to start
		if (pollret == 1 && fd[0].revents == POLLIN) {
			unsigned char buf[1] = {0xff};
			read(fd[0].fd, buf, 1);
			if (buf[0] != 0x12) {// magic number. if not meet, exiting.
				goto cleanup_malloc;
			}
		} else {
			goto cleanup_malloc;
		}

		// printf("sub\n");
		sub_argv = static_cast<char **>(malloc(sizeof(char *) * argc));
		if (sub_argv == nullptr) {
			eprintf("malloc failed\n");
			goto cleanup_malloc;
		}
		memcpy(sub_argv, argv + 1, sizeof(char *) * (argc - 1));
		sub_argv[argc - 1] = nullptr;
		ret = execvp(argv[1], sub_argv);

		free(sub_argv);
		// exiting...
	cleanup_malloc:
		close(_pipe_sub_stdin[0]); // read side
		close(_pipe_sub_stdout[1]);// write side
		close(_pipe_sub_stderr[1]);// write side

	} else {                // parent
		if (sub_pid == -1) {// failed to create sub process
			eprintf("fork failed: errno=%d", errno);
			goto cleanup_fork;
		}

		unsigned char tempbuf[1] = {0x12};// magic data to be sent to subp

		if (pthread_mutex_init(&serverHolder_mutex, nullptr) != 0) {
			eprintf("critical error! pthread_mutex_init for serverHolder_mutex failed!\n");
			tempbuf[0] = 0xff;
		}

		if (start_perf_monitor(sub_pid) != 0) {
			eprintf("critical error! perf monitor failed to start!\n");
			tempbuf[0] = 0xff;
		}

		if (start_http_server() != 0) {
			eprintf("critical error! web server failed to start!\n");
			tempbuf[0] = 0xff;
		}

		close(_pipe_sub_stdin[0]); // read side
		close(_pipe_sub_stdout[1]);// write side
		close(_pipe_sub_stderr[1]);// write side
		printf("parent process started\n");

		write(_pipe_sub_stdin[1], tempbuf, 1);// send to subprocess

		// main func
		// wait(nullptr);
		struct GBN_Buffer subp_stdout;
		struct GBN_Buffer subp_stderr;
		struct GBN_Buffer pare_stdin;
		while (true) {
			int poll_ret = 0;
			struct pollfd fds[3] = {};
			fds[0].fd = 0;// manager stdin
			fds[0].events = POLLIN;
			fds[1].fd = _pipe_sub_stdout[0];// sub-process stdout
			fds[1].events = POLLIN;
			fds[2].fd = _pipe_sub_stderr[0];// sub-process stderr
			fds[2].events = POLLIN;

			poll_ret = poll(fds, 3, -1);// poll!
			if (poll_ret == -1) {       // error
				eprintf("poll failed: errno=%d\n", errno);
				break;
			} else if (poll_ret == 0) {
				eprintf("poll failed: unexpected timeout\n");
				break;
			} else {
				char shared_buffer[256] = {};
				int read_ret = 0;
				if (fds[0].revents & POLLIN) {// stdin ready
					read_ret = read(fds[0].fd, shared_buffer, sizeof(shared_buffer));
					if (read_ret == -1) {
						eprintf("read(fds[0].fd) failed: errno=%d\n", errno);
						break;
					}
					write(_pipe_sub_stdin[1], shared_buffer, read_ret);
					if (store_buf(pare_stdin, shared_buffer, read_ret) != 0) {
						eprintf("temp buffer full!\n");
					}
					while (true) {
						char *p = getline(pare_stdin);
						if (p == nullptr) {
							break;
						}
						printf("parent stdin: %s\n", p);
					}
				}

				// first check for POLLIN, and ignore POLLHUP
				// Only deal with POLLHUP after there is no data to be read
				if (fds[1].revents & POLLIN) {// sub-process stdout ready
					read_ret = read(fds[1].fd, shared_buffer, sizeof(shared_buffer));
					if (read_ret == -1) {
						eprintf("read(fds[1].fd) failed: errno=%d\n", errno);
						break;
					}
					write(1, shared_buffer, read_ret);
					if (store_buf(subp_stdout, shared_buffer, read_ret) != 0) {
						eprintf("temp buffer full!\n");
					}
					while (true) {
						char *p = getline(subp_stdout);
						if (p == nullptr) {
							break;
						}
						line_parse_stdout(p);
					}
				} else if (fds[1].revents &
				           POLLHUP) {// pipe closed, sub-process exited.
					break;
				}

				if (fds[2].revents & POLLIN) {// sub-process stderr ready
					read_ret = read(fds[2].fd, shared_buffer, sizeof(shared_buffer));
					if (read_ret == -1) {
						eprintf("read(fds[2].fd) failed: errno=%d\n", errno);
						break;
					}
					write(2, shared_buffer, read_ret);
					if (store_buf(subp_stderr, shared_buffer, read_ret) != 0) {
						eprintf("temp buffer full!\n");
					}
					while (true) {
						char *p = getline(subp_stderr);
						if (p == nullptr) {
							break;
						}
						line_parse_stderr(p);
					}
				}
			}
		}
		stop_http_server();
		stop_perf_monitor();
		pthread_mutex_destroy(&serverHolder_mutex);
		printf("parent process exiting... waiting for sub-process\n");
		int sub_process_status = 0;
		wait(&sub_process_status);// wait for sub-process to exit.
		if (WIFEXITED(sub_process_status)) {
			if (WEXITSTATUS(sub_process_status) != 0) {
				eprintf("Sub process exited with %d\n",
				        WEXITSTATUS(sub_process_status));
			}
		} else {// crashed
			eprintf("sub-process crashed\n");
		}

		// exiting...
		close(_pipe_sub_stdin[1]); // write side
		close(_pipe_sub_stdout[0]);// read side
		close(_pipe_sub_stderr[0]);// read side
		printf("parent process exited\n");
	}

	return ret;
	// TODO: duplicated close
cleanup_dup:
cleanup_fork:
	close(_pipe_sub_stderr[0]);
	close(_pipe_sub_stderr[1]);
cleanup_pipe_sub_stderr:
	close(_pipe_sub_stdin[0]);
	close(_pipe_sub_stdin[1]);
cleanup_pipe_sub_stdin:
	close(_pipe_sub_stdout[0]);
	close(_pipe_sub_stdout[1]);
cleanup_pipe_sub_stdout:
	return 0;
}
