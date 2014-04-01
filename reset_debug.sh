#!/bin/bash
debug_dir=`date "+debug_%y%m%d%H%M"`
mkdir $debug_dir
mv addsyms debug_mmap  debug_perf  debug_stack_bases  debug_stderr  debug_strace $debug_dir
touch addsyms debug_mmap  debug_perf  debug_stack_bases  debug_stderr  debug_strace
