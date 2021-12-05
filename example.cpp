#include <iostream>

#define ECS_IMPL
#include "ecs.hpp"

struct Transform {
	float x, y;
};

struct Tag {
	const char* name;
};

static void on_tag_create(ecs::World& world, const ecs::Entity& e) {
	std::cout << "Tag create: " << e.get<Tag>().name << "\n";
}

static void on_tag_destroy(ecs::World& world, const ecs::Entity& e) {
	std::cout << "Tag destroy: " << e.get<Tag>().name << "\n";
}

ecs::i32 main() {
	ecs::World world;

	world.set_create_func<Tag>(on_tag_create);
	world.set_destroy_func<Tag>(on_tag_destroy);

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

	world.collect_garbage();
}
