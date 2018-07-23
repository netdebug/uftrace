#include <inttypes.h>
#include <unistd.h>
#include <argp.h>

#include "uftrace.h"
#include "version.h"
#include "utils/utils.h"
#include "utils/symbol.h"
#include "utils/dwarf.h"

/* needs to print session info with symbol */
static bool needs_session;

/* output of --version option (generated by argp runtime) */
const char *argp_program_version = "symbols " UFTRACE_VERSION;

static struct argp_option symbols_options[] = {
	{ "data", 'd', "DATA", 0, "Use this DATA instead of uftrace.data" },
	{ 0 }
};

struct symbols_opts {
	char *dirname;
};

/* just to prevent linker failure */
int arch_register_index(char *reg)
{
	return -1;
}

static error_t parse_option(int key, char *arg, struct argp_state *state)
{
	struct symbols_opts *opts = state->input;

	switch (key) {
	case 'd':
		opts->dirname = xstrdup(arg);
		break;

	case ARGP_KEY_NO_ARGS:
	case ARGP_KEY_END:
		break;

	default:
		return ARGP_ERR_UNKNOWN;
	}

	return 0;
}

static int print_session_symbol(struct uftrace_session *s, void *arg)
{
	uint64_t addr = *(uint64_t *)arg;
	struct sym *sym;
	struct debug_location *dloc;

	sym = find_symtabs(&s->symtabs, addr);
	if (sym == NULL)
		sym = session_find_dlsym(s, ~0ULL, addr);

	if (sym == NULL)
		return 0;

	printf("  %s", sym->name);

	dloc = find_file_line(&s->symtabs, addr);
	if (dloc && dloc->file)
		printf(" (at %s:%d)", dloc->file->name, dloc->line);

	if (needs_session)
		printf(" [in %.*s]", SESSION_ID_LEN, s->sid);

	return 0;
}

static int read_session(struct uftrace_session_link *link, char *dirname)
{
	FILE *fp;
	char *fname = NULL;
	char *line = NULL;
	size_t sz = 0;
	long sec, nsec;
	struct uftrace_msg_task tmsg;
	struct uftrace_msg_sess smsg;
	struct uftrace_msg_dlopen dlop;
	char *exename, *pos;
	int count = 0;

	xasprintf(&fname, "%s/%s", dirname, "task.txt");

	fp = fopen(fname, "r");
	if (fp == NULL) {
		free(fname);
		return -1;
	}

	pr_dbg("reading %s file\n", fname);
	while (getline(&line, &sz, fp) >= 0) {
		if (!strncmp(line, "TASK", 4)) {
			sscanf(line + 5, "timestamp=%lu.%lu tid=%d pid=%d",
			       &sec, &nsec, &tmsg.tid, &tmsg.pid);

			tmsg.time = (uint64_t)sec * NSEC_PER_SEC + nsec;
			create_task(link, &tmsg, false, true);
		}
		else if (!strncmp(line, "FORK", 4)) {
			sscanf(line + 5, "timestamp=%lu.%lu pid=%d ppid=%d",
			       &sec, &nsec, &tmsg.tid, &tmsg.pid);

			tmsg.time = (uint64_t)sec * NSEC_PER_SEC + nsec;
			create_task(link, &tmsg, true, true);
		}
		else if (!strncmp(line, "SESS", 4)) {
			sscanf(line + 5, "timestamp=%lu.%lu %*[^i]id=%d sid=%s",
			       &sec, &nsec, &smsg.task.pid, (char *)&smsg.sid);

			// Get the execname
			pos = strstr(line, "exename=");
			if (pos == NULL)
				pr_err_ns("invalid task.txt format");
			exename = pos + 8 + 1;  // skip double-quote
			pos = strrchr(exename, '\"');
			if (pos)
				*pos = '\0';

			smsg.task.tid = smsg.task.pid;
			smsg.task.time = (uint64_t)sec * NSEC_PER_SEC + nsec;
			smsg.namelen = strlen(exename);

			create_session(link, &smsg, dirname, exename, true);
			count++;
		}
		else if (!strncmp(line, "DLOP", 4)) {
			struct uftrace_session *s;

			sscanf(line + 5, "timestamp=%lu.%lu tid=%d sid=%s base=%"PRIx64,
			       &sec, &nsec, &dlop.task.tid, (char *)&dlop.sid,
			       &dlop.base_addr);

			pos = strstr(line, "libname=");
			if (pos == NULL)
				pr_err_ns("invalid task.txt format");
			exename = pos + 8 + 1;  // skip double-quote
			pos = strrchr(exename, '\"');
			if (pos)
				*pos = '\0';

			dlop.task.pid = dlop.task.tid;
			dlop.task.time = (uint64_t)sec * NSEC_PER_SEC + nsec;
			dlop.namelen = strlen(exename);

			s = get_session_from_sid(link, dlop.sid);
			session_add_dlopen(s, dlop.task.time,
					   dlop.base_addr, exename);
		}
	}

	if (count > 1)
		needs_session = true;

	fclose(fp);
	free(fname);
	return 0;
}

int main(int argc, char *argv[])
{
	uint64_t addr;
	struct symbols_opts opts = {
		.dirname = UFTRACE_DIR_NAME,
	};
	struct uftrace_session_link link = {
		.root  = RB_ROOT,
		.tasks = RB_ROOT,
	};
	struct argp argp = {
		.options = symbols_options,
		.parser = parse_option,
		.args_doc = "[options]",
		.doc = "symbols -- read address and convert to symbol",
	};

	argp_parse(&argp, argc, argv, ARGP_IN_ORDER, NULL, &opts);

retry:
	if (read_session(&link, opts.dirname) < 0) {
		if (!strcmp(opts.dirname, UFTRACE_DIR_NAME)) {
			opts.dirname = ".";
			goto retry;
		}

		printf("read session failed\n");
		return -1;
	}

	while (scanf("%"PRIx64, &addr) == 1) {
		printf("%"PRIx64":", addr);
		if (needs_session)
			putchar('\n');
		walk_sessions(&link, print_session_symbol, &addr);
		putchar('\n');
	}

	return 0;
}
