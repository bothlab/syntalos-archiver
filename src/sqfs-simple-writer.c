
#include "sqfs-simple-writer.h"

#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>
#include <errno.h>
#include <sys/stat.h>

#ifdef HAVE_SCHED_GETAFFINITY
#include <sched.h>

static size_t os_get_num_jobs(void)
{
	cpu_set_t cpu_set;
	CPU_ZERO(&cpu_set);

	if (sched_getaffinity(0, sizeof cpu_set, &cpu_set) == -1)
		return 1;
	else
		return CPU_COUNT(&cpu_set);
}
#else
static size_t os_get_num_jobs(void)
{
	return 1;
}
#endif


enum {
	DEF_UID = 0,
	DEF_GID,
	DEF_MODE,
	DEF_MTIME,
};

static const char *defaults[] = {
	[DEF_UID] = "uid",
	[DEF_GID] = "gid",
	[DEF_MODE] = "mode",
	[DEF_MTIME] = "mtime",
	NULL
};

static int parse_fstree_defaults(fstree_defaults_t *sb, char *subopts)
{
	char *value;
	long lval;
	int i;

	memset(sb, 0, sizeof(*sb));
	sb->mode = S_IFDIR | 0755;
	sb->mtime = 0;

	if (subopts == NULL)
		return 0;

	while (*subopts != '\0') {
		i = getsubopt(&subopts, (char *const *)defaults, &value);

		if (value == NULL) {
			fprintf(stderr, "Missing value for option %s\n",
				defaults[i]);
			return -1;
		}

		switch (i) {
		case DEF_UID:
			lval = strtol(value, NULL, 0);
			if (lval < 0)
				goto fail_uv;
			if (lval > (long)INT32_MAX)
				goto fail_ov;
			sb->uid = lval;
			break;
		case DEF_GID:
			lval = strtol(value, NULL, 0);
			if (lval < 0)
				goto fail_uv;
			if (lval > (long)INT32_MAX)
				goto fail_ov;
			sb->gid = lval;
			break;
		case DEF_MODE:
			lval = strtol(value, NULL, 0);
			if (lval < 0)
				goto fail_uv;
			if (lval > 07777)
				goto fail_ov;
			sb->mode = S_IFDIR | (sqfs_u16)lval;
			break;
		case DEF_MTIME:
			errno = 0;
			lval = strtol(value, NULL, 0);
			if (lval < 0)
				goto fail_uv;
			if (sizeof(long) > sizeof(sqfs_u32)) {
				if (lval > (long)UINT32_MAX)
					goto fail_ov;
			} else if (errno != 0) {
				goto fail_ov;
			}
			sb->mtime = lval;
			break;
		default:
			fprintf(stderr, "Unknown option '%s'\n", value);
			return -1;
		}
	}
	return 0;
fail_uv:
	fprintf(stderr, "%s: value must be positive\n", defaults[i]);
	return -1;
fail_ov:
	fprintf(stderr, "%s: value too large\n", defaults[i]);
	return -1;
}

static void sqfs_perror(const char *file, const char *action, int error_code)
{
	//os_error_t syserror;
	const char *errstr;

	switch (error_code) {
	case SQFS_ERROR_ALLOC:
		errstr = "out of memory";
		break;
	case SQFS_ERROR_IO:
		errstr = "I/O error";
		break;
	case SQFS_ERROR_COMPRESSOR:
		errstr = "internal compressor error";
		break;
	case SQFS_ERROR_INTERNAL:
		errstr = "internal error";
		break;
	case SQFS_ERROR_CORRUPTED:
		errstr = "data corrupted";
		break;
	case SQFS_ERROR_UNSUPPORTED:
		errstr = "unknown or not supported";
		break;
	case SQFS_ERROR_OVERFLOW:
		errstr = "numeric overflow";
		break;
	case SQFS_ERROR_OUT_OF_BOUNDS:
		errstr = "location out of bounds";
		break;
	case SFQS_ERROR_SUPER_MAGIC:
		errstr = "wrong magic value in super block";
		break;
	case SFQS_ERROR_SUPER_VERSION:
		errstr = "wrong squashfs version in super block";
		break;
	case SQFS_ERROR_SUPER_BLOCK_SIZE:
		errstr = "invalid block size specified in super block";
		break;
	case SQFS_ERROR_NOT_DIR:
		errstr = "target is not a directory";
		break;
	case SQFS_ERROR_NO_ENTRY:
		errstr = "no such file or directory";
		break;
	case SQFS_ERROR_LINK_LOOP:
		errstr = "hard link loop detected";
		break;
	case SQFS_ERROR_NOT_FILE:
		errstr = "target is not a file";
		break;
	case SQFS_ERROR_ARG_INVALID:
		errstr = "invalid argument";
		break;
	case SQFS_ERROR_SEQUENCE:
		errstr = "illegal oder of operations";
		break;
	default:
		errstr = "libsquashfs returned an unknown error code";
		break;
	}

	//syserror = get_os_error_state();
	if (file != NULL)
		fprintf(stderr, "%s: ", file);

	if (action != NULL)
		fprintf(stderr, "%s: ", action);

	fprintf(stderr, "%s.\n", errstr);
	//set_os_error_state(syserror);

	if (error_code == SQFS_ERROR_IO) {
#if defined(_WIN32) || defined(__WINDOWS__)
		w32_perror("OS error");
#else
		perror("OS error");
#endif
	}
}

void sqfs_writer_cfg_init(sqfs_writer_cfg_t *cfg)
{
	memset(cfg, 0, sizeof(*cfg));

	cfg->num_jobs = os_get_num_jobs();
	cfg->block_size = SQFS_DEFAULT_BLOCK_SIZE;
	cfg->devblksize = SQFS_DEVBLK_SIZE;
	cfg->comp_id = SQFS_COMP_ZSTD;
}

int sqfs_writer_init(sqfs_writer_t *sqfs, const sqfs_writer_cfg_t *wrcfg)
{
	sqfs_block_processor_desc_t blkdesc;
	sqfs_compressor_config_t cfg;
	fstree_defaults_t fsd;
	int ret, flags;

	sqfs->filename = wrcfg->filename;

	if (sqfs_compressor_config_init(&cfg, wrcfg->comp_id,
					wrcfg->block_size,
					0)) {
		return -1;
	}

	sqfs->outfile = sqfs_open_file(wrcfg->filename, wrcfg->outmode);
	if (sqfs->outfile == NULL) {
		perror(wrcfg->filename);
		return -1;
	}

	if (parse_fstree_defaults(&fsd, wrcfg->fs_defaults))
		goto fail_file;

	if (fstree_init(&sqfs->fs, &fsd))
		goto fail_file;

	ret = sqfs_compressor_create(&cfg, &sqfs->cmp);

#ifdef WITH_LZO
	if (cfg.id == SQFS_COMP_LZO) {
		if (sqfs->cmp != NULL)
			sqfs_free(sqfs->cmp);

		ret = lzo_compressor_create(&cfg, &sqfs->cmp);
	}
#endif

	if (ret != 0) {
		sqfs_perror(wrcfg->filename, "creating compressor", ret);
		goto fail_fs;
	}

	cfg.flags |= SQFS_COMP_FLAG_UNCOMPRESS;
	ret = sqfs_compressor_create(&cfg, &sqfs->uncmp);

#ifdef WITH_LZO
	if (cfg.id == SQFS_COMP_LZO) {
		if (ret == 0 && sqfs->uncmp != NULL)
			sqfs_free(sqfs->uncmp);

		ret = lzo_compressor_create(&cfg, &sqfs->uncmp);
	}
#endif

	if (ret != 0) {
		sqfs_perror(wrcfg->filename, "creating uncompressor", ret);
		goto fail_cmp;
	}

	ret = sqfs_super_init(&sqfs->super, wrcfg->block_size,
			      sqfs->fs.defaults.mtime, wrcfg->comp_id);
	if (ret) {
		sqfs_perror(wrcfg->filename, "initializing super block", ret);
		goto fail_uncmp;
	}

	ret = sqfs_super_write(&sqfs->super, sqfs->outfile);
	if (ret) {
		sqfs_perror(wrcfg->filename, "writing super block", ret);
		goto fail_uncmp;
	}

	ret = sqfs->cmp->write_options(sqfs->cmp, sqfs->outfile);
	if (ret < 0) {
		sqfs_perror(wrcfg->filename, "writing compressor options", ret);
		goto fail_uncmp;
	}

	if (ret > 0)
		sqfs->super.flags |= SQFS_FLAG_COMPRESSOR_OPTIONS;

	sqfs->blkwr = sqfs_block_writer_create(sqfs->outfile, wrcfg->devblksize, 0);
	if (sqfs->blkwr == NULL) {
		perror("creating block writer");
		goto fail_uncmp;
	}

	sqfs->fragtbl = sqfs_frag_table_create(0);
	if (sqfs->fragtbl == NULL) {
		perror("creating fragment table");
		goto fail_blkwr;
	}

	memset(&blkdesc, 0, sizeof(blkdesc));
	blkdesc.size = sizeof(blkdesc);
	blkdesc.max_block_size = wrcfg->block_size;
	blkdesc.num_workers = wrcfg->num_jobs;
	blkdesc.max_backlog = wrcfg->max_backlog;
	blkdesc.cmp = sqfs->cmp;
	blkdesc.wr = sqfs->blkwr;
	blkdesc.tbl = sqfs->fragtbl;
	blkdesc.file = sqfs->outfile;
	blkdesc.uncmp = sqfs->uncmp;

	ret = sqfs_block_processor_create_ex(&blkdesc, &sqfs->data);
	if (ret != 0) {
		sqfs_perror(wrcfg->filename, "creating data block processor",
			    ret);
		goto fail_fragtbl;
	}

	sqfs->idtbl = sqfs_id_table_create(0);
	if (sqfs->idtbl == NULL) {
		sqfs_perror(wrcfg->filename, "creating ID table",
			    SQFS_ERROR_ALLOC);
		goto fail_data;
	}

	if (!wrcfg->no_xattr) {
		sqfs->xwr = sqfs_xattr_writer_create(0);

		if (sqfs->xwr == NULL) {
			sqfs_perror(wrcfg->filename, "creating xattr writer",
				    SQFS_ERROR_ALLOC);
			goto fail_id;
		}
	}

	sqfs->im = sqfs_meta_writer_create(sqfs->outfile, sqfs->cmp, 0);
	if (sqfs->im == NULL) {
		fputs("Error creating inode meta data writer.\n", stderr);
		goto fail_xwr;
	}

	sqfs->dm = sqfs_meta_writer_create(sqfs->outfile, sqfs->cmp,
					   SQFS_META_WRITER_KEEP_IN_MEMORY);
	if (sqfs->dm == NULL) {
		fputs("Error creating directory meta data writer.\n", stderr);
		goto fail_im;
	}

	flags = 0;
	if (wrcfg->exportable)
		flags |= SQFS_DIR_WRITER_CREATE_EXPORT_TABLE;

	sqfs->dirwr = sqfs_dir_writer_create(sqfs->dm, flags);
	if (sqfs->dirwr == NULL) {
		fputs("Error creating directory table writer.\n", stderr);
		goto fail_dm;
	}

	return 0;
fail_dm:
	sqfs_free(sqfs->dm);
fail_im:
	sqfs_free(sqfs->im);
fail_xwr:
	sqfs_free(sqfs->xwr);
fail_id:
	sqfs_free(sqfs->idtbl);
fail_data:
	sqfs_free(sqfs->data);
fail_fragtbl:
	sqfs_free(sqfs->fragtbl);
fail_blkwr:
	sqfs_free(sqfs->blkwr);
fail_uncmp:
	sqfs_free(sqfs->uncmp);
fail_cmp:
	sqfs_free(sqfs->cmp);
fail_fs:
	fstree_cleanup(&sqfs->fs);
fail_file:
	sqfs_free(sqfs->outfile);
	return -1;
}
