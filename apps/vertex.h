#ifndef __EXT_VERTEX_H__
#define __EXT_VERTEX_H__

#include <stdlib.h>
#include <assert.h>

#include <vector>

typedef unsigned long vertex_id_t;

/**
 * This class contains the basic information of a vertex in the memory.
 */
class in_mem_vertex_info
{
	vertex_id_t id;
	off_t off;
	int size;
public:
	in_mem_vertex_info() {
		off = 0;
		size = 0;
		id = -1;
	}

	in_mem_vertex_info(off_t off, int size) {
		this->id = -1;
		this->off = off;
		this->size = size;
	}

	off_t get_ext_mem_off() const {
		return off;
	}

	int get_ext_mem_size() const {
		return size;
	}

	vertex_id_t get_id() const {
		return id;
	}
};

class edge
{
	vertex_id_t from;
	vertex_id_t to;
public:
	edge() {
		this->from = -1;
		this->to = -1;
	}

	edge(vertex_id_t from, vertex_id_t to) {
		this->from = from;
		this->to = to;
	}

	vertex_id_t get_from() const {
		return from;
	}

	vertex_id_t get_to() const {
		return to;
	}
};

class in_mem_directed_vertex;
class in_mem_undirected_vertex;

/**
 * This vertex represents a directed vertex stored in the external memory.
 */
class ext_mem_directed_vertex
{
	vertex_id_t id;
	int num_in_edges;
	int num_out_edges;
	vertex_id_t neighbors[0];
public:
	static ext_mem_directed_vertex *deserialize(char *buf, int size) {
		assert((unsigned) size >= sizeof(ext_mem_directed_vertex));
		ext_mem_directed_vertex *v = (ext_mem_directed_vertex *) buf;
		assert((unsigned) size >= sizeof(ext_mem_directed_vertex)
				+ (v->num_in_edges + v->num_out_edges) * sizeof(v->neighbors[0]));
		return v;
	}

	static int serialize(const in_mem_directed_vertex &in_v, char *buf,
			int size);

	int get_num_in_edges() const {
		return num_in_edges;
	}

	int get_num_out_edges() const {
		return num_out_edges;
	}

	const edge get_in_edge(int idx) const {
		assert(idx < num_in_edges);
		edge e(neighbors[idx], id);
		return e;
	}

	const edge get_out_edge(int idx) const {
		assert(idx < num_out_edges);
		edge e(id, neighbors[num_in_edges + idx]);
		return e;
	}

	const vertex_id_t get_id() const {
		return id;
	}
};

/**
 * This vertex represents an undirected vertex in the external memory.
 */
class ext_mem_undirected_vertex
{
	vertex_id_t id;
	int num_edges;
	vertex_id_t neighbors[0];
public:
	static ext_mem_undirected_vertex *deserialize(char *buf, int size) {
		assert((unsigned) size >= sizeof(ext_mem_undirected_vertex));
		ext_mem_undirected_vertex *v = (ext_mem_undirected_vertex *) buf;
		assert((unsigned) size >= sizeof(ext_mem_undirected_vertex)
				+ sizeof(v->neighbors[0]) * v->num_edges);
		return v;
	}

	static int serialize(const in_mem_undirected_vertex &v, char *buf,
			int size);

	int get_num_edges() const {
		return num_edges;
	}

	const edge get_edge(int idx) const {
		assert(idx < num_edges);
		edge e(id, neighbors[idx]);
		return e;
	}

	vertex_id_t get_id() const {
		return id;
	}
};

class in_mem_directed_vertex
{
	vertex_id_t id;
	std::vector<vertex_id_t> out_edges;
	std::vector<vertex_id_t> in_edges;
public:
	in_mem_directed_vertex(vertex_id_t id) {
		this->id = id;
	}

	vertex_id_t get_id() const {
		return id;
	}

	void add_in_edge(vertex_id_t id) {
		in_edges.push_back(id);
	}

	void add_out_edge(vertex_id_t id) {
		out_edges.push_back(id);
	}

	int get_num_in_edges() const {
		return in_edges.size();
	}

	int get_num_out_edges() const {
		return out_edges.size();
	}

	const edge get_in_edge(int idx) const {
		edge e(in_edges[idx], id);
		return e;
	}

	const edge get_out_edge(int idx) const {
		edge e(id, out_edges[idx]);
		return e;
	}

	int get_serialize_size() const {
		return sizeof(ext_mem_directed_vertex)
			+ sizeof(vertex_id_t) * (get_num_in_edges() + get_num_out_edges());
	}
};

class in_mem_undirected_vertex
{
	vertex_id_t id;
	std::vector<vertex_id_t> edges;
public:
	in_mem_undirected_vertex(vertex_id_t id) {
		this->id = id;
	}

	vertex_id_t get_id() const {
		return id;
	}

	void add_edge(vertex_id_t id) {
		edges.push_back(id);
	}

	int get_num_edges() const {
		return edges.size();
	}

	bool has_edge(vertex_id_t id) const {
		for (size_t i = 0; i < edges.size(); i++)
			if (edges[i] == id)
				return true;
		return false;
	}

	const edge get_edge(int idx) const {
		edge e(id, edges[idx]);
		return e;
	}

	int get_serialize_size() const {
		return sizeof(ext_mem_undirected_vertex)
			+ sizeof(vertex_id_t) * get_num_edges();
	}
};

#endif