/*
 * Copyright 2014 Open Connectome Project (http://openconnecto.me)
 * Written by Da Zheng (zhengda1936@gmail.com)
 *
 * This file is part of FlashMatrix.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <libgen.h>
#include <malloc.h>

#include <unordered_map>

#include "safs_file.h"
#include "io_request.h"

#include "EM_vector.h"

namespace fm
{

EM_vector::EM_vector(size_t length, size_t entry_size)
{
	accessor_count = 0;
	this->length = length;
	this->entry_size = entry_size;

	char *tmp = tempnam(".", "vec");
	std::string tmp_name = basename(tmp);
	safs::safs_file f(safs::get_sys_RAID_conf(), tmp_name);
	assert(!f.exist());
	bool ret = f.create_file(length * entry_size);
	assert(ret);
	factory = safs::create_io_factory(tmp_name, safs::REMOTE_ACCESS);
	free(tmp);
}

EM_vector::~EM_vector()
{
	assert(accessor_count == 0);
}

void EM_vector::resize(size_t length)
{
	// TODO
	assert(0);
}

class vec_callback: public safs::callback
{
	std::unordered_map<off_t, subvec_compute::ptr> computes;
public:
	virtual ~vec_callback() {
		assert(computes.empty());
	}

	void add(off_t off, subvec_compute::ptr compute) {
		auto ret = computes.insert(std::pair<off_t, subvec_compute::ptr>(
					off, compute));
		assert(ret.second);
	}

	virtual int invoke(safs::io_request *reqs[], int num) {
		for (int i = 0; i < num; i++) {
			off_t off = reqs[i]->get_offset();
			auto it = computes.find(off);
			it->second->run(reqs[i]->get_buf(), reqs[i]->get_size());
			assert(it != computes.end());
			computes.erase(it);
			if (reqs[i]->get_access_method() == READ)
				free(reqs[i]->get_buf());
		}
		return 0;
	}
};

void EM_vector_accessor::fetch_subvec(size_t start, size_t length,
		subvec_compute::ptr compute)
{
	off_t off = vec.get_byte_off(start);
	size_t size = length * vec.get_entry_size();
	assert(start % PAGE_SIZE == 0);
	assert(size % PAGE_SIZE == 0);
	char *buf = (char *) memalign(PAGE_SIZE, size);
	safs::data_loc_t loc(io->get_file_id(), off);
	safs::io_request req(buf, loc, size, READ);
	((vec_callback &) io->get_callback()).add(off, compute);
	io->access(&req, 1);
}

void EM_vector_accessor::set_subvec(const char *buf, size_t start, size_t length,
		subvec_compute::ptr compute)
{
	off_t off = vec.get_byte_off(start);
	size_t size = length * vec.get_entry_size();
	assert(start % PAGE_SIZE == 0);
	assert(size % PAGE_SIZE == 0);
	assert((long) buf % PAGE_SIZE == 0);

	safs::data_loc_t loc(io->get_file_id(), off);
	safs::io_request req((char *) buf, loc, size, WRITE);
	((vec_callback &) io->get_callback()).add(off, compute);
	io->access(&req, 1);
}

void EM_vector_accessor::wait4complete(int num)
{
	io->wait4complete(num);
}

void EM_vector_accessor::wait4all()
{
	io->wait4complete(io->num_pending_ios());
}

EM_vector_accessor::EM_vector_accessor(EM_vector &_vec,
		safs::file_io_factory::shared_ptr factory): vec(_vec)
{
	io = factory->create_io(thread::get_curr_thread());
	io->set_callback(safs::callback::ptr(new vec_callback()));
}

EM_vector_accessor::~EM_vector_accessor()
{
	io->wait4complete(io->num_pending_ios());
	io->cleanup();
	assert(io->num_pending_ios() == 0);
	vec.notify_destroy_accessor();
}

}