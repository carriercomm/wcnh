/* PennMUSH headers */
#include "copyrite.h"
#include "config.h"
#include <string.h>
#include "conf.h"
#include "externs.h"
#include "parse.h"
#include "confmagic.h"
#include "command.h"
#include "function.h"
#include "case.h"
#include "log.h"

/* System headers. */
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/select.h>

/* JSON server headers */
#include "json-config.h"
#include "json-net.h"
#include "json-call.h"
#include "json-private.h"

/* Communication failure error message. */
#define ERROR_COMM_FAILURE T("#-1 COMMUNICATION FAILURE")

/* Internal error message. */
#define ERROR_INTERNAL T("#-1 INTERNAL ERROR")

/* Handy macro for getting context. */
#define GET_INFO_VAR JSON_Server_Context *const info = json_server_info()

/* Handy macro for trying a protocol operation. All errors are fatal. */
#define TRY_PROTO(op) do { if (!(op)) goto failed; } while (0)

/*
 * Macro to copy an execution context frame. Multiply evaluates.
 *
 * TODO: If copying overhead becomes significant, may want to consider using a
 * flag to implement a lazy copying strategy. Allocation should still occur
 * from the stack, since that's still faster than trying to malloc() lazily.
 */
#define COPY_CONTEXT_FRAME(d,s) \
	do { \
		(d).executor = (s).executor; \
		(d).caller = (s).caller; \
		(d).enactor = (s).enactor; \
	\
		(d).pe_info = (s).pe_info; \
	} while (0)

/*
 * Context parameters.
 *
 * TODO: May want to define these properties in a table instead.
 */
#define CONTEXT_KEY_EXECUTOR "executor"
#define CONTEXT_KLEN_EXECUTOR 8

#define CONTEXT_KEY_CALLER "caller"
#define CONTEXT_KLEN_CALLER 6

#define CONTEXT_KEY_ENACTOR "enactor"
#define CONTEXT_KLEN_ENACTOR 7

/* Execution context frame. */
typedef struct JSON_Server_Frame_tag {
	dbref executor; /* the executor */
	dbref caller; /* the caller */
	dbref enactor; /* the enactor */

	NEW_PE_INFO *pe_info; /* NEW_PE_INFO structure */
} JSON_Server_Frame;

/* Execution context. */
typedef struct JSON_Server_Context_tag {
	JSON_Server server; /* connection state */
	JSON_Server_Frame current; /* current context frame */
} JSON_Server_Context;

/* Receives and dispatches incoming messages. */
static int json_server_dispatch(JSON_Server_Message *msg);

/* Calls a PennMUSH function. */
static int json_server_penn_call(JSON_Server_Message *msg);

/*
 * Get the JSON server instance.
 */
static JSON_Server_Context *
json_server_info(void)
{
	static int init = 0;
	static JSON_Server_Context info;

	if (!init) {
		init = 1;

		info.server.fd = -1;
	}

	return &info;
}

/*
 * Simple error logging facility.
 */
void
json_server_log(JSON_Server *server, const char *message, int code)
{
	/* Log error. */
	if (code) {
		fprintf(stderr, "JSON server[%d]: %s: %s\n",
		        server->fd, message, strerror(code));
	} else {
		fprintf(stderr, "JSON server[%d]: %s\n",
		        server->fd, message);
	}
}

/*
 * Shuts down JSON sever.
 */
void
json_server_shutdown(int reboot)
{
	GET_INFO_VAR;

	/* TODO: Preserve server across reboots? */
	json_server_stop(&info->server);
}

/*
 * Sets the input file descriptor.
 */
void
json_server_setfd(fd_set *input_set)
{
	GET_INFO_VAR;

	if (info->server.fd == -1) {
		return;
	}

	FD_SET(info->server.fd, input_set);
}

/*
 * Processes the input file descriptor if selected.
 *
 * Note that this method is only called from the event loop.
 */
void
json_server_issetfd(fd_set *input_set)
{
	GET_INFO_VAR;

	if (info->server.fd == -1) {
		return;
	}

	if (FD_ISSET(info->server.fd, input_set)) {
		/* TODO: Dispatch incoming request. */
		ssize_t len;

		len = read(info->server.fd,
		           info->server.in_buf, JSON_SERVER_BUFFER_SIZE);
		if (len < 1) {
			json_server_stop(&info->server);
		}
	}
}

/*
 * Administrative command.
 *
 * arg_left: (subcommand) (subcommand arguments)
 */
COMMAND(cmd_json_rpc)
{
	GET_INFO_VAR;

	const char *subcmd = arg_left;

	if (!*subcmd) {
		/* Report usage information. */
		notify(executor, "RPC: Commands: STATUS START STOP");
	} else if (strcasecmp(subcmd, "STATUS") == 0) {
		/* Report status information. */
		if (info->server.fd == -1) {
			notify(executor, "RPC: Disconnected.");
		} else {
			notify(executor, "RPC: Connected.");
		}
	} else if (strcasecmp(subcmd, "START") == 0) {
		/* Start the JSON server instance. */
		notify(executor, "RPC: Starting.");
		json_server_start(&info->server);
	} else if (strcasecmp(subcmd, "STOP") == 0) {
		/* Stop the JSON server instance. */
		notify(executor, "RPC: Stopping.");
		json_server_stop(&info->server);
	} else {
		/* Unknown command. */
		notify_format(executor, "RPC: Unknown command: %s", subcmd);
	}
}

/*
 * Initiates call from MUSHcode to JSON server.
 *
 * args[0]: function identifier
 * args[1 .. (nargs - 1)]: function arguments
 */
FUNCTION(fun_json_rpc)
{
	GET_INFO_VAR;

	JSON_Server_Frame old_frame;
	JSON_Server_Message msg;
	int ii;

	/* Restrict to wizards using Hard coded permission check. */
	if (!Wizard(executor)) {
		safe_str(T(e_perm), buff, bp);
		return;
	}

	/* Ensure the server is started. */
	json_server_start(&info->server);
	if (info->server.fd == -1) {
		safe_str(ERROR_COMM_FAILURE, buff, bp);
		return;
	}

	/* Save call state. */
	COPY_CONTEXT_FRAME(old_frame, info->current);

	/* Send request. */
	TRY_PROTO(json_server_start_request(&info->server,
	                                    args[0], arglens[0]));

	for (ii = 1; ii < nargs; ii++) {
		TRY_PROTO(json_server_add_param(&info->server,
	                                        args[ii], arglens[ii]));
	}

	info->current.executor = executor;
	TRY_PROTO(json_server_add_context(&info->server, "executor", 8,
	                                  unparse_dbref(executor), -1));

	info->current.caller = caller;
	TRY_PROTO(json_server_add_context(&info->server, "caller", 6,
	                                  unparse_dbref(caller), -1));

	info->current.enactor = enactor;
	TRY_PROTO(json_server_add_context(&info->server, "enactor", 7,
	                                  unparse_dbref(enactor), -1));

	info->current.pe_info = pe_info;

	TRY_PROTO(json_server_send_request(&info->server));

	/* Dispatch response. */
	json_server_message_init(&msg);

	if (!json_server_dispatch(&msg)) {
		goto failed;
	}

	if (msg.local_error) {
		/* Report local error. */
		json_server_message_clear(&msg);
		safe_str(ERROR_INTERNAL, buff, bp);
		goto success;
	}

	switch (msg.type) {
	case JSON_SERVER_MSG_RESULT:
		/* Report result response. */
		if (msg.message) {
			safe_str(msg.message, buff, bp);
		}
		break;

	case JSON_SERVER_MSG_ERROR:
		/* Report error response. */
		safe_format(buff, bp, T("#-1 %ld|%s"),
		            msg.error_code, msg.message);
		break;

	default:
		/* This shouldn't happen. */
		json_server_message_clear(&msg);
		goto failed;
	}

	json_server_message_clear(&msg);
	goto success;

failed:
	/* Recover from protocol failure. */
	json_server_stop(&info->server);
	safe_str(ERROR_COMM_FAILURE, buff, bp);

	/* FALLTHROUGH */
success:
	/* Restore call state. */
	COPY_CONTEXT_FRAME(info->current, old_frame);
}

/*
 * Receives and dispatches incoming messages.
 */
static int
json_server_dispatch(JSON_Server_Message *msg)
{
	GET_INFO_VAR;

	for (;;) {
		if (!json_server_receive(&info->server, msg)) {
			return 0;
		}

		switch (msg->type) {
		case JSON_SERVER_MSG_REQUEST:
			/* Dispatch recursive request. */
			if (!json_server_penn_call(msg)) {
				goto failed;
			}
			break;

		default:
			/* Return response. */
			return 1;
		}

		json_server_message_clear(msg);
	}

failed:
	json_server_message_clear(msg);
	return 0;
}

/*
 * Calls a PennMUSH soft code function in response to an incoming request. This
 * code is based on parse.c:process_expression(), specifically the lines around
 * the func_hash_lookup() call.
 */
static int
json_server_penn_call(JSON_Server_Message *msg)
{
	static char name[BUFFER_LEN];

	char buff[BUFFER_LEN], *buffp, **bp;
	char *sp, *tp;

	FUN *fp;
	int tmp;

	GET_INFO_VAR;

	/* Apologize to caller, we broke. */
	if (msg->local_error) {
		return json_server_send_error(&info->server, msg->local_error,
		                              ERROR_INTERNAL, -1);
	}

	/* Prepare result buffer. */
	buffp = buff;
	bp = &buffp;

	/* Find the function. Note that msg->message is null-terminated. */
	tp = name;

	for (sp = msg->message; *sp; sp++) {
		safe_chr(UPCASE(*sp), name, &tp);
	}

	*tp = '\0';

	fp = builtin_func_hash_lookup(name);
	if (!fp) {
		/* Function was not found. */
		safe_format(buff, bp, T("#-1 FUNCTION (%s) NOT FOUND"), name);
		return json_server_send_error(&info->server,
		                              JSON_SERVER_ERROR_METHOD,
		                              buff, buffp - buff);
	}

	/* Check that the number of arguments is valid. */
	tmp = (fp->maxargs < 0) ? -fp->maxargs : fp->maxargs;
	if (msg->param_nargs < fp->minargs || tmp < msg->param_nargs) {
		safe_format(buff, bp,
		            T("#-1 FUNCTION (%s) EXPECTS %d TO %d ARGUMENTS"),
		            fp->name, fp->minargs, tmp);
		return json_server_send_error(&info->server,
		                              JSON_SERVER_ERROR_PARAMS,
		                              buff, buffp - buff);
	}

	/* Call the function. Note that we skipped a lot of safety checks! */
	fp->where.fun(fp, buff, bp,
	              msg->param_nargs, msg->param_args, msg->param_arglens,
	              info->current.executor, info->current.caller,
	              info->current.enactor, fp->name, info->current.pe_info,
	              PE_DEFAULT);

	/* Return the result. */
	if (buffp == buff) {
		return json_server_send_result(&info->server, NULL, -1);
	} else {
		return json_server_send_result(&info->server,
		                               buff, buffp - buff);
	}
}

/*
 * Updates context parameters. Note that the previous context parameter values
 * are saved, so we do not need to do any special recovery for fatal errors.
 */
int
json_server_context_callback(const char *key, int klen,
                             const char *value, int vlen)
{
	/* TODO: Implement context callback. */
	/*fprintf(stderr, "DEBUG(%.*s=%.*s)\n", klen, key, vlen, value);*/
	return 1;
}