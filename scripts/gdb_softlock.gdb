set pagination off
set confirm off
set logging file /home/sian/tmc/gdb.log
set logging overwrite on
set logging redirect on
set logging enabled on

# Skip past dynamic linker noise during startup
handle SIGPIPE nostop noprint pass

# Watch every write to gPlayerState.controlMode. Each hit prints the
# new value plus a short backtrace, then auto-continues so the game
# stays interactive.
watch gPlayerState.controlMode
commands
silent
printf "[gdb-ctl] new=%u\n", gPlayerState.controlMode
bt 6
printf "---\n"
continue
end

# Same for gUI.controlMode — Subtask_FadeOut/FadeIn save/restore via this.
watch gUI.controlMode
commands
silent
printf "[gdb-uictl] new=%u\n", gUI.controlMode
bt 6
printf "---\n"
continue
end

run --no-audio
