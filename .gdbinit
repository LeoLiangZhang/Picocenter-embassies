define pico_symbols
  shell if [ -a addsyms ] ; then rm addsyms ; else echo "addsyms does not exist." ; fi;
  shell toolchains/linux_elf/scripts/debuggershelper.py
  source addsyms
end

# monitor child processes
#set detach-on-fork off

