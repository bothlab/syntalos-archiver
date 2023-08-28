#include <sqfs/meta_writer.h>
#include <sqfs/dir_writer.h>
#include <sqfs/compressor.h>
#include <sqfs/id_table.h>
#include <sqfs/inode.h>
#include <sqfs/super.h>
#include <sqfs/io.h>

#include "sqfs-simple-writer.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include <sqfs/error.h>
#include <sys/stat.h>

#include <stdlib.h>
#include <string.h>


enum {
	DIR_SCAN_NO_SOCK = 0x0001,
	DIR_SCAN_NO_SLINK = 0x0002,
	DIR_SCAN_NO_FILE = 0x0004,
	DIR_SCAN_NO_BLK = 0x0008,
	DIR_SCAN_NO_DIR = 0x0010,
	DIR_SCAN_NO_CHR = 0x0020,
	DIR_SCAN_NO_FIFO = 0x0040,

	DIR_SCAN_KEEP_TIME = 0x0100,
	DIR_SCAN_KEEP_UID = 0x0200,
	DIR_SCAN_KEEP_GID = 0x0400,
	DIR_SCAN_KEEP_MODE = 0x0800,

	DIR_SCAN_ONE_FILESYSTEM = 0x1000,
	DIR_SCAN_NO_RECURSION = 0x2000,
	DIR_SCAN_MATCH_FULL_PATH = 0x4000,
};

typedef struct {
	sqfs_u32 flags;
	sqfs_u32 def_uid;
	sqfs_u32 def_gid;
	sqfs_u32 def_mode;
	sqfs_s64 def_mtime;

	/**
	 * @brief A prefix to attach to all returend paths
	 *
	 * If not null, this string and an additional "/" are prepended to
	 * all entries returned by the iterator.
	 */
	const char *prefix;

	/**
	 * @brief A glob pattern that either the name must match
	 *
	 * If this is not NULL, only paths that match this globbing pattern
	 * are returned. If the flag DIR_SCAN_MATCH_FULL_PATH is set, the
	 * entire path must match, slashes cannot match wild card characters.
	 * If not set, only the last part of the path is tested. The iterator
	 * still recurses into directories, it simply doesn't report them if
	 * they don't match.
	 */
	const char *name_pattern;
} dir_tree_cfg_t;


typedef struct {
	sqfs_dir_iterator_t base;

	dir_tree_cfg_t cfg;
	int state;
	sqfs_dir_iterator_t *rec;
} dir_tree_iterator_t;

static bool should_skip(const dir_tree_iterator_t *dir, const sqfs_dir_entry_t *ent)
{
	unsigned int type_mask;

	if ((dir->cfg.flags & DIR_SCAN_ONE_FILESYSTEM)) {
		if (ent->flags & SQFS_DIR_ENTRY_FLAG_MOUNT_POINT)
			return true;
	}

	switch (ent->mode & S_IFMT) {
	case S_IFSOCK: type_mask = DIR_SCAN_NO_SOCK;  break;
	case S_IFLNK:  type_mask = DIR_SCAN_NO_SLINK; break;
	case S_IFREG:  type_mask = DIR_SCAN_NO_FILE;  break;
	case S_IFBLK:  type_mask = DIR_SCAN_NO_BLK;   break;
	case S_IFCHR:  type_mask = DIR_SCAN_NO_CHR;   break;
	case S_IFIFO:  type_mask = DIR_SCAN_NO_FIFO;  break;
	default:       type_mask = 0;                 break;
	}

	return (dir->cfg.flags & type_mask) != 0;
}

static sqfs_dir_entry_t *expand_path(const dir_tree_iterator_t *it, sqfs_dir_entry_t *ent)
{
	if (it->cfg.prefix != NULL && it->cfg.prefix[0] != '\0') {
		size_t plen = strlen(it->cfg.prefix) + 1;
		size_t slen = strlen(ent->name) + 1;
		void *new = realloc(ent, sizeof(*ent) + plen + slen);

		if (new == NULL) {
			free(ent);
			return NULL;
		}

		ent = new;
		memmove(ent->name + plen, ent->name, slen);

		memcpy(ent->name, it->cfg.prefix, plen - 1);
		ent->name[plen - 1] = '/';
	}

	return ent;
}

static void apply_changes(const dir_tree_iterator_t *it, sqfs_dir_entry_t *ent)
{
	if (!(it->cfg.flags & DIR_SCAN_KEEP_TIME))
		ent->mtime = it->cfg.def_mtime;

	if (!(it->cfg.flags & DIR_SCAN_KEEP_UID))
		ent->uid = it->cfg.def_uid;

	if (!(it->cfg.flags & DIR_SCAN_KEEP_GID))
		ent->gid = it->cfg.def_gid;

	if (!(it->cfg.flags & DIR_SCAN_KEEP_MODE)) {
		ent->mode &= ~(07777);
		ent->mode |= it->cfg.def_mode & 07777;
	}
}

/*****************************************************************************/

static void destroy(sqfs_object_t *obj)
{
	dir_tree_iterator_t *it = (dir_tree_iterator_t *)obj;

	sqfs_drop(it->rec);
	free(it);
}

static int next(sqfs_dir_iterator_t *base, sqfs_dir_entry_t **out)
{
	dir_tree_iterator_t *it = (dir_tree_iterator_t *)base;
	sqfs_dir_entry_t *ent;
	int ret;

	if (it->state)
		return it->state;
retry:
	*out = NULL;
	ent = NULL;

	for (;;) {
		ret = it->rec->next(it->rec, &ent);
		if (ret != 0) {
			it->state = ret;
			return ret;
		}

		if (!should_skip(it, ent))
			break;

		if (S_ISDIR(ent->mode))
			it->rec->ignore_subdir(it->rec);
		free(ent);
		ent = NULL;
	}

	ent = expand_path(it, ent);
	if (ent == NULL) {
		it->state = SQFS_ERROR_ALLOC;
		return it->state;
	}

	apply_changes(it, ent);

	if (S_ISDIR(ent->mode)) {
		if (it->cfg.flags & DIR_SCAN_NO_RECURSION)
			it->rec->ignore_subdir(it->rec);

		if (it->cfg.flags & DIR_SCAN_NO_DIR) {
			free(ent);
			goto retry;
		}
	}

	if (it->cfg.name_pattern != NULL) {
		if (it->cfg.flags & DIR_SCAN_MATCH_FULL_PATH) {
			ret = fnmatch(it->cfg.name_pattern,
				      ent->name, FNM_PATHNAME);
		} else {
			const char *name = strrchr(ent->name, '/');
			name = (name == NULL) ? ent->name : (name + 1);

			ret = fnmatch(it->cfg.name_pattern, name, 0);
		}

		if (ret != 0) {
			free(ent);
			goto retry;
		}
	}

	*out = ent;
	return it->state;
}

static int read_link(sqfs_dir_iterator_t *base, char **out)
{
	dir_tree_iterator_t *it = (dir_tree_iterator_t *)base;

	if (it->state)
		return it->state;

	return it->rec->read_link(it->rec, out);
}

static int open_subdir(sqfs_dir_iterator_t *base, sqfs_dir_iterator_t **out)
{
	dir_tree_iterator_t *it = (dir_tree_iterator_t *)base;

	if (it->state)
		return it->state;

	return it->rec->open_subdir(it->rec, out);
}

static void ignore_subdir(sqfs_dir_iterator_t *base)
{
	dir_tree_iterator_t *it = (dir_tree_iterator_t *)base;

	if (it->state == 0)
		it->rec->ignore_subdir(it->rec);
}

static int open_file_ro(sqfs_dir_iterator_t *base, sqfs_istream_t **out)
{
	dir_tree_iterator_t *it = (dir_tree_iterator_t *)base;

	if (it->state)
		return it->state;

	return it->rec->open_file_ro(it->rec, out);
}

static int read_xattr(sqfs_dir_iterator_t *base, sqfs_xattr_t **out)
{
	dir_tree_iterator_t *it = (dir_tree_iterator_t *)base;

	if (it->state)
		return it->state;

	return it->rec->read_xattr(it->rec, out);
}

int main(int argc, char **argv)
{
    sqfs_writer_cfg_t cfg;
    sqfs_writer_t sqfs;


    sqfs_writer_cfg_init(&cfg);
    cfg.filename = "42.sqfs";

    if (sqfs_writer_init(&sqfs, &cfg))
            return EXIT_FAILURE;

    {
        s_dir_iterator_t *dir = NULL;
        dir_tree_cfg_t dt_cfg;
        int ret;

        memset(&dt_cfg, 0, sizeof(dt_cfg));
        dt_cfg.flags = opt.dirscan_flags | DIR_SCAN_KEEP_UID |
                DIR_SCAN_KEEP_GID | DIR_SCAN_KEEP_MODE;
        dt_cfg.def_mtime = sqfs.fs.defaults.mtime;

        dir = dir_tree_iterator_create(opt.packdir, &dt_cfg);
        if (dir == NULL)
                goto out;

        ret = fstree_from_dir(&sqfs.fs, dir);
        sqfs_drop(dir);
        if (ret != 0)
                goto out;

    }


out:
	return EXIT_SUCCESS;
}
