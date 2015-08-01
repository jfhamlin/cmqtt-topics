#!/bin/bash

# Auto generate single AllTests file for CuTest.
# Searches through all *.c files in the current directory.
# Prints to stdout.
# Author: Asim Jalis
# Date: 01/08/2003

FILES=$1

#if test $# -eq 0 ; then FILES=*.c ; else FILES=$* ; fi

echo '

/* This is auto-generated code. Edit at your own peril. */
#include <stdio.h>
#include "CuTest.h"

'

cat $FILES | grep '^void Test' | 
    sed -e 's/(.*$//' \
        -e 's/$/(CuTest*);/' \
        -e 's/^/extern /'

echo \
'

int RunAllTests(void) 
{
    CuString *output = CuStringNew();
    CuSuite* suite = CuSuiteNew();

'
cat $FILES | grep '^void Test' | 
    sed -e 's/^void //' \
        -e 's/(.*$//' \
        -e 's/^/    SUITE_ADD_TEST(suite, /' \
        -e 's/$/);/'

echo \
'
    CuSuiteRun(suite);
    CuSuiteSummary(suite, output);
    CuSuiteDetails(suite, output);
    printf("%s", output->buffer);
    return suite->failCount;
}

int main(int argc, char **argv)
{
    return RunAllTests();
}
'