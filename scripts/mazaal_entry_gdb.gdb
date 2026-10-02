# Headless Fortress of Winds boss-entry crash repro (issue #162).
# Usage (run TWICE from build/pc — the first run's area-change autosave is
# what the second run restores):
#   cd build/pc && gdb -batch -x ../../scripts/mazaal_entry_gdb.gdb ./tmc_pc
#   gdb -batch -x ../../scripts/mazaal_entry_gdb.gdb ./tmc_pc
#
# Reproduces the v0.7.0..v0.8.0 SIGSEGV in mazaalHead.c sub_08033FFC case 1:
# warps into the Mazaal arena (the area-change autosave captures the entry in
# the ring, slots 5-7), then restores the ring — the savestate re-entry path
# wires the bracelets (gRoomTransition.field_0x38 != 0) and fires the arm-wake
# broadcast that read a bracelet's 4-byte EntityRef slot as an 8-byte pointer.
# Fixed builds run clean: "==case1 slots: unk74=N unk78=N==" with real pool
# slot indices, no signal. NOTE: whether the arm-wake re-fires depends on the
# exact moment the restored state captured — a mid-fight/arena autosave (e.g.
# from a real playthrough, like the issue #162 reporter's) reproduces
# reliably; a fresh synthetic entry may restart the intro instead.
# (Restoring a snapshot taken by the SAME process instead trips an unrelated,
# pre-existing savestate quirk first — stale entity hitbox in the R-interaction
# scan, CheckEntityPickup in port_gameplay_stubs.c — hence the two-run dance.)
set pagination off
set confirm off
set print thread-events off
set breakpoint pending on
set env TMC_REPRO_NPC_TALK=1
set env TMC_REPRO_NPC_TALK_WARP=0x58,0x16,0xb8,0xa0,1
set env TMC_REPRO_MASH_A=1
set env TMC_AUTOPLAY=1
set env SDL_VIDEODRIVER=dummy
set env SDL_AUDIODRIVER=dummy

# Walk the auto-save ring (whichever slot the previous run's arena entry
# landed in); the newest arena state wins.
set $n = 0
break GameMain_Update
commands 1
  silent
  set $n = $n + 1
  if $n == 700
    print Port_QuickSave_LoadSlot(5)
  end
  if $n == 760
    print Port_QuickSave_LoadSlot(6)
  end
  if $n == 820
    print Port_QuickSave_LoadSlot(7)
  end
  continue
end

# Arm-wake broadcast (the #162 crash site).
break sub_08033FFC if ((Entity*)this)->subAction == 1
commands 2
  printf "==case1 slots: unk74=%d unk78=%d==\n", (int)this->unk_74.entity, (int)this->unk_78.entity
  continue
end

run
echo \n==POST-RUN (backtrace only meaningful after a crash)==\n
backtrace 15
