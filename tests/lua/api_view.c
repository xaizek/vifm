#include <stic.h>

#include <string.h> /* strdup() */

#include "../../src/cfg/config.h"
#include "../../src/engine/keys.h"
#include "../../src/engine/mode.h"
#include "../../src/int/file_magic.h"
#include "../../src/lua/vlua.h"
#include "../../src/modes/cmdline.h"
#include "../../src/modes/modes.h"
#include "../../src/modes/visual.h"
#include "../../src/modes/wk.h"
#include "../../src/ui/tabs.h"
#include "../../src/ui/ui.h"
#include "../../src/utils/dynarray.h"
#include "../../src/utils/str.h"
#include "../../src/filelist.h"

#include <test-utils.h>

#include "asserts.h"

static int has_mime_type_detection(void);
static int has_no_mime_type_detection(void);

static vlua_t *vlua;
static line_stats_t *stats;

SETUP_ONCE()
{
	stub_colmgr();

	stats = get_line_stats();
}

TEARDOWN_ONCE()
{
	curr_view = NULL;
	other_view = NULL;
}

SETUP()
{
	vlua = vlua_init();

	view_setup(&lwin);
	strcpy(lwin.curr_dir, "/lwin");
	lwin.list_rows = 2;
	lwin.list_pos = 1;
	lwin.top_line = 0;
	lwin.dir_entry = dynarray_cextend(NULL,
			lwin.list_rows*sizeof(*lwin.dir_entry));
	lwin.dir_entry[0].name = strdup("file0");
	lwin.dir_entry[0].origin = &lwin.curr_dir[0];
	lwin.dir_entry[1].name = strdup("file1");
	lwin.dir_entry[1].origin = &lwin.curr_dir[0];

	view_setup(&rwin);
	strcpy(rwin.curr_dir, "/rwin");
	rwin.list_rows = 1;
	rwin.list_pos = 0;
	rwin.top_line = 0;
	rwin.dir_entry = dynarray_cextend(NULL,
			rwin.list_rows*sizeof(*rwin.dir_entry));
	rwin.dir_entry[0].name = strdup("ofile0");
	rwin.dir_entry[0].origin = &rwin.curr_dir[0];

	curr_view = &lwin;
	other_view = &rwin;
}

TEARDOWN()
{
	view_teardown(&lwin);
	view_teardown(&rwin);

	vlua_finish(vlua);
}

TEST(vifmview_properties)
{
	GLUA_EQ(vlua, "2", "print(vifm.currview().entrycount)");
	GLUA_EQ(vlua, "2", "print(vifm.currview().currententry)");
	GLUA_EQ(vlua, "/lwin", "print(vifm.currview().cwd)");
}

TEST(vifmview_entry)
{
	GLUA_EQ(vlua, "nil", "print(vifm.currview():entry(0))");
	GLUA_EQ(vlua, "file0", "print(vifm.currview():entry(1).name)");
	GLUA_EQ(vlua, "file1", "print(vifm.currview():entry(2).name)");
	GLUA_EQ(vlua, "ofile0", "print(vifm.otherview():entry(1).name)");
	GLUA_EQ(vlua, "nil", "print(vifm.currview():entry(3))");
}

TEST(vifmview_cursor)
{
	GLUA_EQ(vlua, "2", "print(vifm.currview().cursor.pos)");
	GLUA_EQ(vlua, "file1", "print(vifm.currview().cursor:entry().name)");
	GLUA_EQ(vlua, "nil", "print(vifm.currview().cursor.nosuchfield)");
	GLUA_EQ(vlua, "", "vifm.currview().cursor.nosuchfield = 10");
}

TEST(vifmview_set_cursor_pos_in_normal_mode)
{
	GLUA_EQ(vlua, "", "vifm.currview().cursor.pos = 1");
	GLUA_EQ(vlua, "1", "print(vifm.currview().cursor.pos)");

	GLUA_EQ(vlua, "", "vifm.currview().cursor.pos = 10");
	GLUA_EQ(vlua, "2", "print(vifm.currview().cursor.pos)");

	GLUA_EQ(vlua, "", "vifm.currview().cursor.pos = 0");
	GLUA_EQ(vlua, "1", "print(vifm.currview().cursor.pos)");

	BLUA_ENDS(vlua,
			"bad argument #3 to 'newindex' (number has no integer representation)",
			"vifm.currview().cursor.pos = 1.5");
}

TEST(vifmview_set_cursor_pos_in_visual_mode)
{
	modes_init();
	opt_handlers_setup();

	modvis_enter(VS_NORMAL);

	GLUA_EQ(vlua, "", "vifm.currview().cursor.pos = 1");

	assert_true(vle_mode_is(VISUAL_MODE));
	assert_int_equal(0, lwin.list_pos);
	assert_true(lwin.dir_entry[0].selected);
	assert_true(lwin.dir_entry[1].selected);

	GLUA_EQ(vlua, "1", "print(vifm.currview().cursor.pos)");

	modvis_leave(/*save_msg=*/0, /*goto_top=*/1, /*clear_selection=*/1);

	(void)vle_keys_exec_timed_out(WK_C_c);
	vle_keys_reset();
	opt_handlers_teardown();
}

TEST(vifmview_set_cursor_pos_in_cmdline_mode)
{
	modes_init();
	opt_handlers_setup();

	modcline_enter(CLS_COMMAND, "");

	GLUA_EQ(vlua, "", "vifm.currview().cursor.pos = 1");

	assert_true(vle_mode_is(CMDLINE_MODE));
	assert_int_equal(0, lwin.list_pos);

	GLUA_EQ(vlua, "1", "print(vifm.currview().cursor.pos)");

	(void)vle_keys_exec_timed_out(WK_C_c);
	vle_keys_reset();
	opt_handlers_teardown();
}

TEST(vifmview_set_cursor_pos_during_incsearch_from_normal_mode)
{
	modes_init();
	opt_handlers_setup();
	conf_setup();
	cfg.inc_search = 1;

	(void)vle_keys_exec_timed_out(WK_SLASH);

	assert_true(vle_mode_is(CMDLINE_MODE));
	assert_int_equal(1, lwin.list_pos);
	assert_int_equal(0, stats->old_top);
	assert_int_equal(1, stats->old_pos);

	GLUA_EQ(vlua, "", "vifm.currview().cursor.pos = 1");

	assert_true(vle_mode_is(CMDLINE_MODE));
	assert_int_equal(0, lwin.list_pos);
	assert_int_equal(0, stats->old_top);
	assert_int_equal(0, stats->old_pos);

	(void)vle_keys_exec_timed_out(WK_C_c);
	cfg.inc_search = 0;
	conf_teardown();
	vle_keys_reset();
	opt_handlers_teardown();
}

TEST(vifmview_set_cursor_pos_during_incsearch_from_visual_mode)
{
	modes_init();
	opt_handlers_setup();
	conf_setup();
	cfg.inc_search = 1;

	(void)vle_keys_exec_timed_out(WK_v WK_SLASH);

	assert_true(vle_mode_is(CMDLINE_MODE));
	assert_true(vle_primary_mode_is(VISUAL_MODE));
	assert_int_equal(1, lwin.list_pos);
	assert_int_equal(0, stats->old_top);
	assert_int_equal(1, stats->old_pos);
	assert_false(lwin.dir_entry[0].selected);
	assert_true(lwin.dir_entry[1].selected);

	GLUA_EQ(vlua, "", "vifm.currview().cursor.pos = 1");

	assert_true(vle_mode_is(CMDLINE_MODE));
	assert_true(vle_primary_mode_is(VISUAL_MODE));
	assert_int_equal(0, lwin.list_pos);
	assert_int_equal(0, stats->old_top);
	assert_int_equal(0, stats->old_pos);
	assert_true(lwin.dir_entry[0].selected);
	assert_true(lwin.dir_entry[1].selected);

	(void)vle_keys_exec_timed_out(WK_C_c);
	(void)vle_keys_exec_timed_out(WK_C_c);
	cfg.inc_search = 0;
	conf_teardown();
	vle_keys_reset();
	opt_handlers_teardown();
}

TEST(vifmview_custom)
{
	GLUA_EQ(vlua, "nil", "print(vifm.currview().custom)");

	make_abs_path(lwin.curr_dir, sizeof(lwin.curr_dir), "", "", NULL);
	flist_custom_start(&lwin, "vifmview_custom");
	flist_custom_add(&lwin, TEST_DATA_PATH "/existing-files/a");
	assert_success(flist_custom_finish(&lwin, CV_REGULAR, /*allow_empty=*/0));

	GLUA_EQ(vlua, "vifmview_custom", "print(vifm.currview().custom.title)");
	GLUA_EQ(vlua, "custom", "print(vifm.currview().custom.type)");
}

TEST(vifmview_select)
{
	assert_false(lwin.dir_entry[0].selected);
	assert_false(lwin.dir_entry[1].selected);

	GLUA_EQ(vlua, "0",
			"print(vifm.currview():select({ indexes = { 0, 1.5, 10 } }))");
	assert_false(lwin.dir_entry[0].selected);
	assert_false(lwin.dir_entry[1].selected);

	GLUA_EQ(vlua, "1",
			"print(vifm.currview():select({ indexes = { 2 } }))");
	assert_false(lwin.dir_entry[0].selected);
	assert_true(lwin.dir_entry[1].selected);

	GLUA_EQ(vlua, "1",
			"print(vifm.currview():select({ indexes = { 1, 2 } }))");
	assert_true(lwin.dir_entry[0].selected);
	assert_true(lwin.dir_entry[1].selected);
}

TEST(vifmview_select_in_visual_mode)
{
	modes_init();
	opt_handlers_setup();

	/* Does nothing in visual non-amend mode. */

	modvis_enter(VS_NORMAL);

	assert_false(lwin.dir_entry[0].selected);
	assert_true(lwin.dir_entry[1].selected);

	GLUA_EQ(vlua, "0", "print(vifm.currview():select({ indexes = { 1 } }))");
	assert_false(lwin.dir_entry[0].selected);
	assert_true(lwin.dir_entry[1].selected);

	/* Selects in visual amend mode. */

	modvis_enter(VS_AMEND);

	assert_false(lwin.dir_entry[0].selected);
	assert_true(lwin.dir_entry[1].selected);

	GLUA_EQ(vlua, "1", "print(vifm.currview():select({ indexes = { 1 } }))");
	assert_true(lwin.dir_entry[0].selected);
	assert_true(lwin.dir_entry[1].selected);

	modvis_leave(/*save_msg=*/0, /*goto_top=*/1, /*clear_selection=*/1);

	(void)vle_keys_exec_timed_out(WK_C_c);
	vle_keys_reset();
	opt_handlers_teardown();
}

TEST(vifmview_unselect)
{
	lwin.dir_entry[0].selected = 1;
	lwin.dir_entry[1].selected = 1;

	GLUA_EQ(vlua, "0",
			"print(vifm.currview():unselect({ indexes = { 0, 1.5, 10 } }))");
	assert_true(lwin.dir_entry[0].selected);
	assert_true(lwin.dir_entry[1].selected);

	GLUA_EQ(vlua, "1",
			"print(vifm.currview():unselect({ indexes = { 2 } }))");
	assert_true(lwin.dir_entry[0].selected);
	assert_false(lwin.dir_entry[1].selected);

	GLUA_EQ(vlua, "0",
			"print(vifm.currview():unselect({ indexes = 1 }))");
	assert_true(lwin.dir_entry[0].selected);
	assert_false(lwin.dir_entry[1].selected);

	GLUA_EQ(vlua, "1",
			"print(vifm.currview():unselect({ indexes = { 1, 2 } }))");
	assert_false(lwin.dir_entry[0].selected);
	assert_false(lwin.dir_entry[1].selected);
}

TEST(vifmview_selected)
{
	lwin.dir_entry[0].selected = 1;
	lwin.dir_entry[1].selected = 1;

	GLUA_EQ(vlua, "", "selected = vifm.currview():selected()");
	GLUA_EQ(vlua, "file0", "print(selected().name)");
	GLUA_EQ(vlua, "file1", "print(selected().name)");
	GLUA_EQ(vlua, "nil", "print(selected())");

	lwin.dir_entry[0].selected = 1;
	lwin.dir_entry[1].selected = 0;

	GLUA_EQ(vlua, "", "selected = vifm.currview():selected()");
	GLUA_EQ(vlua, "file0", "print(selected().name)");
	GLUA_EQ(vlua, "nil", "print(selected())");

	lwin.dir_entry[0].selected = 0;
	lwin.dir_entry[1].selected = 1;

	GLUA_EQ(vlua, "", "selected = vifm.currview():selected()");
	GLUA_EQ(vlua, "file1", "print(selected().name)");
	GLUA_EQ(vlua, "nil", "print(selected())");

	lwin.dir_entry[0].selected = 0;
	lwin.dir_entry[1].selected = 0;

	GLUA_EQ(vlua, "", "selected = vifm.currview():selected()");
	GLUA_EQ(vlua, "nil", "print(selected())");
}

TEST(vifmview_entries)
{
	GLUA_EQ(vlua, "", "entries = vifm.currview():entries()");
	GLUA_EQ(vlua, "", "entry = entries()");
	GLUA_EQ(vlua, "file0", "print(entry.name)");
	GLUA_EQ(vlua, "", "entry = entries()");
	GLUA_EQ(vlua, "file1", "print(entry.name)");
	GLUA_EQ(vlua, "", "entry = entries()");
	GLUA_EQ(vlua, "nil", "print(entry)");
}

TEST(vifmview_focus)
{
	opt_handlers_setup();
	columns_setup_column(SK_BY_NAME);
	columns_setup_column(SK_BY_SIZE);

	GLUA_EQ(vlua, "", "curr_tab1 = vifm.currview()");
	int curr_id = curr_view->id;

	tabs_new(NULL, NULL);
	GLUA_EQ(vlua, "", "other_tab2 = vifm.otherview()");
	int other_id = other_view->id;

	GLUA_EQ(vlua, "true", "print(curr_tab1:focus())");
	assert_int_equal(curr_view->id, curr_id);

	GLUA_EQ(vlua, "true", "print(other_tab2:focus())");
	assert_int_equal(curr_view->id, other_id);

	tabs_only(&lwin);

	columns_teardown();
	opt_handlers_teardown();
}

TEST(vifmview_gotopath)
{
	conf_setup();
	make_abs_path(lwin.curr_dir, sizeof(lwin.curr_dir), TEST_DATA_PATH, "", NULL);

	GLUA_EQ(vlua, "false", "print(vifm.currview():gotopath('rename/wrong'))");

	GLUA_EQ(vlua, "true", "print(vifm.currview():gotopath('rename/a'))");
	assert_string_equal("a", lwin.dir_entry[lwin.list_pos].name);

	GLUA_EQ(vlua, "true", "print(vifm.currview():gotopath('aa'))");
	assert_string_equal("aa", lwin.dir_entry[lwin.list_pos].name);

	conf_teardown();
}

TEST(vifmview_loadcustom)
{
	opt_handlers_setup();
	make_abs_path(lwin.curr_dir, sizeof(lwin.curr_dir), TEST_DATA_PATH, "rename",
			NULL);

	/* Missing required keys. */
	BLUA_ENDS(vlua, ": `title` key is mandatory",
			"print(vifm.currview():loadcustom({ }))");
	assert_false(flist_custom_active(&lwin));
	BLUA_ENDS(vlua, ": `paths` key is mandatory",
			"print(vifm.currview():loadcustom({ title = 'title' }))");
	assert_false(flist_custom_active(&lwin));

	/* Invalid type field. */
	BLUA_ENDS(vlua, ": Unknown type of custom view: 'bla'",
			"print(vifm.currview():loadcustom({ title = 'title',"
			                                   "paths = { },"
			                                   "type = 'bla' }))");
	assert_false(flist_custom_active(&lwin));

	/* Good invocation. */
	GLUA_ENDS(vlua, "",
			"print(vifm.currview():loadcustom({ title = 'title',"
			                                   "paths = { 'a', 'aa' } }))");
	assert_true(flist_custom_active(&lwin));
	assert_int_equal(CV_REGULAR, lwin.custom.type);
	assert_int_equal(2, lwin.list_rows);
	assert_string_equal("a", lwin.dir_entry[0].name);
	assert_string_equal("aa", lwin.dir_entry[1].name);

	/* Bad invocation doesn't ruin current custom view. */
	BLUA_ENDS(vlua, "", "print(vifm.currview():loadcustom({ title = 'title' }))");
	assert_true(flist_custom_active(&lwin));
	assert_int_equal(CV_REGULAR, lwin.custom.type);
	assert_int_equal(2, lwin.list_rows);
	assert_string_equal("a", lwin.dir_entry[0].name);
	assert_string_equal("aa", lwin.dir_entry[1].name);

	/* Good invocation for unsorted view. */
	GLUA_ENDS(vlua, "",
			"print(vifm.currview():loadcustom({ title = 'title',"
			                                   "type = 'very-custom',"
			                                   "paths = { 'aa', 'a' } }))");
	assert_true(flist_custom_active(&lwin));
	assert_int_equal(CV_VERY, lwin.custom.type);
	assert_int_equal(2, lwin.list_rows);
	assert_string_equal("aa", lwin.dir_entry[0].name);
	assert_string_equal("a", lwin.dir_entry[1].name);

	/* Good invocation for regular view requested explicitly.  Missing file is
	 * silently ignored. */
	GLUA_ENDS(vlua, "",
			"print(vifm.currview():loadcustom({ title = 'title',"
			                                   "type = 'custom',"
			                                   "paths = { 'aa', 'a', 'x' } }))");
	assert_true(flist_custom_active(&lwin));
	assert_int_equal(CV_REGULAR, lwin.custom.type);
	assert_int_equal(2, lwin.list_rows);
	assert_string_equal("a", lwin.dir_entry[0].name);
	assert_string_equal("aa", lwin.dir_entry[1].name);

	/* List of files can be empty. */
	GLUA_ENDS(vlua, "",
			"print(vifm.currview():loadcustom({ title = 'title',"
			                                   "paths = { 'nosuchfile' } }))");
	assert_true(flist_custom_active(&lwin));
	assert_int_equal(CV_REGULAR, lwin.custom.type);
	assert_int_equal(1, lwin.list_rows);
	assert_string_equal("..", lwin.dir_entry[0].name);

	opt_handlers_teardown();
}

TEST(vifmview_entry_mimetype_unavailable, IF(has_no_mime_type_detection))
{
	GLUA_EQ(vlua, "nil", "print(vifm.currview():entry(2):mimetype())");
}

TEST(vifmview_entry_mimetype, IF(has_mime_type_detection))
{
	make_abs_path(lwin.curr_dir, sizeof(lwin.curr_dir), SANDBOX_PATH, "", NULL);
	lwin.dir_entry[1].origin = SANDBOX_PATH;
	copy_file(TEST_DATA_PATH "/read/very-long-line", SANDBOX_PATH "/file1");

	GLUA_EQ(vlua, "text/plain", "print(vifm.currview():entry(2):mimetype())");

	remove_file(SANDBOX_PATH "/file1");
}

static int
has_mime_type_detection(void)
{
	return get_mimetype(TEST_DATA_PATH "/read/dos-line-endings", 0) != NULL;
}

static int
has_no_mime_type_detection(void)
{
	return has_mime_type_detection() == 0;
}

/* vim: set tabstop=2 softtabstop=2 shiftwidth=2 noexpandtab cinoptions-=(0 : */
/* vim: set cinoptions+=t0 : */
