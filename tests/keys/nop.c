#include <stic.h>

#include "../../src/engine/keys.h"
#include "../../src/modes/modes.h"

#include "builtin_keys.h"
#include "suite.h"

SETUP()
{
	assert_success(set_user_key(L"abc", L"", NORMAL_MODE));
}

TEST(nop_no_follow_ok)
{
	assert_false(IS_KEYS_RET_CODE(vle_keys_exec(L"abc")));
}

TEST(nop_followed_ok)
{
	last = 0;
	assert_false(IS_KEYS_RET_CODE(vle_keys_exec(L"abcj")));
	assert_int_equal(2, last);
}

/* vim: set tabstop=2 softtabstop=2 shiftwidth=2 noexpandtab cinoptions-=(0: */
/* vim: set cinoptions+=t0 filetype=c : */
