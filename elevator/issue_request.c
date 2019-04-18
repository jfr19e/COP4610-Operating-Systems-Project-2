#include <linux/linkage.h>
#include <linux/kernel.h>
#include <linux/module.h>

/* System call stub */
int (*STUB_issue_request)(int, int, int) = NULL;
EXPORT_SYMBOL(STUB_issue_request);

/* System call wrapper */
asmlinkage int sys_issue_request(int p_type, int start_floor,
								 int dest_floor)
{
	if (STUB_issue_request != NULL)
		return STUB_issue_request(p_type, start_floor, dest_floor);
	else
		return -ENOSYS;
}
