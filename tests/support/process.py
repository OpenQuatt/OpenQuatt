"""Run bounded test commands while owning their complete POSIX process group."""

from contextlib import ExitStack
import os
import signal
import subprocess
import sys


def _stop_and_reap(guardian, process):
    """Signal the owned group before reaping its reserved guardian PID.

    The command may already have been reaped by communicate(). The guardian
    holds its PID/group identity until signaling, so a reused command PID can
    never redirect cleanup to an unrelated group. Only direct children can be
    waited for portably; the OS reaps adopted descendants after they are killed.
    """
    while True:
        try:
            os.killpg(guardian.pid, signal.SIGKILL)
            break
        except ProcessLookupError:
            break
        except KeyboardInterrupt:
            # A second Ctrl-C must not interrupt delivery of the group signal.
            continue
    for child in (process, guardian):
        if child is None:
            continue
        while True:
            try:
                child.wait()
                break
            except KeyboardInterrupt:
                # Keep ownership until both direct children are reaped.
                continue


def run_process(args, *, timeout, check=False, capture_output=False, input=None, **kwargs):
    """Return a CompletedProcess with subprocess.run-compatible output/errors.

    Commands run without a shell in a dedicated POSIX process group. A direct
    guardian child reserves its group identity until cleanup has signaled the
    group, even after the actual command exits. The group is stopped before
    returning or raising, including descendants left by an exited wrapper.
    Windows process-tree handling is explicitly unsupported.
    """
    if os.name != "posix":
        raise NotImplementedError("run_process requires POSIX process groups (macOS or Linux)")
    if kwargs.pop("shell", False):
        raise ValueError("run_process does not allow shell=True; pass an argument list")
    if kwargs.pop("start_new_session", False):
        raise ValueError("run_process commands must remain in the guardian's session")
    if kwargs.pop("process_group", None) not in (None, -1):
        raise ValueError("run_process owns its process group")
    if input is not None:
        if kwargs.get("stdin") is not None:
            raise ValueError("stdin and input arguments may not both be used")
        kwargs["stdin"] = subprocess.PIPE
    if capture_output:
        if kwargs.get("stdout") is not None or kwargs.get("stderr") is not None:
            raise ValueError("stdout and stderr arguments may not be used with capture_output")
        kwargs["stdout"] = subprocess.PIPE
        kwargs["stderr"] = subprocess.PIPE

    # Both children inherit our session so the command can legally join the
    # guardian's group. Stdin is a lease: owner death closes it and the guardian
    # stops its own group. Never poll/wait the guardian before
    # killpg: an unreaped guardian reserves the identity even if it exits early.
    with subprocess.Popen(
        [sys.executable, "-c", (
            "import os, signal, sys; sys.stdin.buffer.read(); "
            "os.killpg(os.getpgrp(), signal.SIGKILL)"
        )],
        stdin=subprocess.PIPE, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
        process_group=0,
    ) as guardian, ExitStack() as commands:
        process = None
        try:
            process = commands.enter_context(subprocess.Popen(args, process_group=guardian.pid, **kwargs))
            stdout, stderr = process.communicate(input, timeout=timeout)
            if check and process.returncode:
                raise subprocess.CalledProcessError(process.returncode, args, output=stdout, stderr=stderr)
            return subprocess.CompletedProcess(args, process.returncode, stdout, stderr)
        finally:
            # Preserve TimeoutExpired's partial bytes, including in text mode;
            # do not decode truncated output again after stopping the group.
            _stop_and_reap(guardian, process)
