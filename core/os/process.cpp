#include "process.h"

#if defined(_WIN32)

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <string>

namespace {

std::wstring _widen(const String &p_text) {
	if (p_text.empty()) {
		return std::wstring();
	}
	const int length = MultiByteToWideChar(CP_UTF8, 0, p_text.data(), (int)p_text.size(), nullptr, 0);
	std::wstring wide((size_t)length, L'\0');
	MultiByteToWideChar(CP_UTF8, 0, p_text.data(), (int)p_text.size(), wide.data(), length);
	return wide;
}

// Règles de la ligne de commande de Windows (CommandLineToArgvW) : guillemets autour de
// l'argument, barres obliques inverses doublées devant un guillemet.
void _append_argument(const std::wstring &p_argument, std::wstring &r_line) {
	if (!r_line.empty()) {
		r_line += L' ';
	}
	if (!p_argument.empty() && p_argument.find_first_of(L" \t\n\v\"") == std::wstring::npos) {
		r_line += p_argument;
		return;
	}
	r_line += L'"';
	for (size_t i = 0;; i++) {
		size_t backslashes = 0;
		while (i < p_argument.size() && p_argument[i] == L'\\') {
			i++;
			backslashes++;
		}
		if (i == p_argument.size()) {
			r_line.append(backslashes * 2, L'\\');
			break;
		}
		if (p_argument[i] == L'"') {
			r_line.append(backslashes * 2 + 1, L'\\');
		} else {
			r_line.append(backslashes, L'\\');
		}
		r_line += p_argument[i];
	}
	r_line += L'"';
}

// Lit ce qui est disponible sans bloquer. Renvoie false quand le tube est fermé.
bool _drain(HANDLE p_pipe, String *r_out) {
	while (true) {
		DWORD available = 0;
		if (!PeekNamedPipe(p_pipe, nullptr, 0, nullptr, &available, nullptr)) {
			return false;
		}
		if (available == 0) {
			return true;
		}
		char buffer[8192];
		DWORD read = 0;
		if (!ReadFile(p_pipe, buffer, (DWORD)sizeof(buffer), &read, nullptr) || read == 0) {
			return false;
		}
		if (r_out) {
			r_out->append(buffer, read);
		}
	}
}

} // namespace

bool Process::run(const String &p_program, const std::vector<String> &p_arguments, const String &p_stdin, String *r_output, String *r_errors, int *r_exit_code, const std::atomic<bool> *p_cancel, const String &p_working_dir) {
	SECURITY_ATTRIBUTES security = {};
	security.nLength = sizeof(security);
	security.bInheritHandle = TRUE;
	HANDLE in_read = nullptr, in_write = nullptr, out_read = nullptr, out_write = nullptr, err_read = nullptr, err_write = nullptr;
	if (!CreatePipe(&in_read, &in_write, &security, 0) || !CreatePipe(&out_read, &out_write, &security, 1 << 16) || !CreatePipe(&err_read, &err_write, &security, 1 << 16)) {
		return false;
	}
	// Seules les extrémités du programme lancé sont héritées.
	SetHandleInformation(in_write, HANDLE_FLAG_INHERIT, 0);
	SetHandleInformation(out_read, HANDLE_FLAG_INHERIT, 0);
	SetHandleInformation(err_read, HANDLE_FLAG_INHERIT, 0);

	std::wstring line;
	_append_argument(_widen(p_program), line);
	for (const String &argument : p_arguments) {
		_append_argument(_widen(argument), line);
	}
	STARTUPINFOW startup = {};
	startup.cb = sizeof(startup);
	startup.dwFlags = STARTF_USESTDHANDLES;
	startup.hStdInput = in_read;
	startup.hStdOutput = out_write;
	startup.hStdError = err_write;
	PROCESS_INFORMATION info = {};
	const std::wstring directory = _widen(p_working_dir);
	const BOOL started = CreateProcessW(nullptr, line.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW, nullptr, directory.empty() ? nullptr : directory.c_str(), &startup, &info);
	CloseHandle(in_read);
	CloseHandle(out_write);
	CloseHandle(err_write);
	if (!started) {
		CloseHandle(in_write);
		CloseHandle(out_read);
		CloseHandle(err_read);
		return false;
	}
	if (!p_stdin.empty()) {
		DWORD written = 0;
		WriteFile(in_write, p_stdin.data(), (DWORD)p_stdin.size(), &written, nullptr);
	}
	CloseHandle(in_write);

	while (true) {
		const bool out_open = _drain(out_read, r_output);
		const bool err_open = _drain(err_read, r_errors);
		if (p_cancel && p_cancel->load()) {
			TerminateProcess(info.hProcess, 1);
			break;
		}
		if (WaitForSingleObject(info.hProcess, (out_open || err_open) ? 15 : 0) == WAIT_OBJECT_0) {
			_drain(out_read, r_output);
			_drain(err_read, r_errors);
			break;
		}
		if (!out_open && !err_open) {
			WaitForSingleObject(info.hProcess, 50);
		}
	}
	WaitForSingleObject(info.hProcess, 5000);
	DWORD code = 1;
	GetExitCodeProcess(info.hProcess, &code);
	if (r_exit_code) {
		*r_exit_code = (int)code;
	}
	CloseHandle(info.hProcess);
	CloseHandle(info.hThread);
	CloseHandle(out_read);
	CloseHandle(err_read);
	return true;
}

#else // POSIX

#include <cerrno>
#include <csignal>
#include <fcntl.h>
#include <poll.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

bool Process::run(const String &p_program, const std::vector<String> &p_arguments, const String &p_stdin, String *r_output, String *r_errors, int *r_exit_code, const std::atomic<bool> *p_cancel, const String &p_working_dir) {
	int in_pipe[2], out_pipe[2], err_pipe[2], status_pipe[2];
	if (pipe(in_pipe) != 0) {
		return false;
	}
	if (pipe(out_pipe) != 0) {
		close(in_pipe[0]);
		close(in_pipe[1]);
		return false;
	}
	if (pipe(err_pipe) != 0 || pipe(status_pipe) != 0) {
		close(in_pipe[0]);
		close(in_pipe[1]);
		close(out_pipe[0]);
		close(out_pipe[1]);
		return false;
	}
	// Ce tube se ferme tout seul si exec réussit ; sinon l'enfant y écrit errno.
	fcntl(status_pipe[1], F_SETFD, FD_CLOEXEC);

	// Tout est préparé avant fork : l'enfant n'appelle que des fonctions sûres.
	std::vector<char *> argv;
	argv.push_back(const_cast<char *>(p_program.c_str()));
	for (const String &argument : p_arguments) {
		argv.push_back(const_cast<char *>(argument.c_str()));
	}
	argv.push_back(nullptr);

	const pid_t pid = fork();
	if (pid < 0) {
		for (int fd : { in_pipe[0], in_pipe[1], out_pipe[0], out_pipe[1], err_pipe[0], err_pipe[1], status_pipe[0], status_pipe[1] }) {
			close(fd);
		}
		return false;
	}
	if (pid == 0) {
		dup2(in_pipe[0], STDIN_FILENO);
		dup2(out_pipe[1], STDOUT_FILENO);
		dup2(err_pipe[1], STDERR_FILENO);
		for (int fd : { in_pipe[0], in_pipe[1], out_pipe[0], out_pipe[1], err_pipe[0], err_pipe[1], status_pipe[0] }) {
			close(fd);
		}
		if (!p_working_dir.empty() && chdir(p_working_dir.c_str()) != 0) {
			const int error = errno;
			const ssize_t ignored = write(status_pipe[1], &error, sizeof(error));
			(void)ignored;
			_exit(127);
		}
		execvp(argv[0], argv.data());
		const int error = errno;
		const ssize_t ignored = write(status_pipe[1], &error, sizeof(error));
		(void)ignored;
		_exit(127);
	}
	close(in_pipe[0]);
	close(out_pipe[1]);
	close(err_pipe[1]);
	close(status_pipe[1]);

	int exec_error = 0;
	const bool exec_failed = read(status_pipe[0], &exec_error, sizeof(exec_error)) == (ssize_t)sizeof(exec_error);
	close(status_pipe[0]);

	if (!exec_failed && !p_stdin.empty()) {
		// Un programme qui ferme son entrée ne doit pas tuer l'éditeur (SIGPIPE).
		struct sigaction ignore = {}, previous = {};
		ignore.sa_handler = SIG_IGN;
		sigaction(SIGPIPE, &ignore, &previous);
		size_t sent = 0;
		while (sent < p_stdin.size()) {
			const ssize_t written = write(in_pipe[1], p_stdin.data() + sent, p_stdin.size() - sent);
			if (written <= 0) {
				if (written < 0 && errno == EINTR) {
					continue;
				}
				break;
			}
			sent += (size_t)written;
		}
		sigaction(SIGPIPE, &previous, nullptr);
	}
	close(in_pipe[1]);

	bool out_open = true, err_open = true, killed = false;
	while (out_open || err_open) {
		struct pollfd fds[2];
		int count = 0;
		int out_index = -1, err_index = -1;
		if (out_open) {
			out_index = count;
			fds[count++] = { out_pipe[0], POLLIN, 0 };
		}
		if (err_open) {
			err_index = count;
			fds[count++] = { err_pipe[0], POLLIN, 0 };
		}
		const int ready = poll(fds, (nfds_t)count, 100);
		if (p_cancel && p_cancel->load() && !killed) {
			kill(pid, SIGTERM);
			killed = true;
		}
		if (ready < 0 && errno != EINTR) {
			break;
		}
		if (ready <= 0) {
			continue;
		}
		char buffer[8192];
		if (out_index >= 0 && (fds[out_index].revents & (POLLIN | POLLHUP | POLLERR))) {
			const ssize_t got = read(out_pipe[0], buffer, sizeof(buffer));
			if (got > 0) {
				if (r_output) {
					r_output->append(buffer, (size_t)got);
				}
			} else if (got == 0 || errno != EINTR) {
				out_open = false;
			}
		}
		if (err_index >= 0 && (fds[err_index].revents & (POLLIN | POLLHUP | POLLERR))) {
			const ssize_t got = read(err_pipe[0], buffer, sizeof(buffer));
			if (got > 0) {
				if (r_errors) {
					r_errors->append(buffer, (size_t)got);
				}
			} else if (got == 0 || errno != EINTR) {
				err_open = false;
			}
		}
	}
	close(out_pipe[0]);
	close(err_pipe[0]);

	int status = 0;
	while (waitpid(pid, &status, 0) < 0 && errno == EINTR) {
	}
	if (exec_failed) {
		return false;
	}
	if (r_exit_code) {
		*r_exit_code = WIFEXITED(status) ? WEXITSTATUS(status) : 1;
	}
	return true;
}

#endif
