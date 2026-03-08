/*
 * shell.c — Patch-D: CanvasOS Shell with PixelCode Self-Hosting
 *
 * Provides:
 *   - Builtin commands (ps, kill, ls, cd, mkdir, rm, exit, det, timewarp, env)
 *   - PixelCode self-hosted utilities (echo, cat, info, hash, help)
 *   - Pipe execution (cmd_a | cmd_b)
 *   - Redirection (cmd > file, cmd >> file, cmd < file)
 *   - Environment variables ($VAR, VAR=value)
 *   - source command (execute script lines)
 */
#include "../include/canvasos_shell.h"
#include "../include/canvasos_utils.h"
#include "../include/canvasos_pixelcode.h"
#include "../include/canvasos_vm.h"
#include "../include/canvasos_pixel_loader.h"
#include "../include/canvasos_fd.h"
#include "../include/canvasos_signal.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>

/* ── Trim whitespace ─────────────────────────────────── */
static const char *skip_ws(const char *s) {
    while (*s && isspace((unsigned char)*s)) s++;
    return s;
}

static void trim_right(char *s) {
    int len = (int)strlen(s);
    while (len > 0 && isspace((unsigned char)s[len-1])) s[--len] = '\0';
}

/* ── Variable expansion ──────────────────────────────── */
static void shell_expand_vars(const Shell *sh, const char *input,
                              char *output, size_t cap) {
    size_t o = 0;
    for (size_t i = 0; input[i] && o < cap - 1; ) {
        if (input[i] == '$') {
            i++;
            char vname[SHELL_VAR_NAME_MAX] = {0};
            int vi = 0;
            while (input[i] && (isalnum((unsigned char)input[i]) || input[i] == '_')
                   && vi < SHELL_VAR_NAME_MAX - 1) {
                vname[vi++] = input[i++];
            }
            vname[vi] = '\0';
            const char *val = shell_get_var(sh, vname);
            if (val) {
                size_t vlen = strlen(val);
                if (o + vlen < cap) {
                    memcpy(output + o, val, vlen);
                    o += vlen;
                }
            }
        } else {
            output[o++] = input[i++];
        }
    }
    output[o] = '\0';
}

/* ── Builtin dispatch ────────────────────────────────── */
static int shell_exec_builtin(Shell *sh, EngineContext *ctx, const char *line) {
    char cmd[128] = {0};
    char arg[128] = {0};

    /* Split into command and argument */
    const char *p = skip_ws(line);
    int ci = 0;
    while (*p && !isspace((unsigned char)*p) && ci < 127) cmd[ci++] = *p++;
    cmd[ci] = '\0';
    p = skip_ws(p);
    if (*p) {
        strncpy(arg, p, 127);
        trim_right(arg);
    }

    /* ── PixelCode self-hosting dispatch ── */
    if (pxl_get_mode() == PXL_MODE_PIXELCODE &&
        pxl_find_utility(cmd) != PXL_UTIL_NONE) {
        return pxl_exec_utility(ctx, sh->pt, sh->pipes, cmd, arg);
    }

    /* ── Process management ── */
    if (strcmp(cmd, "ps") == 0)
        return cmd_ps(sh->pt);

    if (strcmp(cmd, "kill") == 0) {
        unsigned pid = 0, sig = SIG_KILL;
        if (sscanf(arg, "%u %u", &pid, &sig) >= 1)
            return cmd_kill(sh->pt, pid, (uint8_t)sig);
        printf("  usage: kill <pid> [signal]\n");
        return -1;
    }

    /* ── File system ── */
    if (strcmp(cmd, "ls") == 0)
        return cmd_ls(ctx, &sh->pathctx, strlen(arg) > 0 ? arg : ".");

    if (strcmp(cmd, "cd") == 0) {
        if (strlen(arg) == 0) return 0;
        return cmd_cd(&sh->pathctx, ctx, arg);
    }

    if (strcmp(cmd, "mkdir") == 0) {
        if (strlen(arg) == 0) { printf("  usage: mkdir <name>\n"); return -1; }
        return cmd_mkdir(ctx, &sh->pathctx, arg);
    }

    if (strcmp(cmd, "rm") == 0) {
        if (strlen(arg) == 0) { printf("  usage: rm <path>\n"); return -1; }
        return cmd_rm(ctx, &sh->pathctx, arg);
    }

    if (strcmp(cmd, "cat") == 0)
        return cmd_cat(ctx, &sh->pathctx, sh->pathctx.pid, arg);

    /* ── Output ── */
    if (strcmp(cmd, "echo") == 0)
        return cmd_echo(sh->pathctx.pid, arg);

    /* ── System ── */
    if (strcmp(cmd, "hash") == 0)
        return cmd_hash(ctx);

    if (strcmp(cmd, "info") == 0)
        return cmd_info(ctx, sh->pt);

    /* ── Determinism ── */
    if (strcmp(cmd, "det") == 0) {
        if (strcmp(arg, "on") == 0) {
            det_set_all(&sh->detmode, true);
            det_log_change(ctx, &sh->detmode);
            printf("  determinism: ON\n");
            return 0;
        }
        if (strcmp(arg, "off") == 0) {
            det_set_all(&sh->detmode, false);
            det_log_change(ctx, &sh->detmode);
            printf("  determinism: OFF\n");
            return 0;
        }
        printf("  det %s\n", det_is_deterministic(&sh->detmode) ? "ON" : "OFF");
        return 0;
    }

    /* ── Time warp (now via timeline) ── */
    if (strcmp(cmd, "timewarp") == 0) {
        unsigned tick = 0;
        if (strcmp(arg, "resume") == 0)
            return timewarp_resume(&sh->timewarp, ctx);
        if (sscanf(arg, "%u", &tick) == 1)
            return timeline_timewarp(&sh->timeline, ctx, tick);
        printf("  usage: timewarp <tick> | timewarp resume\n");
        return -1;
    }

    /* ── Snapshot ── */
    if (strcmp(cmd, "snapshot") == 0) {
        const char *name = strlen(arg) > 0 ? arg : NULL;
        int id = timeline_snapshot(&sh->timeline, ctx, name);
        if (id >= 0)
            printf("  snapshot #%d at tick %u\n", id, ctx->tick);
        else
            printf("  snapshot failed\n");
        return id >= 0 ? 0 : -1;
    }

    /* ── Branch ── */
    if (strcmp(cmd, "branch") == 0) {
        char subcmd[32] = {0}, barg[64] = {0};
        sscanf(arg, "%31s %63[^\n]", subcmd, barg);

        if (strcmp(subcmd, "create") == 0) {
            int bid = timeline_branch_create(&sh->timeline, ctx,
                                             strlen(barg) > 0 ? barg : "branch");
            if (bid >= 0)
                printf("  branch #%d created at tick %u\n", bid, ctx->tick);
            return bid >= 0 ? 0 : -1;
        }
        if (strcmp(subcmd, "list") == 0)
            return timeline_branch_list(&sh->timeline);
        if (strcmp(subcmd, "switch") == 0) {
            unsigned bid = 0;
            if (sscanf(barg, "%u", &bid) == 1)
                return timeline_branch_switch(&sh->timeline, ctx, bid);
            printf("  usage: branch switch <id>\n");
            return -1;
        }
        printf("  usage: branch create|list|switch\n");
        return -1;
    }

    /* ── Merge ── */
    if (strcmp(cmd, "merge") == 0) {
        unsigned a = 0, b = 0;
        if (sscanf(arg, "%u %u", &a, &b) == 2) {
            MergeResult mr;
            int rc = timeline_merge(&sh->timeline, ctx, a, b, &mr);
            if (rc == 0) {
                if (mr.has_conflict)
                    printf("  merged with %u conflicts\n", mr.conflict_count);
                else
                    printf("  merged cleanly (%u cells)\n", mr.applied_count);
            }
            return rc;
        }
        printf("  usage: merge <branch_a> <branch_b>\n");
        return -1;
    }

    /* ── Timeline ── */
    if (strcmp(cmd, "timeline") == 0)
        return timeline_show(&sh->timeline, ctx);

    /* ── Variable assignment ── */
    if (strchr(line, '=') && !strchr(line, ' ')) {
        char vname[SHELL_VAR_NAME_MAX] = {0};
        char vval[SHELL_VAR_VAL_MAX] = {0};
        if (sscanf(line, "%15[^=]=%63s", vname, vval) >= 1) {
            shell_set_var(sh, vname, vval);
            return 0;
        }
    }

    /* ── env: show all vars ── */
    if (strcmp(cmd, "env") == 0) {
        for (int i = 0; i < sh->var_count; i++)
            printf("  %s=%s\n", sh->vars[i].name, sh->vars[i].value);
        return 0;
    }

    /* ── source: execute lines from buffer ── */
    if (strcmp(cmd, "source") == 0) {
        /* In CanvasOS, source reads PixelCode lines from a path.
         * For now, we support inline mini-scripts separated by ';' */
        if (strlen(arg) == 0) { printf("  usage: source <path>\n"); return -1; }
        /* Execute arg as-is (single line source for testing) */
        return shell_exec_line(sh, ctx, arg);
    }

    /* ── help ── */
    if (strcmp(cmd, "help") == 0) {
        printf("  CanvasOS Shell — Builtins:\n");
        printf("    ps, kill, ls, cd, mkdir, rm, cat, echo\n");
        printf("    hash, info, det, timewarp, env, source, help, exit\n");
        printf("    VAR=value, $VAR expansion\n");
        printf("    cmd_a | cmd_b  (pipe)\n");
        printf("    cmd > file     (redirect)\n");
        return 0;
    }

    /* ── exit ── */
    if (strcmp(cmd, "exit") == 0) {
        sh->running = false;
        return 0;
    }

    return -2; /* not a builtin */
}

/* ── Init ────────────────────────────────────────────── */
void shell_init(Shell *sh, ProcTable *pt, PipeTable *pipes, EngineContext *ctx) {
    memset(sh, 0, sizeof(*sh));
    sh->pt    = pt;
    sh->pipes = pipes;
    pathctx_init(&sh->pathctx, PID_SHELL, (FsKey){0, 0});
    timewarp_init(&sh->timewarp);
    det_init(&sh->detmode);
    timeline_init(&sh->timeline, ctx);
    sh->running = true;

    /* Default environment variables */
    shell_set_var(sh, "HOME", "/");
    shell_set_var(sh, "USER", "root");
    shell_set_var(sh, "SHELL", "canvasos");
    (void)ctx;
}

/* ── Variables ───────────────────────────────────────── */
void shell_set_var(Shell *sh, const char *name, const char *value) {
    if (!sh || !name || !value) return;
    for (int i = 0; i < sh->var_count; ++i) {
        if (strcmp(sh->vars[i].name, name) == 0) {
            snprintf(sh->vars[i].value, sizeof(sh->vars[i].value), "%s", value);
            return;
        }
    }
    if (sh->var_count < SHELL_VAR_MAX) {
        snprintf(sh->vars[sh->var_count].name,
                 sizeof(sh->vars[sh->var_count].name), "%s", name);
        snprintf(sh->vars[sh->var_count].value,
                 sizeof(sh->vars[sh->var_count].value), "%s", value);
        sh->var_count++;
    }
}

const char *shell_get_var(const Shell *sh, const char *name) {
    if (!sh || !name) return NULL;
    for (int i = 0; i < sh->var_count; ++i)
        if (strcmp(sh->vars[i].name, name) == 0) return sh->vars[i].value;
    return NULL;
}

/* ── Pipe execution ──────────────────────────────────── */
int shell_exec_pipe(Shell *sh, EngineContext *ctx, const char *line) {
    char left[256] = {0}, right[256] = {0};
    /* Find first unquoted pipe symbol */
    const char *pipe_pos = strchr(line, '|');
    if (!pipe_pos) return -1;

    size_t llen = (size_t)(pipe_pos - line);
    if (llen >= sizeof(left)) llen = sizeof(left) - 1;
    memcpy(left, line, llen);
    left[llen] = '\0';
    trim_right(left);

    strncpy(right, pipe_pos + 1, sizeof(right) - 1);
    trim_right(right);

    /* Create child processes */
    int pid_l = proc_spawn(sh->pt, PID_SHELL, 0, 100, 1);
    int pid_r = proc_spawn(sh->pt, PID_SHELL, 0, 100, 1);
    if (pid_l < 0 || pid_r < 0) return -1;

    /* Create pipe */
    int pipe_id = pipe_create(sh->pipes, ctx,
                              (uint32_t)pid_l, (uint32_t)pid_r);
    if (pipe_id < 0) return -1;

    /* Execute left side — capture stdout into pipe */
    fd_stdout_clear();
    shell_exec_line(sh, ctx, left);
    uint8_t cap_buf[1024];
    uint16_t cap_len = fd_stdout_get(cap_buf, sizeof(cap_buf));
    if (cap_len > 0)
        pipe_write(sh->pipes, ctx, pipe_id, cap_buf, cap_len);

    /* Execute right side — feed pipe data as context */
    /* For now, read pipe into stdout for the right command */
    uint8_t rbuf[1024] = {0};
    int rlen = pipe_read(sh->pipes, ctx, pipe_id, rbuf, sizeof(rbuf));
    (void)rlen; /* right command uses its own input */

    shell_exec_line(sh, ctx, right);

    /* Cleanup */
    pipe_close(sh->pipes, ctx, pipe_id);
    proc_exit(sh->pt, (uint32_t)pid_l, 0);
    proc_exit(sh->pt, (uint32_t)pid_r, 0);
    uint8_t st;
    proc_wait(sh->pt, PID_SHELL, &st);
    proc_wait(sh->pt, PID_SHELL, &st);

    return 0;
}

/* ── Redirection ─────────────────────────────────────── */
int shell_exec_redir(Shell *sh, EngineContext *ctx, const char *line) {
    char cmd_part[256] = {0};
    char op[4]  = {0};
    char path_part[128] = {0};

    /* Parse: "cmd >path", "cmd >>path", "cmd <path" */
    const char *gt = strchr(line, '>');
    const char *lt = strchr(line, '<');
    const char *redir_pos = gt ? gt : lt;
    if (!redir_pos) return -1;

    size_t cmd_len = (size_t)(redir_pos - line);
    if (cmd_len >= sizeof(cmd_part)) cmd_len = sizeof(cmd_part) - 1;
    memcpy(cmd_part, line, cmd_len);
    trim_right(cmd_part);

    if (gt) {
        if (gt[1] == '>') {
            strncpy(op, ">>", 3);
            strncpy(path_part, skip_ws(gt + 2), sizeof(path_part) - 1);
        } else {
            strncpy(op, ">", 2);
            strncpy(path_part, skip_ws(gt + 1), sizeof(path_part) - 1);
        }
    } else {
        strncpy(op, "<", 2);
        strncpy(path_part, skip_ws(lt + 1), sizeof(path_part) - 1);
    }
    trim_right(path_part);

    if (strcmp(op, ">") == 0 || strcmp(op, ">>") == 0) {
        /* Output redirection: execute command, capture stdout to file path */
        fd_stdout_clear();
        shell_exec_line(sh, ctx, cmd_part);
        uint8_t buf[4096];
        uint16_t n = fd_stdout_get(buf, sizeof(buf));

        /* Open file for writing */
        uint8_t flags = O_WRITE | O_CREATE;
        if (strcmp(op, ">>") == 0) flags |= O_APPEND;
        int fd = fd_open(ctx, sh->pathctx.pid, path_part, flags);
        if (fd >= 0 && n > 0) {
            fd_write(ctx, sh->pathctx.pid, fd, buf, n);
            fd_close(ctx, sh->pathctx.pid, fd);
        }
        return 0;
    }

    if (strcmp(op, "<") == 0) {
        /* Input redirection: open file, read content, pass to command stdin */
        int fd = fd_open(ctx, sh->pathctx.pid, path_part, O_READ);
        if (fd < 0) {
            printf("  shell: cannot open %s\n", path_part);
            return -1;
        }
        /* For now, just execute the command (stdin redirection is complex) */
        fd_close(ctx, sh->pathctx.pid, fd);
        return shell_exec_line(sh, ctx, cmd_part);
    }

    return -1;
}

/* ── Main dispatch ───────────────────────────────────── */
int shell_exec_line(Shell *sh, EngineContext *ctx, const char *line) {
    if (!sh || !ctx || !line) return -1;

    /* Skip empty lines and comments */
    const char *p = skip_ws(line);
    if (*p == '\0' || *p == '#') return 0;

    /* Expand variables */
    char expanded[512];
    shell_expand_vars(sh, p, expanded, sizeof(expanded));
    p = expanded;

    /* Handle semicolons: execute each part sequentially */
    if (strchr(p, ';')) {
        char buf[512];
        strncpy(buf, p, sizeof(buf) - 1);
        buf[sizeof(buf) - 1] = '\0';
        int last_rc = 0;
        char *rest = buf;
        char *seg;
        while ((seg = strsep(&rest, ";")) != NULL) {
            const char *trimmed = seg;
            while (*trimmed && isspace((unsigned char)*trimmed)) trimmed++;
            if (*trimmed == '\0') continue;
            /* Execute segment directly — no recursive shell_exec_line */
            char seg_expanded[512];
            shell_expand_vars(sh, trimmed, seg_expanded, sizeof(seg_expanded));
            const char *sp = seg_expanded;
            while (*sp && isspace((unsigned char)*sp)) sp++;
            if (*sp == '\0' || *sp == '#') continue;
            if (strchr(sp, '|'))
                last_rc = shell_exec_pipe(sh, ctx, sp);
            else if (strchr(sp, '>') || strchr(sp, '<'))
                last_rc = shell_exec_redir(sh, ctx, sp);
            else {
                int brc = shell_exec_builtin(sh, ctx, sp);
                if (brc == -2) {
                    PxState px; VmState vm;
                    pxstate_init(&px);
                    vm_init(&vm, ORIGIN_X, ORIGIN_Y, PID_SHELL);
                    last_rc = px_exec_line(ctx, &px, &vm, sp);
                } else {
                    last_rc = brc;
                }
            }
        }
        return last_rc;
    }

    /* Pipe */
    if (strchr(p, '|'))
        return shell_exec_pipe(sh, ctx, p);

    /* Redirection */
    if (strchr(p, '>') || strchr(p, '<'))
        return shell_exec_redir(sh, ctx, p);

    /* Builtin */
    int rc = shell_exec_builtin(sh, ctx, p);
    if (rc != -2) return rc;

    /* Fallback: PixelCode one-line execution */
    PxState px;
    VmState vm;
    pxstate_init(&px);
    vm_init(&vm, ORIGIN_X, ORIGIN_Y, PID_SHELL);
    return px_exec_line(ctx, &px, &vm, p);
}
