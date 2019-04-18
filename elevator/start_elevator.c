#include <linux/linkage.h>
#include <linux/kernel.h>
#include <linux/module.h>

/* System call stub */
int (*STUB_start_elevator)(void) = NULL;
EXPORT_SYMBOL(STUB_start_elevator);

/* System call wrapper */
asmlinkage int sys_start_elevator(void)
{
	if (STUB_start_elevator != NULL)
		return STUB_start_elevator();
	else
		return -ENOSYS;
}
