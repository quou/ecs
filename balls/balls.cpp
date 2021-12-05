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

#include <stdio.h>
#include <string.h>

#include <memory>
#include <complex>
#include <random>

#include <glad/glad.h>
#include <GLFW/glfw3.h>

#define ECS_IMPL
#include <ecs.hpp>

using namespace ecs;

static const char* const vert_shader_src =
"#version 330 core\n"

"layout (location = 0) in vec2 position;\n"
"layout (location = 1) in vec2 uv;\n"
"layout (location = 2) in vec4 color;\n"
"layout (location = 3) in float is_circle;"

"out VS_OUT {\n"
"	vec4 color;\n"
"	vec2 uv;\n"
"	float is_circle;\n"
"} vs_out;\n"

"uniform mat4 camera;\n"

"void main() {\n"
"	vs_out.color = color;\n"
"	vs_out.uv = uv;\n"
"	vs_out.is_circle = is_circle;\n"

"	gl_Position = camera * vec4(position, 0.0, 1.0);\n"
"}\n";

static const char* const frag_shader_src =
"#version 330 core\n"

"in VS_OUT {\n"
"	vec4 color;\n"
"	vec2 uv;\n"
"	float is_circle;\n"
"} fs_in;\n"

"out vec4 color;\n"

"void main() {\n"
"	if (fs_in.is_circle == 1.0f) {"
"		float distance = length(fs_in.uv - vec2(0.5, 0.5));\n"
"		float circle = smoothstep(0.0, 0.005, distance);\n"
"		circle *= smoothstep(0.5 + 0.005, 0.5, distance);\n"

"		if (circle == 0) { discard; }\n"

"		color = vec4(circle) * fs_in.color;\n"
"	} else {"
"		color = fs_in.color;\n"
"	}\n"
"}\n";

/* Bare-bones but fast batch renderer. Can draw solid
 * quads and circles, but not textures. */
class Renderer {
private:
	u32 shader;

	u32 va, vb, ib;

	static const u64 max_quads = 800;
	static const u64 vpq = 4;
	static const u64 vpv = 9;
	u64 quad_count = 0;
public:
	void update_camera(float width, float height) {
		float m[4][4];

		for (u64 j = 0; j < 4; j++) {
			for (u64 i = 0; i < 4; i++) {
				m[i][j] = 0.0f;
			}
		}
		m[0][0] = 1.0f;
		m[1][1] = 1.0f;
		m[2][2] = 1.0f;
		m[3][3] = 1.0f;

		float l = -width / 2.0f;
		float r = width / 2.0f;
		float b = height / 2.0f;
		float t = -height / 2.0f;
		float n = -1.0f;
		float f = 1.0f;

		m[0][0] = 2.0f / (r - l);
		m[1][1] = 2.0f / (t - b);
		m[2][2] = 2.0f / (n - f);

		m[3][0] = (l + r) / (l - r);
		m[3][1] = (b + t) / (b - t);
		m[3][2] = (f + n) / (f - n);

		i32 loc = glGetUniformLocation(shader, "camera");
		glUniformMatrix4fv(loc, 1, GL_FALSE, (float*)m);
	}

	Renderer() {
		/* Create the shader */
		i32 r;

		u32 v = glCreateShader(GL_VERTEX_SHADER);
		glShaderSource(v, 1, (const char**)&vert_shader_src, nullptr);
		glCompileShader(v);

		glGetShaderiv(v, GL_COMPILE_STATUS, &r);
		if (!r) {
			char info[1024];
			glGetShaderInfoLog(v, 1024, nullptr, info);
			fprintf(stderr, "%s\n", info);
		}

		u32 f = glCreateShader(GL_FRAGMENT_SHADER);
		glShaderSource(f, 1, (const char**)&frag_shader_src, nullptr);
		glCompileShader(f);

		glGetShaderiv(f, GL_COMPILE_STATUS, &r);
		if (!r) {
			char info[1024];
			glGetShaderInfoLog(f, 1024, nullptr, info);
			fprintf(stderr, "%s\n", info);
		}

		shader = glCreateProgram();
		glAttachShader(shader, v);
		glAttachShader(shader, f);
		glLinkProgram(shader);

		glGetProgramiv(shader, GL_LINK_STATUS, &r);
		if (!r) {
			char info[1024];
			glGetProgramInfoLog(shader, 1024, nullptr, info);
			fprintf(stderr, "%s\n", info);
		}

		glDeleteShader(v);
		glDeleteShader(f);

		glUseProgram(shader);

		update_camera(1366.0f, 768.0f);

		/* Create the vertex buffer */
		glGenVertexArrays(1, &va);
		glGenBuffers(1, &vb);
		glGenBuffers(1, &ib);

		glBindVertexArray(va);
		glBindBuffer(GL_ARRAY_BUFFER, vb);
		glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ib);

		glBufferData(GL_ARRAY_BUFFER,
			max_quads * vpv * vpq * sizeof(float),
			nullptr, GL_DYNAMIC_DRAW);
		glBufferData(GL_ELEMENT_ARRAY_BUFFER,
			max_quads * 6 * sizeof(u32),
			nullptr, GL_DYNAMIC_DRAW);

		glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE,
			vpv * sizeof(float), (void*)(u64)(0 * sizeof(float)));
		glEnableVertexAttribArray(0);

		glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE,
			vpv * sizeof(float), (void*)(u64)(2 * sizeof(float)));
		glEnableVertexAttribArray(1);

		glVertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE,
			vpv * sizeof(float), (void*)(u64)(4 * sizeof(float)));
		glEnableVertexAttribArray(2);

		glVertexAttribPointer(3, 1, GL_FLOAT, GL_FALSE,
			vpv * sizeof(float), (void*)(u64)(8 * sizeof(float)));
		glEnableVertexAttribArray(3);

		glBindVertexArray(0);
		glBindBuffer(GL_ARRAY_BUFFER, 0);
		glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
	}

	~Renderer() {
		glDeleteVertexArrays(1, &va);
		glDeleteBuffers(1, &vb);
		glDeleteBuffers(1, &ib);

		glDeleteProgram(shader);
	}

	void flush() {
		glBindVertexArray(va);
		glDrawElements(GL_TRIANGLES,
			quad_count * 6,
			GL_UNSIGNED_INT, 0);
		glBindVertexArray(0);

		quad_count = 0;
	}

	void push(float x, float y, float w, float h, u8 r, u8 g, u8 b, u8 a, bool circle) {
		if (quad_count > max_quads) {
			flush();
		}

		float red   = (float)r / 255.0f;
		float green = (float)g / 255.0f;
		float blue  = (float)b / 255.0f;
		float alpha = (float)a / 255.0f;

		w /= 2;
		h /= 2;

		float verts[] = {
			x - w, y - h, 0.0f, 1.0f, red, green, blue, alpha, circle ? 1.0f : 0.0f,
			x + w, y - h, 1.0f, 1.0f, red, green, blue, alpha, circle ? 1.0f : 0.0f,
			x + w, y + h, 1.0f, 0.0f, red, green, blue, alpha, circle ? 1.0f : 0.0f,
			x - w, y + h, 0.0f, 0.0f, red, green, blue, alpha, circle ? 1.0f : 0.0f,
		};

		const u32 idx_off = (u32)quad_count * 4;
		u32 idxs[] = {
			idx_off + 3, idx_off + 2, idx_off + 1,
			idx_off + 3, idx_off + 1, idx_off + 0
		};

		glBindVertexArray(va);
		glBindBuffer(GL_ARRAY_BUFFER, vb);
		glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ib);

		glBufferSubData(GL_ARRAY_BUFFER,
			quad_count * vpv * vpq * sizeof(float),
			vpv * vpq * sizeof(float), verts);
		glBufferSubData(GL_ELEMENT_ARRAY_BUFFER,
			quad_count * 6 * sizeof(u32),
			6 * sizeof(u32), idxs);

		glBindVertexArray(0);
		glBindBuffer(GL_ARRAY_BUFFER, 0);
		glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);

		quad_count++;
	}
};

static float random(float min, float max) {
	return ((max - min) * ((float)rand() / RAND_MAX)) + min;
}

/* Components */
struct Position {
	float x, y;
};

struct Player {
	float speed;

	bool left, right;
};

struct Circle {
	float size;
};

struct Velocity {
	float x, y;
};

struct Square {
	float size;
};

struct Color {
	u8 r, g, b, a;
};

static Entity spawn_ball(World& world) {
	auto ball = world.new_entity();
	ball.add(Position { random(-400.0f, 400.0f), -500.0f });
	ball.add(Velocity { 0.0f, 700.0f });
	ball.add(Circle { 32.0f });
	ball.add(Color { (u8)random(0.0f, 255.0f), (u8)random(0.0f, 255.0f), (u8)random(0.0f, 255.0f), 255 });

	return ball;
}

i32 main() {
	srand(time(nullptr));

	glfwInit();

	glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);
	GLFWwindow* window = glfwCreateWindow(1366, 768, "Balls!", nullptr, nullptr);
	glfwMakeContextCurrent(window);
	glfwSwapInterval(0);

	gladLoadGL();

	glEnable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

	float now = glfwGetTime(), last = glfwGetTime();
	float ts = 0.0f;
	float timer = 0.0f;
	float difficulty = 3.0f;
	float spawn_timer = 0.0f;

	Renderer renderer;

	auto world = std::make_unique<World>();

	auto player = world->new_entity();
	player.add(Player { 800.0f, false, false });
	player.add(Position { 0.0f, 300.0f });
	player.add(Square { 100.0f });
	player.add(Color { 255, 255, 255, 255 });

	spawn_ball(*world);

	while (!glfwWindowShouldClose(window)) {
		glfwPollEvents();

		glClear(GL_COLOR_BUFFER_BIT);

		if (spawn_timer > difficulty) {
			spawn_timer = 0.0f;
			spawn_ball(*world);
		}

		for (auto view = world->new_view<Position, Velocity>(); view.valid(); view.next()) {
			auto& position = view.get<Position>();
			auto& velocity = view.get<Velocity>();

			position.x += velocity.x * ts;
			position.y += velocity.y * ts;
		}

		for (auto view = world->new_view<Player, Position, Square>(); view.valid(); view.next()) {
			auto& position = view.get<Position>();
			auto& player = view.get<Player>();
			auto& square = view.get<Square>();

			auto state = glfwGetKey(window, GLFW_KEY_LEFT);
			if (state == GLFW_PRESS) {
				player.left = true;
			} else if (state == GLFW_RELEASE) {
				player.left = false;
			}
			state = glfwGetKey(window, GLFW_KEY_RIGHT);
			if (state == GLFW_PRESS) {
				player.right = true;
			} else if (state == GLFW_RELEASE) {
				player.right = false;
			}

			if (player.left) {
				position.x -= player.speed * ts;
			}

			if (player.right) {
				position.x += player.speed * ts;
			}

			for (auto balls = world->new_view<Position, Circle>(); balls.valid(); balls.next()) {
				auto& ball_pos = balls.get<Position>();
				auto& ball = balls.get<Circle>();

				if ((position.x + square.size / 2.0f > ball_pos.x - ball.size / 2.0f &&
					position.y + square.size / 2.0f > ball_pos.y - ball.size / 2.0f &&
					position.x -square.size / 2.0f < ball_pos.x + ball.size / 2.0f &&
					position.y -square.size / 2.0f < ball_pos.y + ball.size / 2.0f) || ball_pos.y > 500.0f) {
					balls.get_entity().destroy();
				}
			}
		}

		for (auto view = world->new_view<Position, Square, Color>(); view.valid(); view.next()) {
			auto& position = view.get<Position>();
			auto& square = view.get<Square>();
			auto& color = view.get<Color>();

			renderer.push(position.x, position.y, square.size, square.size, color.r, color.g, color.b, color.a, false);
		}

		for (auto view = world->new_view<Position, Circle, Color>(); view.valid(); view.next()) {
			auto& position = view.get<Position>();
			auto& circle = view.get<Circle>();
			auto& color = view.get<Color>();

			renderer.push(position.x, position.y, circle.size, circle.size, color.r, color.g, color.b, color.a, true);
		}

		renderer.flush();

		glfwSwapBuffers(window);

		now = glfwGetTime();
		ts = now - last;
		last = now;

		timer += ts;
		spawn_timer += ts;

		difficulty -= ts * 0.05f;

		if (timer > 1.0f) {
			timer = 0.0f;
			printf("Timestep: %f\tFramerate: %f\tEntities: %lu\n", ts, 1.0f / ts, world->count());

			/* Collect garbage every second. */
			world->collect_garbage();
		}
	}

	glfwDestroyWindow(window);

	glfwTerminate();
}
