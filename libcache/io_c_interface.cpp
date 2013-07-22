#include <fcntl.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>

#include <vector>

#include "io_interface.h"
#include "concurrency.h"
#include "RAID_config.h"
#include "cache.h"
#include "cache_config.h"

static long cache_size = 512 * 1024 * 1024;
static int cache_type = ASSOCIATIVE_CACHE;
int num_threads = 1;
int num_nodes = 1;
int RAID_mapping_option = RAID0;
int RAID_block_size = 16;		// in the number of pages.

void ssd_io_init(const char *name, int flags)
{
	static atomic_unsigned_integer has_init;

	// this is the first time it is called.
	if (has_init.inc(1) == 1) {
		int access_option = GLOBAL_CACHE_ACCESS;
		if (flags & O_DIRECT) {
			printf("use remote access\n");
			access_option = REMOTE_ACCESS;
		}
		else {
			printf("use global cached IO\n");
		}

		// Init RAID configuration.
		std::vector<file_info> files;
		int num_files = retrieve_data_files(name, files);
		printf("There are %d data files\n", num_files);
		RAID_config raid_conf(files, RAID_mapping_option, RAID_block_size);

		// Init node id array.
		std::set<int> node_ids = raid_conf.get_node_ids();
		// In this way, we can guarantee that the cache is created
		// on the nodes with the data files.
		for (int i = 0; i < num_nodes
				&& node_ids.size() < (unsigned) num_nodes; i++)
			node_ids.insert(i);
		std::vector<int> node_id_array;
		// We only get a specified number of nodes.
		for (std::set<int>::const_iterator it = node_ids.begin();
				it != node_ids.end() && (int) node_id_array.size() < num_nodes; it++)
			node_id_array.push_back(*it);
		printf("There are %ld nodes\n", node_id_array.size());

		// Init cache configuration.
		cache_config *cache_conf = NULL;
		if (access_option == GLOBAL_CACHE_ACCESS)
			cache_conf = new even_cache_config(cache_size, cache_type,
					node_id_array);
		else if (access_option == PART_GLOBAL_ACCESS) {
			assert(num_nodes == 4);
			cache_conf = new test_cache_config(cache_size, cache_type,
					node_id_array);
		}

		create_ios(raid_conf, cache_conf, node_id_array, num_threads, access_option,
				0, false);
	}
}

struct data_fill_struct
{
	pthread_t tid;
	int fd;
	off_t off;
	size_t tot_size;
};

static void *fill_space(void *arg)
{
	struct data_fill_struct *data = (struct data_fill_struct *) arg;
	size_t tot_size = data->tot_size;
	int fd = data->fd;
	off_t off = data->off;

	size_t block_size = 2 * 1024 * 1024;
	char *buf = (char *) valloc(block_size);
	memset(buf, 0, block_size);
	off_t ret = lseek(fd, off, SEEK_SET);
	if (ret < 0) {
		perror("lseek");
		abort();
	}
	size_t remaining_size = tot_size;
	while (remaining_size > 0) {
		size_t count = min(remaining_size, block_size);
		ssize_t written = write(fd, buf, count);
		if (written < 0) {
			perror("write");
			abort();
		}
		if (written == 0) {
			printf("WARNING! nothing was written\n");
		}
		remaining_size -= written;
	}
	free(buf);
	return NULL;
}

static size_t get_filesize(int fd)
{
	struct stat stat_buf;
	if (fstat(fd, &stat_buf) != 0) {
		perror("fstat");
		abort();
	}
	return stat_buf.st_size;
}

extern "C" {

void set_cache_size(long size)
{
	cache_size = size;
}

void set_cache_type(int type)
{
	cache_type = type;
}

void set_num_threads(int num)
{
	num_threads = num;
}

void set_num_nodes(int num)
{
	num_nodes = num;
}

void set_RAID_mapping_option(int option)
{
	RAID_mapping_option = option;
}

void set_RAID_block_size(int num_pages)
{
	RAID_block_size = num_pages;
}

int ssd_create(const char *name, size_t tot_size)
{
	std::vector<file_info> files;
	retrieve_data_files(name, files);
	size_t file_size = tot_size / files.size();
	struct data_fill_struct data[files.size()];
	for (unsigned i = 0; i < files.size(); i++) {
		int fd = open(files[i].name.c_str(), O_RDWR | O_CREAT | O_DIRECT,
				S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);
		if (fd < 0) {
			perror("open");
			abort();
		}

		size_t curr_size = get_filesize(fd);
		if (file_size > curr_size) {
			int err = posix_fallocate(fd, 0, file_size);
			if (err) {
				fprintf(stderr, "posix_fallocate error: %s\n", strerror(err));
				abort();
			}
			data[i].fd = fd;
			data[i].off = 0;
			data[i].tot_size = file_size;
			pthread_create(&data[i].tid, NULL, fill_space, &data[i]);
		}
		else {
			close(fd);
			data[i].fd = -1;
		}
	}
	for (unsigned i = 0; i < files.size(); i++) {
		if (data[i].fd >= 0) {
			pthread_join(data[i].tid, NULL);
			close(data[i].fd);
			printf("fill the file %s\n", files[i].name.c_str());
		}
	}
	return 0;
}

int ssd_open(const char *name, int flags)
{
	ssd_io_init(name, flags);

	io_interface *io = allocate_io();
	return io->get_io_idx();
}

ssize_t ssd_read(int fd, void *buf, size_t count, off_t off)
{
	io_interface *io = get_io(fd);
	io_status status = io->access((char *) buf, off, count, READ);
	assert(status == IO_OK);
	return status.get_priv_data();
}

ssize_t ssd_write(int fd, void *buf, size_t count, off_t off)
{
	io_interface *io = get_io(fd);
	io_status status = io->access((char *) buf, off, count, WRITE);
	assert(status == IO_OK);
	return status.get_priv_data();
}

int ssd_close(int fd)
{
	io_interface *io = get_io(fd);
	io->cleanup();
	release_io(io);
	return 0;
}

int ssd_delete(const char *name)
{
	std::vector<file_info> files;
	retrieve_data_files(name, files);
	for (unsigned i = 0; i < files.size(); i++) {
		int ret = unlink(files[i].name.c_str());
		if (ret < 0) {
			perror("unlink");
			abort();
		}
	}
	return 0;
}

size_t ssd_get_filesize(const char *name)
{
	std::vector<file_info> files;
	retrieve_data_files(name, files);
	size_t tot_size = 0;
	for (unsigned i = 0; i < files.size(); i++) {
		struct stat stat_buf;
		if (stat(files[i].name.c_str(), &stat_buf) != 0) {
			perror("stat");
			abort();
		}
		tot_size += stat_buf.st_size;
	}
	return tot_size;
}

}