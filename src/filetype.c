/* vifm
 * Copyright (C) 2001 Ken Steen.
 * Copyright (C) 2011 xaizek.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA
 */

#include "filetype.h"

#include <assert.h> /* assert() */
#include <ctype.h> /* isspace() */
#include <stddef.h> /* NULL */
#include <stdlib.h> /* free() */
#include <string.h> /* strchr() strdup() strcasecmp() */

#include "compat/fs_limits.h"
#include "compat/reallocarray.h"
#include "modes/dialogs/msg_dialog.h"
#include "utils/matchers.h"
#include "utils/str.h"
#include "utils/string_array.h"
#include "utils/path.h"
#include "utils/utils.h"

static const char * find_existing_cmd(const assoc_list_t *record_list,
		const char file[]);
static assoc_record_t find_existing_cmd_record(const assoc_records_t *records);
static assoc_records_t parse_command_list(const char cmds[], int with_descr);
static assoc_records_t clone_all_matching_records(const char file[],
		const assoc_list_t *record_list);
static int add_assoc(assoc_list_t *assoc_list, assoc_t assoc);
static int is_assoc_equal(const assoc_t *a, const assoc_t *b);
static void reset_all_lists(void);
static void add_defaults(int in_x);
static void reset_list(assoc_list_t *assoc_list);
static void reset_list_head(assoc_list_t *assoc_list);
static void free_assoc_record(assoc_record_t *record);
static void undouble_commas(char s[]);
static void free_assoc(assoc_t *assoc);
static int assoc_records_contains(assoc_records_t *assocs, const char command[],
		const char description[]);
static void safe_free(char **adr);
static int is_assoc_record_empty(const assoc_record_t *record);
static int mg_match(const matchers_group_t *mg, const char str[]);
static void mg_free(matchers_group_t *mg);

const assoc_record_t NONE_PSEUDO_PROG = {
	.command = "",
	.description = "",
};

assoc_list_t filetypes;
assoc_list_t xfiletypes;
assoc_list_t fileviewers;

/* Internal list that stores only currently active associations.
 * Since it holds only copies of structures from filetype and filextype lists,
 * it doesn't consume much memory, and its items shouldn't be freed */
static assoc_list_t active_filetypes;

/* Used to set type of new association records */
static AssocRecordType new_records_type = ART_CUSTOM;

/* Pointer to external command existence check function. */
static external_command_exists_t external_command_exists_func;

void
ft_init(external_command_exists_t ece_func)
{
	external_command_exists_func = ece_func;
}

int
ft_exists(const char cmd[])
{
	if(external_command_exists_func == NULL)
	{
		return 1;
	}

	char cmd_name[NAME_MAX + 1];
	(void)extract_cmd_name(cmd, 0, sizeof(cmd_name), cmd_name);
	system_to_internal_slashes(cmd_name);
	return external_command_exists_func(cmd_name);
}

const char *
ft_get_program(const char file[])
{
	return find_existing_cmd(&active_filetypes, file);
}

const char *
ft_get_viewer(const char file[])
{
	return find_existing_cmd(&fileviewers, file);
}

strlist_t
ft_get_viewers(const char file[])
{
	strlist_t viewers = {};

	int i;
	for(i = 0; i < fileviewers.count; ++i)
	{
		assoc_t *const assoc = &fileviewers.list[i];

		if(!mg_match(&assoc->mg, file))
		{
			continue;
		}

		int j;
		for(j = 0; j < assoc->records.count; ++j)
		{
			const char *cmd = assoc->records.list[j].command;
			if(!is_in_string_array(viewers.items, viewers.nitems, cmd) &&
					ft_exists(cmd))
			{
				viewers.nitems = add_to_string_array(&viewers.items, viewers.nitems,
						cmd);
			}
		}
	}

	return viewers;
}

/* Finds first existing command which pattern matches given file.  Returns the
 * command (its lifetime is managed by this unit) or NULL on failure. */
static const char *
find_existing_cmd(const assoc_list_t *record_list, const char file[])
{
	int i;

	for(i = 0; i < record_list->count; ++i)
	{
		assoc_record_t prog;
		assoc_t *const assoc = &record_list->list[i];

		if(!mg_match(&assoc->mg, file))
		{
			continue;
		}

		prog = find_existing_cmd_record(&assoc->records);
		if(!is_assoc_record_empty(&prog))
		{
			return prog.command;
		}
	}

	return NULL;
}

/* Finds record that corresponds to an external command that is available.
 * Returns the record on success or an empty record on failure. */
static assoc_record_t
find_existing_cmd_record(const assoc_records_t *records)
{
	static assoc_record_t empty_record;

	int i;
	for(i = 0; i < records->count; ++i)
	{
		if(ft_exists(records->list[i].command))
		{
			return records->list[i];
		}
	}

	return empty_record;
}

assoc_records_t
ft_get_all_programs(const char file[])
{
	return clone_all_matching_records(file, &active_filetypes);
}

void
ft_set_programs(matchers_group_t mg, const char programs[], int for_x, int in_x)
{
	const assoc_t assoc = {
		.mg = mg,
		.records = parse_command_list(programs, 1),
	};

	/* On error, add_assoc() frees assoc, so just exit then. */
	if(add_assoc(for_x ? &xfiletypes : &filetypes, assoc))
	{
		if(!for_x || in_x)
		{
			(void)add_assoc(&active_filetypes, assoc);
		}
	}
}

/* Parses comma separated list of commands into array of associations.  Returns
 * the list. */
static assoc_records_t
parse_command_list(const char cmds[], int with_descr)
{
	assoc_records_t records = {};

	char *free_this = strdup(cmds);

	char *part = free_this, *state = NULL;
	while((part = split_and_get_dc(part, &state)) != NULL)
	{
		const char *description = "";

		if(with_descr && *part == '{')
		{
			char *p = strchr(part + 1, '}');
			if(p != NULL)
			{
				*p = '\0';
				description = part + 1;
				part = skip_whitespace(p + 1);
			}
		}

		if(part[0] != '\0')
		{
			ft_assoc_record_add(&records, part, description);
		}
	}

	free(free_this);

	return records;
}

assoc_records_t
ft_get_all_viewers(const char file[])
{
	return clone_all_matching_records(file, &fileviewers);
}

/* Clones all records which pattern matches the file.  Returns list of records
 * composed of clones. */
static assoc_records_t
clone_all_matching_records(const char file[], const assoc_list_t *record_list)
{
	int i;
	assoc_records_t result = {};

	for(i = 0; i < record_list->count; ++i)
	{
		assoc_t *const assoc = &record_list->list[i];
		if(mg_match(&assoc->mg, file))
		{
			ft_assoc_record_add_all(&result, &assoc->records);
		}
	}

	return result;
}

void
ft_set_viewers(matchers_group_t mg, const char viewers[])
{
	const assoc_t assoc = {
		.mg = mg,
		.records = parse_command_list(viewers, 0),
	};
	/* On error, add_assoc() frees assoc, so just exit then. */
	(void)add_assoc(&fileviewers, assoc);
}

/* Adds association to the list of associations, takes ownership of it
 * regardless of the outcome.  Returns non-zero if the element was added,
 * otherwise it's freed. */
static int
add_assoc(assoc_list_t *assoc_list, assoc_t assoc)
{
	int i;
	for(i = 0; i < assoc_list->count; ++i)
	{
		if(is_assoc_equal(&assoc_list->list[i], &assoc))
		{
			/* Not adding duplicates. */
			free_assoc(&assoc);
			return 0;
		}
	}

	void *p;
	p = reallocarray(assoc_list->list, assoc_list->count + 1, sizeof(assoc_t));
	if(p == NULL)
	{
		free_assoc(&assoc);
		show_error_msg("Memory Error", "Unable to allocate enough memory");
		return 0;
	}

	assoc_list->list = p;
	assoc_list->list[assoc_list->count] = assoc;
	assoc_list->count++;
	return 1;
}

/* Compares two associations for equality.  Returns non-zero if they are equal,
 * otherwise zero is returned. */
static int
is_assoc_equal(const assoc_t *a, const assoc_t *b)
{
	if(a->records.count != b->records.count)
	{
		return 0;
	}

	if(a->mg.count != b->mg.count)
	{
		return 0;
	}

	int i;

	/* In principle, we're only interested whether we have the same set of
	 * matchers or not regardless of their order, but since the goal is to avoid
	 * essentially identical duplicates, just do pair-wise comparison. */
	for(i = 0; i < a->mg.count; ++i)
	{
		if(strcmp(matchers_get_expr(a->mg.list[i]),
					matchers_get_expr(b->mg.list[i])) != 0)
		{
			return 0;
		}
	}

	for(i = 0; i < a->records.count; ++i)
	{
		if(strcmp(a->records.list[i].command, b->records.list[i].command) != 0)
		{
			return 0;
		}
	}

	return 1;
}

ViewerKind
ft_viewer_kind(const char viewer[])
{
	if(is_null_or_empty(viewer))
	{
		/* No special viewer implies viewing in text form. */
		return VK_TEXTUAL;
	}

	/* Pass-through marker (for sixel graphics or something similar), %px and %py
	 * could be needed here as well, so this check has higher priority. */
	if(ma_contains1(viewer, MK_pd))
	{
		return VK_PASS_THROUGH;
	}

	/* %pw and %ph can be useful for text output, but %px and %py are useful
	 * for graphics only and basically must be used both at the same time. */
	if(ma_contains1(viewer, MK_px) && ma_contains1(viewer, MK_py))
	{
		return VK_GRAPHICAL;
	}

	/* Must be just text otherwise. */
	return VK_TEXTUAL;
}

void
ft_reset(int in_x)
{
	reset_all_lists();
	add_defaults(in_x);
}

static void
reset_all_lists(void)
{
	reset_list(&filetypes);
	reset_list(&xfiletypes);
	reset_list(&fileviewers);

	reset_list_head(&active_filetypes);
}

/* Loads default (builtin) associations. */
static void
add_defaults(int in_x)
{
	char *error;
	matchers_group_t mg;
	if(ft_mg_from_string("{*/}", &mg, &error) != 0)
	{
		assert(0 && "Failed to allocate builtin matcher!");
	}

	new_records_type = ART_BUILTIN;
	ft_set_programs(mg, "{Enter directory}" VIFM_PSEUDO_CMD, /*for_x=*/0, in_x);
	new_records_type = ART_CUSTOM;
}

static void
reset_list(assoc_list_t *assoc_list)
{
	int i;
	for(i = 0; i < assoc_list->count; i++)
	{
		free_assoc(&assoc_list->list[i]);
	}
	reset_list_head(assoc_list);
}

static void
reset_list_head(assoc_list_t *assoc_list)
{
	free(assoc_list->list);
	assoc_list->list = NULL;
	assoc_list->count = 0;
}

static void
free_assoc(assoc_t *assoc)
{
	mg_free(&assoc->mg);
	ft_assoc_records_free(&assoc->records);
}

void
ft_assoc_records_free(assoc_records_t *records)
{
	int i;
	for(i = 0; i < records->count; i++)
	{
		free_assoc_record(&records->list[i]);
	}

	free(records->list);
	records->list = NULL;
	records->count = 0;
}

/* After this call the structure contains NULL values. */
static void
free_assoc_record(assoc_record_t *record)
{
	safe_free(&record->command);
	safe_free(&record->description);
}

int
ft_assoc_exists(const assoc_list_t *assocs, const char pattern[],
		const char cmd[])
{
	if(*cmd == '{')
	{
		const char *const descr_end = strchr(cmd + 1, '}');
		if(descr_end != NULL)
		{
			cmd = skip_whitespace(descr_end + 1);
		}
	}

	char *undoubled = strdup(cmd);
	if(undoubled == NULL)
	{
		show_error_msg("Memory Error", "Unable to allocate enough memory");
		return 0;
	}
	undouble_commas(undoubled);

	int i;
	for(i = 0; i < assocs->count; ++i)
	{
		int j;

		const assoc_t assoc = assocs->list[i];

		char *mg_str = ft_mg_to_string(&assoc.mg);
		if(mg_str == NULL)
		{
			show_error_msg("Memory Error", "Unable to allocate enough memory");
			return 0;
		}

		if(strcmp(mg_str, pattern) != 0)
		{
			free(mg_str);
			continue;
		}
		free(mg_str);

		for(j = 0; j < assoc.records.count; ++j)
		{
			const assoc_record_t ft_record = assoc.records.list[j];
			if(strcmp(ft_record.command, undoubled) == 0)
			{
				free(undoubled);
				return 1;
			}
		}
	}

	free(undoubled);
	return 0;
}

/* Updates the string in place to squash double commas into single one. */
static void
undouble_commas(char s[])
{
	char *p;

	p = s;
	while(s[0] != '\0')
	{
		if(s[0] == ',' && s[1] == ',')
		{
			++s;
		}
		*p++ = s[0];
		if(s[0] != '\0')
		{
			++s;
		}
	}
	*p = '\0';
}

void
ft_assoc_record_add(assoc_records_t *records, const char *command,
		const char *description)
{
	if(assoc_records_contains(records, command, description))
	{
		return;
	}

	void *p;
	p = reallocarray(records->list, records->count + 1, sizeof(assoc_record_t));
	if(p == NULL)
	{
		show_error_msg("Memory Error", "Unable to allocate enough memory");
		return;
	}

	records->list = p;
	records->list[records->count].command = strdup(command);
	records->list[records->count].description = strdup(description);
	records->list[records->count].type = new_records_type;
	records->count++;
}

void
ft_assoc_record_add_all(assoc_records_t *assocs, const assoc_records_t *src)
{
	int i;
	void *p;
	const int src_count = src->count;

	if(src_count == 0)
	{
		return;
	}

	p = reallocarray(assocs->list, assocs->count + src_count,
			sizeof(assoc_record_t));
	if(p == NULL)
	{
		show_error_msg("Memory Error", "Unable to allocate enough memory");
		return;
	}

	assocs->list = p;

	for(i = 0; i < src_count; ++i)
	{
		assoc_record_t *record = &src->list[i];
		if(assoc_records_contains(assocs, record->command, record->description))
		{
			continue;
		}

		assocs->list[assocs->count].command = strdup(record->command);
		assocs->list[assocs->count].description = strdup(record->description);
		assocs->list[assocs->count].type = record->type;
		++assocs->count;
	}
}

/* Checks if list of records contains specified command-description pair.
 * Returns non-zero if so, otherwise zero is returned. */
static int
assoc_records_contains(assoc_records_t *assocs, const char command[],
		const char description[])
{
	int i;
	for(i = 0; i < assocs->count; ++i)
	{
		if(strcmp(assocs->list[i].command, command) == 0
				&& strcmp(assocs->list[i].description, description) == 0)
		{
			return 1;
		}
	}
	return 0;
}

static void
safe_free(char **adr)
{
	free(*adr);
	*adr = NULL;
}

/* Returns non-zero for an empty assoc_record_t structure. */
static int
is_assoc_record_empty(const assoc_record_t *record)
{
	return record->command == NULL && record->description == NULL;
}

/* Checks whether given string matches.  Returns non-zero if so, otherwise zero
 * is returned. */
static int
mg_match(const matchers_group_t *mg, const char str[])
{
	int i;
	for(i = 0; i < mg->count; ++i)
	{
		if(matchers_match(mg->list[i], str))
		{
			return 1;
		}
	}
	return 0;
}

int
ft_mg_from_string(const char str[], matchers_group_t *mg, char **error)
{
	*error = NULL;

	int nexprs;
	char **exprs = matchers_list(str, &nexprs);
	if(exprs == NULL)
	{
		update_string(error, "Failed to parse list of matchers.");
		return 1;
	}

	matchers_group_t result = {
		.list = reallocarray(NULL, nexprs, sizeof(*result.list)),
		.count = 0,
	};
	if(result.list == NULL)
	{
		free_string_array(exprs, nexprs);
		update_string(error, "Failed to allocate matchers.");
		return 1;
	}

	int i;
	for(i = 0; i < nexprs; ++i, ++result.count)
	{
		char *matcher_error;
		result.list[i] = matchers_alloc(exprs[i], /*cs_by_def=*/0,
				/*glob_by_def=*/1, /*on_empty_re=*/"", &matcher_error);
		if(result.list[i] == NULL)
		{
			put_string(error, format_str("Wrong pattern (%s): %s", exprs[i],
						matcher_error));
			free(matcher_error);
			break;
		}
	}

	free_string_array(exprs, nexprs);

	if(i < nexprs)
	{
		mg_free(&result);
		return 1;
	}

	*mg = result;
	return 0;
}

/* Frees matchers in a group of matchers. */
static void
mg_free(matchers_group_t *mg)
{
	int i;
	for(i = 0; i < mg->count; ++i)
	{
		matchers_free(mg->list[i]);
	}
	free(mg->list);
}

char *
ft_mg_to_string(const matchers_group_t *mg)
{
	char *str = NULL;
	size_t len = 0;

	int i;
	for(i = 0; i < mg->count; ++i)
	{
		if(i != 0 && strappendch(&str, &len, ',') != 0)
		{
			break;
		}

		if(strappend(&str, &len, matchers_get_expr(mg->list[i])) != 0)
		{
			break;
		}
	}

	if(i < mg->count)
	{
		free(str);
		return NULL;
	}

	return str;
}

/* vim: set tabstop=2 softtabstop=2 shiftwidth=2 noexpandtab cinoptions-=(0 : */
/* vim: set cinoptions+=t0 filetype=c : */
