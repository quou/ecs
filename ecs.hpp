/* Copyright 2021 veridis_quo_t
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the Software
 * is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
 * OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 */

/* Before using this software, #define ECS_IMPL in *one* C++ source file. */

/* == Compilable Example ==
	c++ example.cpp

#include <iostream>

#define ECS_IMPL
#include "ecs.hpp"

struct Transform {
	float x, y;
};

struct Tag {
	const char* name;
};

ecs::i32 main() {
	ecs::World world;

	auto e = world.new_entity();
	e.add<Transform>(Transform { 5, 3 });
	e.add<Tag>(Tag { "Bob" });

	e = world.new_entity();
	e.add<Transform>(Transform { 3, 55 });
	e.add<Tag>(Tag { "Alice" });

	for (auto view = world.new_view<Tag, Transform>(); view.valid(); view.next()) {
		auto e = view.get_entity();
		auto& trans = view.get<Transform>();
		auto& tag = view.get<Tag>();

		std::cout << tag.name << ": " <<  trans.x << ", " << trans.y << "\n";
	}
}
*/

#pragma once

#include <stdint.h>
#include <assert.h>
#include <string.h>

#include <functional>

namespace ecs {
	typedef uint8_t  u8;
	typedef uint16_t u16;
	typedef uint32_t u32;
	typedef uint64_t u64;
	typedef int8_t   i8;
	typedef int16_t  i16;
	typedef int32_t  i32;
	typedef int64_t  i64;

	typedef u64 Entity_Handle;
	typedef u32 Entity_ID;
	typedef u32 Entity_Version;

	static const Entity_Handle null_handle = UINT64_MAX;
	static const Entity_ID null_entity_id = UINT32_MAX;	

	class Entity;
	class World;
	class View;

	using Component_Create_Func = std::function<void(World&, const Entity&)>;
	using Component_Destroy_Func = std::function<void(World&, const Entity&)>;

	namespace internal {
		Entity_Version get_entity_version(Entity_Handle e);
		Entity_ID get_entity_id(Entity_Handle e);
		Entity_Handle make_handle(Entity_ID id, Entity_Version v);

		u64 get_unique_component_id();

		template <typename T>
		inline static u64 get_component_id() {
			static u64 id = get_unique_component_id();
			return id;
		}

		template <typename T>
		static void mem_copy(T* dst, T* src, u64 count) {
			for (u64 i = 0; i < count; i++) {
				dst[i] = src[i];
			}
		}

		class Component_Pool {
		public:
			i64* sparse = nullptr;
			u64 sparse_capacity = 0;

			Entity_Handle* dense = nullptr;
			u64 dense_count = 0;
			u64 dense_capacity = 0;

			u8* data = nullptr;
			u64 count = 0;
			u64 capacity = 0;

			u64 element_size = 0;
			u64 id = UINT64_MAX;

			World* world = nullptr;

			Component_Create_Func on_create;
			Component_Destroy_Func on_destroy;

			void init(World* w, u64 type_id, u64 el_size) {
				element_size = el_size;
				id = type_id;
				world = w;
			}

			void deinit();

			i64 sparse_idx(Entity_Handle e) const {
				return sparse[get_entity_id(e)];
			}

			bool has(Entity_Handle e) const {
				const Entity_ID id = get_entity_id(e);
				return ((u64)id < sparse_capacity) && (sparse[id] != -1);
			}

			void* get_by_idx(u64 idx) {
				return &data[idx * element_size];
			}

			void* get(Entity_Handle e) {
				return get_by_idx(sparse_idx(e));
			}

			void* add(Entity_Handle e);
			void remove(Entity_Handle e);
		};
	}

	class View {
	public:
		static const u64 max = 16;

		friend class World;
	private:
		u64 to_pool[max];
		internal::Component_Pool* pools[max];
		u64 pool_count = 0;
		internal::Component_Pool* pool = nullptr;
		u64 idx = 0;
		Entity_Handle entity = null_handle;
		World* world = nullptr;

		bool contains(Entity_Handle handle) {
			for (u64 i = 0; i < pool_count; i++) {
				if (!pools[i]->has(handle)) {
					return false;
				}
			}

			return true;
		}

		u64 get_idx(u64 id) {
			for (u64 i = 0; i < pool_count; i++) {
				if (to_pool[i] == id) {
					return i;
				}
			}

			return 0;
		}
	public:
		void next() {
			do {
				if (idx) {
					idx--;
					entity = pool->dense[idx];
				} else {
					entity = null_handle;
				}
			} while ((entity != null_handle) && !contains(entity));
		}

		bool valid();

		template <typename T>
		T& get() {
			return *(T*)pools[get_idx(internal::get_component_id<T>())]->get(entity);
		}

		Entity get_entity() const;
	};

	class World {
	private:
		friend class Entity;
		friend class View;
		friend class internal::Component_Pool;

		Entity_Handle* entities = nullptr;
		u64 entity_capacity = 0;
		u64 entity_count = 0;
		u64 alive_count = 0;

		Entity_ID avail_id = null_entity_id;

		internal::Component_Pool* pools = nullptr;
		u64 pool_count = 0;
		u64 pool_capacity = 0;

		enum class Delete_Type {
			U8,
			U64,
			I64,
			COMPONENT_POOL,
			ENTITY_HANDLE
		};

		struct Deletion_Entry {
			Delete_Type type;
			void* ptr;
		};

		static const u64 max_delete_buffer = 64;
		Deletion_Entry delete_buffer[max_delete_buffer];
		u64 delete_buffer_count = 0;

		i64 iteration_depth = 0;

		template <typename T>
		internal::Component_Pool& get_pool() {
			u64 id = internal::get_component_id<T>();

			for (u64 i = 0; i < pool_count; i++) {
				if (pools[i].id == id) {
					return *(pools + i);
				}
			}
			
			if (pool_count >= pool_capacity) {
				u64 new_capacity = pool_capacity < 8 ? 8 : pool_capacity * 2;
				auto* new_alloc = new internal::Component_Pool[new_capacity];
				if (pools) {
					mem_copy(new_alloc, pools, pool_capacity);

					if (iteration_depth <= 0) {
						delete[] pools;
					} else {
						push_deletion(Delete_Type::COMPONENT_POOL, pools);
					}
				}

				pools = new_alloc;
				pool_capacity = new_capacity;
			}

			auto p = pools + (pool_count++);
			p->init(this, id, sizeof(T));
			return *p;
		}
		
		Entity_Handle generate_entity() {
			if (entity_count >= entity_capacity) {
				u64 new_capacity = entity_capacity < 8 ? 8 : entity_capacity * 2;
				Entity_Handle* new_alloc = new Entity_Handle[new_capacity];
				internal::mem_copy(new_alloc, entities, entity_capacity);

				delete[] entities;

				entity_capacity = new_capacity;
				entities = new_alloc;
			}

			const Entity_Handle e = internal::make_handle(entity_count, 0);
			entities[entity_count++] = e;

			return e;
		}

		Entity_Handle recycle_entity() {
			const Entity_ID cur_id = avail_id;
			const Entity_Version cur_version = internal::get_entity_version(entities[cur_id]);

			avail_id = internal::get_entity_id(entities[cur_id]);

			const Entity_Handle recycled = internal::make_handle(cur_id, cur_version);

			entities[cur_id] = recycled;

			return recycled;
		}

		void release_entity(Entity_Handle e, Entity_Version desired) {
			const Entity_ID id = internal::get_entity_id(e);
			entities[id] = internal::make_handle(avail_id, desired);
			avail_id = id;
		}

		View _new_view(u64 type_count, u64* types) {
			View v;
			v.world = this;
			v.pool_count = type_count;

			iteration_depth++;

			for (u64 i = 0; i < type_count; i++) {
				v.pools[i] = nullptr;

				for (u64 ii = 0; ii < pool_count; ii++) {
					if (pools[ii].id == types[i]) {
						v.pools[i] = pools + ii;
						break;
					}
				}

				if (!v.pools[i]) {
					View nv;
					nv.world = this;
					return nv;
				}

				if (!v.pool) {
					v.pool = v.pools[i];
				} else if (v.pools[i]->count < v.pool->count) {
					v.pool = v.pools[i];
				}
				v.to_pool[i] = types[i];
			}

			if (v.pool && v.pool->count != 0) {
				v.idx = v.pool->count - 1;
				v.entity = v.pool->dense[v.idx];
				if (!v.contains(v.entity)) {
					v.next();
				}
			} else {
				v.idx = 0;
				v.entity = null_handle;
			}

			return v;
		}

		void push_deletion(Delete_Type type, void* ptr) {
			assert(delete_buffer_count < max_delete_buffer && "Deletion buffer too small.");

			delete_buffer[delete_buffer_count++] = Deletion_Entry { type, ptr };
		}

		void commit_deletions() {
			for (u64 i = 0; i < delete_buffer_count; i++) {
				auto& entry = delete_buffer[i];

				switch (entry.type) {
					case Delete_Type::U8:
						delete[] (u8*)entry.ptr;
						break;
					case Delete_Type::U64:
						delete[] (u64*)entry.ptr;
						break;
					case Delete_Type::I64:
						delete[] (i64*)entry.ptr;
						break;
					case Delete_Type::COMPONENT_POOL:
						delete[] (internal::Component_Pool*)entry.ptr;
						break;
					case Delete_Type::ENTITY_HANDLE:
						delete[] (Entity_Handle*)entry.ptr;
						break;
					default: break;
				}
			}

			delete_buffer_count = 0;
		}
	public:
		void* uptr;

		virtual ~World() {
			for (u64 i = 0; i < pool_count; i++) {
				pools[i].deinit();
			}

			delete[] pools;
			delete[] entities;
		}

		u64 count() const {
			return alive_count;
		}

		template <typename T>
		void set_create_func(Component_Create_Func f) {
			get_pool<T>().on_create = f;
		}

		template <typename T>
		void set_destroy_func(Component_Destroy_Func f) {
			get_pool<T>().on_destroy = f;
		}

		Entity at(u64 i);

		Entity new_entity();

		void collect_garbage() {
			commit_deletions();

			for (u64 i = 0; i < pool_count; i++) {
				auto p = pools + i;

				if (p->count > 8 && p->capacity > p->count * 2) {
					p->capacity = p->count;
					u64 r = p->capacity % 8;
					if (r != 0) {
						p->capacity += 8 - r;
					}

					u8* new_alloc = new u8[p->capacity * p->element_size];
					internal::mem_copy(new_alloc, p->data, p->capacity);
					delete[] p->data;
					p->data = new_alloc;
				}
			}
		}
		
		template <typename T>
		View new_view() {
			u64 id = internal::get_component_id<T>();
			return _new_view(1, &id);
		}

		template <typename A, typename B>
		View new_view() {
			u64 ids[] = {
				internal::get_component_id<A>(),
				internal::get_component_id<B>()
			};
			return _new_view(2, ids);
		}

		template <
			typename A,
			typename B,
			typename C>
		View new_view() {
			u64 ids[] = {
				internal::get_component_id<A>(),
				internal::get_component_id<B>(),
				internal::get_component_id<C>()
			};
			return _new_view(3, ids);
		}

		template <
			typename A,
			typename B,
			typename C,
			typename D>
		View new_view() {
			u64 ids[] = {
				internal::get_component_id<A>(),
				internal::get_component_id<B>(),
				internal::get_component_id<C>(),
				internal::get_component_id<D>()
			};
			return _new_view(4, ids);
		}

		template <
			typename A,
			typename B,
			typename C,
			typename D,
			typename E>
		View new_view() {
			u64 ids[] = {
				internal::get_component_id<A>(),
				internal::get_component_id<B>(),
				internal::get_component_id<C>(),
				internal::get_component_id<D>(),
				internal::get_component_id<E>()
			};
			return _new_view(5, ids);
		}

		template <
			typename A,
			typename B,
			typename C,
			typename D,
			typename E,
			typename F>
		View new_view() {
			u64 ids[] = {
				internal::get_component_id<A>(),
				internal::get_component_id<B>(),
				internal::get_component_id<C>(),
				internal::get_component_id<D>(),
				internal::get_component_id<E>(),
				internal::get_component_id<F>()
			};
			return _new_view(6, ids);
		}

		template <
			typename A,
			typename B,
			typename C,
			typename D,
			typename E,
			typename F,
			typename G>
		View new_view() {
			u64 ids[] = {
				internal::get_component_id<A>(),
				internal::get_component_id<B>(),
				internal::get_component_id<C>(),
				internal::get_component_id<D>(),
				internal::get_component_id<E>(),
				internal::get_component_id<F>(),
				internal::get_component_id<G>()
			};
			return _new_view(7, ids);
		}

		template <
			typename A,
			typename B,
			typename C,
			typename D,
			typename E,
			typename F,
			typename G,
			typename H>
		View new_view() {
			u64 ids[] = {
				internal::get_component_id<A>(),
				internal::get_component_id<B>(),
				internal::get_component_id<C>(),
				internal::get_component_id<D>(),
				internal::get_component_id<E>(),
				internal::get_component_id<F>(),
				internal::get_component_id<G>(),
				internal::get_component_id<H>()
			};
			return _new_view(8, ids);
		}

		template <
			typename A,
			typename B,
			typename C,
			typename D,
			typename E,
			typename F,
			typename G,
			typename H,
			typename I>
		View new_view() {
			u64 ids[] = {
				internal::get_component_id<A>(),
				internal::get_component_id<B>(),
				internal::get_component_id<C>(),
				internal::get_component_id<D>(),
				internal::get_component_id<E>(),
				internal::get_component_id<F>(),
				internal::get_component_id<G>(),
				internal::get_component_id<H>(),
				internal::get_component_id<I>()
			};
			return _new_view(9, ids);
		}

		template <
			typename A,
			typename B,
			typename C,
			typename D,
			typename E,
			typename F,
			typename G,
			typename H,
			typename I,
			typename J>
		View new_view() {
			u64 ids[] = {
				internal::get_component_id<A>(),
				internal::get_component_id<B>(),
				internal::get_component_id<C>(),
				internal::get_component_id<D>(),
				internal::get_component_id<E>(),
				internal::get_component_id<F>(),
				internal::get_component_id<G>(),
				internal::get_component_id<H>(),
				internal::get_component_id<I>(),
				internal::get_component_id<J>()
			};
			return _new_view(10, ids);
		}

		template <
			typename A,
			typename B,
			typename C,
			typename D,
			typename E,
			typename F,
			typename G,
			typename H,
			typename I,
			typename J,
			typename K>
		View new_view() {
			u64 ids[] = {
				internal::get_component_id<A>(),
				internal::get_component_id<B>(),
				internal::get_component_id<C>(),
				internal::get_component_id<D>(),
				internal::get_component_id<E>(),
				internal::get_component_id<F>(),
				internal::get_component_id<G>(),
				internal::get_component_id<H>(),
				internal::get_component_id<I>(),
				internal::get_component_id<J>(),
				internal::get_component_id<K>()
			};
			return _new_view(11, ids);
		}

		template <
			typename A,
			typename B,
			typename C,
			typename D,
			typename E,
			typename F,
			typename G,
			typename H,
			typename I,
			typename J,
			typename K,
			typename L>
		View new_view() {
			u64 ids[] = {
				internal::get_component_id<A>(),
				internal::get_component_id<B>(),
				internal::get_component_id<C>(),
				internal::get_component_id<D>(),
				internal::get_component_id<E>(),
				internal::get_component_id<F>(),
				internal::get_component_id<G>(),
				internal::get_component_id<H>(),
				internal::get_component_id<I>(),
				internal::get_component_id<J>(),
				internal::get_component_id<K>(),
				internal::get_component_id<L>()
			};
			return _new_view(12, ids);
		}

		template <
			typename A,
			typename B,
			typename C,
			typename D,
			typename E,
			typename F,
			typename G,
			typename H,
			typename I,
			typename J,
			typename K,
			typename L,
			typename M>
		View new_view() {
			u64 ids[] = {
				internal::get_component_id<A>(),
				internal::get_component_id<B>(),
				internal::get_component_id<C>(),
				internal::get_component_id<D>(),
				internal::get_component_id<E>(),
				internal::get_component_id<F>(),
				internal::get_component_id<G>(),
				internal::get_component_id<H>(),
				internal::get_component_id<I>(),
				internal::get_component_id<J>(),
				internal::get_component_id<K>(),
				internal::get_component_id<L>(),
				internal::get_component_id<M>()
			};
			return _new_view(13, ids);
		}

		template <
			typename A,
			typename B,
			typename C,
			typename D,
			typename E,
			typename F,
			typename G,
			typename H,
			typename I,
			typename J,
			typename K,
			typename L,
			typename M,
			typename N>
		View new_view() {
			u64 ids[] = {
				internal::get_component_id<A>(),
				internal::get_component_id<B>(),
				internal::get_component_id<C>(),
				internal::get_component_id<D>(),
				internal::get_component_id<E>(),
				internal::get_component_id<F>(),
				internal::get_component_id<G>(),
				internal::get_component_id<H>(),
				internal::get_component_id<I>(),
				internal::get_component_id<J>(),
				internal::get_component_id<K>(),
				internal::get_component_id<L>(),
				internal::get_component_id<M>(),
				internal::get_component_id<N>()
			};
			return _new_view(14, ids);
		}

		template <
			typename A,
			typename B,
			typename C,
			typename D,
			typename E,
			typename F,
			typename G,
			typename H,
			typename I,
			typename J,
			typename K,
			typename L,
			typename M,
			typename N,
			typename O>
		View new_view() {
			u64 ids[] = {
				internal::get_component_id<A>(),
				internal::get_component_id<B>(),
				internal::get_component_id<C>(),
				internal::get_component_id<D>(),
				internal::get_component_id<E>(),
				internal::get_component_id<F>(),
				internal::get_component_id<G>(),
				internal::get_component_id<H>(),
				internal::get_component_id<I>(),
				internal::get_component_id<J>(),
				internal::get_component_id<K>(),
				internal::get_component_id<L>(),
				internal::get_component_id<M>(),
				internal::get_component_id<N>(),
				internal::get_component_id<O>()
			};
			return _new_view(15, ids);
		}

		template <
			typename A,
			typename B,
			typename C,
			typename D,
			typename E,
			typename F,
			typename G,
			typename H,
			typename I,
			typename J,
			typename K,
			typename L,
			typename M,
			typename N,
			typename O,
			typename P>
		View new_view() {
			u64 ids[] = {
				internal::get_component_id<A>(),
				internal::get_component_id<B>(),
				internal::get_component_id<C>(),
				internal::get_component_id<D>(),
				internal::get_component_id<E>(),
				internal::get_component_id<F>(),
				internal::get_component_id<G>(),
				internal::get_component_id<H>(),
				internal::get_component_id<I>(),
				internal::get_component_id<J>(),
				internal::get_component_id<K>(),
				internal::get_component_id<L>(),
				internal::get_component_id<M>(),
				internal::get_component_id<N>(),
				internal::get_component_id<O>(),
				internal::get_component_id<P>()
			};
			return _new_view(16, ids);
		}
	};

	class Entity {
	private:
		Entity_Handle handle = null_handle;
		World* world = nullptr;
	public:
		Entity() {}
		Entity(Entity_Handle h, World* w) : handle(h), world(w) {}
		Entity(const Entity& other) : handle(other.handle), world(other.world) {}

		static Entity null() { return Entity(null_handle, nullptr); }

		bool valid() const {
			const Entity_ID id = internal::get_entity_id(handle);
			return world && id < world->entity_count && world->entities[id] == handle;
		}

		void destroy() {
			assert(valid() && "Invalid entity.");

			for (u64 i = 0; i < world->pool_count; i++) {
				if (world->pools[i].has(handle)) {
					world->pools[i].remove(handle);
				}
			}

			const Entity_Version nv = (Entity_Version)internal::get_entity_version(handle) + 1;
			world->release_entity(handle, nv);

			world->alive_count--;
		}

		template <typename T>
		bool has() const {	
			assert(valid() && "Invalid entity.");

			return world->get_pool<T>().has(handle);
		}

		template <typename T>
		T& add(T c) {
			assert(valid() && "Invalid entity.");
			assert(!has<T>() && "Entity already has the specified component.");

			auto& pool = world->get_pool<T>();

			T* n = (T*)pool.add(handle);

			*n = c;

			if (pool.on_create) {
				pool.on_create(*world, *this);
			}

			return *n;
		}

		template <typename T>
		T& get() {
			assert(valid() && "Invalid entity.");
			assert(has<T>() && "Entity doesn't have the requested component.");

			return *(T*)world->get_pool<T>().get(handle);
		}

		template <typename T>
		const T& get() const {
			assert(valid() && "Invalid entity.");
			assert(has<T>() && "Entity doesn't have the requested component.");

			return *(T*)world->get_pool<T>().get(handle);
		}

		template <typename T>
		void remove() {
			assert(valid() && "Invalid entity.");
			assert(has<T>() && "Entity doesn't have the requested component.");

			world->get_pool<T>().remove(handle);
		}

		bool operator==(const Entity& r) {
			return handle == r.handle && world == r.world;
		}

		bool operator!=(const Entity& r) {
			return !(*this == r);
		}

		void operator=(const Entity& other) {
			handle = other.handle;
			world = other.world;
		}

		u32 get_id() const {
			assert(valid() && "Invalid entity.");

			return internal::get_entity_id(handle);
		}

		u32 get_version() const {
			assert(valid() && "Invalid entity.");

			return internal::get_entity_version(handle);
		}

		Entity_Handle get_handle() const {
			return handle;
		}

		World& get_world() {
			return *world;
		}

		const World& get_world() const {
			return *world;
		}
	};

#ifdef ECS_IMPL
	namespace internal {
		Entity_Version get_entity_version(Entity_Handle e) {
			return e >> 32;
		}

		Entity_ID get_entity_id(Entity_Handle e) {
			return (Entity_ID)e;
		}

		Entity_Handle make_handle(Entity_ID id, Entity_Version v) {
			return ((Entity_Handle)v << 32) | id;
		}

		u64 get_unique_component_id() {
			static u64 id = 0;
			return id++;
		}

		void* Component_Pool::add(Entity_Handle e) {
			if (count >= capacity) {
				u64 new_capacity = capacity < 8 ? 8 : capacity * 2;
				u8* new_alloc = new u8[new_capacity * element_size];
				if (data) {
					mem_copy(new_alloc, data, capacity * element_size);

					if (world->iteration_depth <= 0) {
						delete[] data;
					} else {
						world->push_deletion(World::Delete_Type::U8, data);
					}
				}

				capacity = new_capacity;
				data = new_alloc;
			}

			void* new_el = &data[(count++) * element_size];

			const u64 eid = (u64)get_entity_id(e);
			if (eid >= sparse_capacity) {
				const u64 new_capacity = eid + 1;
				i64* new_alloc = new i64[new_capacity];
				if (sparse) {
					mem_copy(new_alloc, sparse, sparse_capacity);

					if (world->iteration_depth <= 0) {
						delete[] sparse;
					} else {
						world->push_deletion(World::Delete_Type::I64, sparse);
					}
				}

				sparse = new_alloc;

				for (u64 i = sparse_capacity; i < new_capacity; i++) {
					sparse[i] = -1;
				}

				sparse_capacity = new_capacity;
			}

			sparse[eid] = dense_count;

			if (dense_count >= dense_capacity) {
				u64 new_capacity = dense_capacity < 8 ? 8 : dense_capacity * 2;
				Entity_Handle* new_alloc = new Entity_Handle[new_capacity];
				if (dense) {
					mem_copy(new_alloc, dense, dense_capacity);

					if (world->iteration_depth <= 0) {
						delete[] dense;
					} else {
						world->push_deletion(World::Delete_Type::ENTITY_HANDLE, dense);
					}
				}

				dense_capacity = new_capacity;
				dense = new_alloc;
			}

			dense[dense_count++] = e;

			return new_el;
		}

		void Component_Pool::remove(Entity_Handle e) {
			if (on_destroy) {
				on_destroy(*world, Entity(e, world));
			}

			const i64 pos = sparse[get_entity_id(e)];
			const Entity_Handle other = dense[dense_count - 1];

			sparse[get_entity_id(other)] = pos;
			dense[pos] = other;
			sparse[get_entity_id(e)] = -1;

			memmove(&data[pos * element_size], &data[(count - 1) * element_size], element_size);

			dense_count--;
			count--;
		}
	
		void Component_Pool::deinit() {
			if (on_destroy) {
				for (u64 i = 0; i < dense_count; i++) {
					on_destroy(*world, Entity(dense[i], world));
				}
			}

			delete[] sparse;
			delete[] dense;
			delete[] data;
		}
	}

	Entity World::new_entity() {
		alive_count++;

		if (avail_id == null_entity_id) {
			return Entity(generate_entity(), this);
		} else {
			return Entity(recycle_entity(), this);
		}
	}

	Entity World::at(u64 i) {
		return Entity(entities[i], this);
	}

	bool View::valid() {
		bool valid = entity != null_handle;

		if (!valid) {
			world->iteration_depth--;

			if (world->iteration_depth <= 0) {
				world->commit_deletions();
			}
		}

		return valid;
	}

	Entity View::get_entity() const {
		return Entity(entity, world);
	}
#endif
}
