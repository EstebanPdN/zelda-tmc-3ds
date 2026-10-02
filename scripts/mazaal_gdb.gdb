set pagination off
set confirm off
set print thread-events off
set breakpoint pending on
set env TMC_REPRO_MAZAAL=1

# Conditional breakpoint on CreateEnemy(MAZAAL_MACRO=0x37, form)
break CreateEnemy
condition 1 $rdi == 0x37
commands 1
  silent
  printf "\n==CreateEnemy(MAZAAL_MACRO=0x37, form=%d)==\n", (int)$rsi
  backtrace 10
  continue
end

# Every MazaalMacro tick (any type)
break MazaalMacro
commands 2
  silent
  printf "\n==MazaalMacro tick — entity=%p==\n", (void*)$rdi
  backtrace 6
  continue
end

run
