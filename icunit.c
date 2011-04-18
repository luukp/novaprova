/* icunit.c - intercept CUnit assert failures in CUT */
#include "u4c_priv.h"

bool CU_assertImplementation(bool bValue,
			     unsigned int uiLine,
			     char strCondition[],
			     char strFile[],
			     char strFunction[] __attribute__((unused)),
			     bool bFatal __attribute__((unused)))
{
//     fprintf(stderr, "    %s at %s:%u\n", strCondition, strFile, uiLine);
    if (!bValue)
	__u4c_fail(strCondition, strFile, uiLine, strFunction);
    return true;
}

