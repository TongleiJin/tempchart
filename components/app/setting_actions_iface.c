#include "setting_actions_iface.h"

static const setting_actions_ops_t *s_ops;

void setting_actions_register(const setting_actions_ops_t *ops)
{
    s_ops = ops;
}

const setting_actions_ops_t *setting_actions_get(void)
{
    return s_ops;
}
