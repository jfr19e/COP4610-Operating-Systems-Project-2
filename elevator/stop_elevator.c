#include <linux/linkage.h>
#include <linux/kernel.h>
#include <linux/module.h>

/* System call stub */
int (*STUB_stop_elevator)(void) = NULL;
EXPORT_SYMBOL(STUB_stop_elevator);

/* System call wrapper */
asmlinkage int sys_stop_elevator(void)
{
	if (STUB_stop_elevator != NULL)
		return STUB_stop_elevator();
	else
		return -ENOSYS;
}
