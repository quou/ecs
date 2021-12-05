# ECS
A lightweight but complete ECS implementation for modern C++.

# Features
 - Cache friendly design implemented on top of an
[EnTT](https://github.com/skypjack/entt)-like sparse set.
 - Clean, C++ 11 API.
 - Lightweight and no-nonsense.
 - Doesn't use STL containers.
 - Single header.

# Examples

A more complete example can be found in
[example.cpp](https://github.com/veridisquot/ecs/blob/master/example.cpp)

A small game-ish project making use of this library can be found in 
[balls](https://github.com/veridisquot/ecs/blob/master/balls)

```cpp
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
```

# Documentation
## The World
The world is a generic container and manager for entities and components.

The world contains a public member of type `void` pointer. This, for example,
can be used to pass extra data through the component create and destroy functions.

### Methods
| Signature | Description |
|--|-|
| `u64 count()` | Returns the amount of entities.|
| `Entity at(u64 i)` | Returns whatever entity is at index `i` in the entity array.|
| `void set_create_func<T>(std::function<void(World&, const Entity&)>)` | Sets the create function for a given component type `T`. |
| `void set_destroy_func<T>(std::function<void(World&, const Entity&)>)` | Sets the destroy function for a given component type `T`. |
| `Entity new_entity()` | Creates a new entity |
| `void collect_garbage()` | Frees up any large amounts of memory that isn't needed any more. |
| `View new_view<Types...>` | Creates a view that iterates components of `Types...` |

## Entities
Entities can be represented by two different types: The `Entity` class and the
`Entity_Handle`. An entity handle is a 64-bit unsigned integer, where the first 32 bits
contain the entity's ID and the last 32 bits contain the entity's version. The version
represents how many times the entity ID has been recycled (deleted and re-created).

The `Entity` class is what should be used to interface with the library. It contains
an `Entity_Handle` as well as a pointer to the `World` that the entity was created in.
It also has methods for adding and removing components and `==` and `!=` operator
overloads to make comparing entities easy.

By default, the entity contains a `null_handle` and a zero pointer for the world.

### Methods
| Signature | Description |
|--|--|
| `static Entity null()` | Returns an entity with `null_handle` and a zero pointer for the world. |
| `bool valid()` | Returns true if the entity is valid |
| `void destroy()` | Destroys the entities and its components |
| `bool has<T>()` | Returns true if the entity has the component of type `T` |
| `T& add<T>()` | Adds a component of type `T` to the entity. Returns a reference to the new component. |
| `T& get<T>()` | Returns a reference to the component of type `T` attached to the entity. |
| `void remove<T>()` | Removes the component of type `T` from the entity. |
| `u32 get_id()` | Returns the entity's ID. |
| `u32 get_version()` | Returns the entity's version. |
| `Entity_Handle get_handle()` | Returns the entity's handle. |
| `World& get_world()` | Returns the world that the entity belongs to. |

## Components
Due to the way that the library handles memory, components should be simple, C-style
structs, meaning they shouldn't inherit from a base class or contain virtual methods,
nor should they have constructors or destructors. If you wish to run extra logic upon
the creation or destruction of a component, you may give functions to the `World` using
the `set_create_func` and `set_destroy_func` methods in order to be notified when a
specified component type is created or destroyed.

For speed, pointers and references inside components should be avoided where possible.

## Views
Views are used to iterate components of a specified type. Views can iterate up to 16
different types of component at once. Creating components while iterating a view should
generally be avoided, because if component memory is re-allocated, then all current
components of that type will point to old copies of memory, effectively making them
read-only. If you must create components while iterating a view, it is recommended
that you do it at the end of the view rather than in the middle.

The `get` method on the `View` class is an unsafe function: No checks are performed to make
sure that only the components that the view is iterating are "got"; Doing this will cause
undefined behaviour.

The world will not de-allocate memory while iterating a view, rather it will delay the
de-allocation until the view has finished. This means that references will not be
invalidated during iteration of a view. However, a reference kept outside the scope of
a view may be invalidated and should not be used. `World::collect_garbage` could also
invalidate references and should not be called during the iteration of a view. Doing so
may cause undefined behaviour.

## Example

```cpp
// Using a `while' loop:
auto view_while = world.new_view<Transform>();
while (view_while.valid()) {
	auto e = view_while.get_entity();

	view_while.next();
}

// Using a `for' loop:
for (auto view_for = world.new_view<Transform>(); view_for.valid(); view_for.next()) {
	auto& transform = view_for.get<Transform>();
}
```

## Methods
| Signature | Description |
|--|--|
| `void next()` | Continue to the next iteration |
| `bool valid()` | Returns false if the view has finished iterating. |
| `T& get<T>` | Returns a reference to the current component of type `T`. |
| `Entity get_entity()` | Returns the current entity. |

## Memory
By default, this implementation doesn't de-allocate any memory, rather, old memory is
kept around and re-used should more components be created. If you want to keep memory
usage to a minimum, you can call `World::collect_garbage` to clear up memory that's not
in use. Keep in mind that a call to this method could be slow if a lot of memory
needs de-allocating.
