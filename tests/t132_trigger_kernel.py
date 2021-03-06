#!/usr/bin/env python

from runtest import TestBase
import os, re

# there was a problem applying depth filter if it contains kernel functions
class TestCase(TestBase):
    def __init__(self):
        TestBase.__init__(self, 'openclose', """
# DURATION    TID     FUNCTION
   0.714 us [ 4435] | __monstartup();
   0.349 us [ 4435] | __cxa_atexit();
            [ 4435] | main() {
            [ 4435] |   fopen() {
   6.413 us [ 4435] |     sys_open();
   7.037 us [ 4435] |   } /* fopen */
            [ 4435] |   fclose() {
   8.389 us [ 4435] |     sys_close();
   9.949 us [ 4435] |   } /* fclose */
  17.632 us [ 4435] | } /* main */
""")

    def pre(self):
        if os.geteuid() != 0:
            return TestBase.TEST_SKIP
        if os.path.exists('/.dockerenv'):
            return TestBase.TEST_SKIP

        return TestBase.TEST_SUCCESS

    def runcmd(self):
        # the -T option works on replay time and accept a regex
        # while -N option works on record time and accept a glob
        uftrace = TestBase.uftrace_cmd
        program = 't-' + self.name

        argument  = '-K3'
        argument += ' -T do_syscall_64@kernel,depth=1'
        argument += ' -T ^sys_@kernel,depth=1'
        argument += ' -N exit_to_usermode_loop@kernel'
        argument += ' -N _*do_page_fault@kernel'

        return '%s %s %s' % (uftrace, argument, program)

    def fixup(self, cflags, result):
        uname = os.uname()

        # Linux v4.17 (x86_64) changed syscall routines
        major, minor, release = uname[2].split('.')
        if uname[0] == 'Linux' and uname[4] == 'x86_64' and \
           int(major) >= 4 and int(minor) >= 17:
            return re.sub('sys_[a-zA-Z0-9_]+', 'do_syscall_64', result)
        else:
            return result.replace('sys_open', 'sys_openat')
